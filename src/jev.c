#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>

#include "jev.h"
#include "bot.h"

/* The macOS builds link libcurl, which ships with the system. The Windows
   cross-build has no HTTP client, so it compiles the stub at the foot of
   this file and its computer player runs on the scripted tactics alone. */

#ifdef ASCENT_HAVE_CURL

#include <curl/curl.h>

#define kStateMax		8192
#define kBodyMax		12288
#define kReplyMax		16384
#define kBackoffAfter	4			/* consecutive failures before slowing down */
#define kBackoffMs		3000

/* The questions never change, so they are built once as text. All three are
   answered in one call, which the API bills and times as a single request. */

static const char kQuestions[] =
"\"questions\":{"
"\"tactic\":{\"type\":\"choice\","
"\"instructions\":\"Choose the single best thing for the red ship (`me`) to do "
"over the next second. This is a two-ship arena game: I score by throwing the "
"ball into `target_goal`, I concede if the opponent throws it into `my_goal`, "
"and shooting the opponent drains their shields and knocks the ball out of "
"their grip. Read `me`, `opponent`, `ball`, `powerup_canister` and "
"`barrier`.\","
"\"criteria\":{"
"\"chase_ball\":\"The ball is loose. Fly into it to catch it.\","
"\"carry_to_goal\":\"I am carrying the ball. Run it toward target_goal and throw it in.\","
"\"intercept_carrier\":\"The opponent is carrying the ball. Chase them and shoot to knock it loose before they score.\","
"\"attack_opponent\":\"Get level with the opponent and shoot them, to drain their shields or kill them.\","
"\"grab_powerup\":\"Collect the powerup canister, which is worth more right now than the ball.\","
"\"defend_goal\":\"Drop back and hold station between the ball and my_goal to block a shot.\","
"\"evade\":\"I am in danger. Break away from the opponent and the barrier and stay alive.\""
"}},"
"\"fire\":{\"type\":\"noul\",\"instructions\":\"Firing the cannon this instant is "
"worth it: the opponent or the ball is roughly level with me, on the side I "
"face, and near enough to hit.\"},"
"\"use_powerup\":{\"type\":\"noul\",\"instructions\":\"This is a good moment to "
"spend the powerup named in `me.powerup`. False when I am holding none, or "
"when saving it is better.\"}"
"}";

/*Worker state. Everything here is shared with the game thread under gLock.*/

static SDL_Thread		*gThread;
static SDL_Mutex		*gLock;
static SDL_Condition	*gWake;

static Boolean			gRunning;			/* worker should keep looping */
static Boolean			gHaveRequest;		/* a state is waiting to be sent */
static Boolean			gHaveReply;			/* an answer is waiting to be read */
static Boolean			gInFlight;			/* a call is out */
static char				gState[kStateMax];
static JevDecision		gReply;
static char				gError[256];
static int				gCalls, gFails;
static double			gLatencyMs;

static char				gKey[512];
static char				gModel[64];

/* ---- a small JSON reader, enough for the answers we ask for ---- */

static const char *SkipWs(const char *p, const char *e)
{
	while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
		p++;
	return p;
}

static const char *SkipString(const char *p, const char *e)	/* p is at '"' */
{
	for (p++; p < e; p++) {
		if (*p == '\\') {
			p++;
			continue;
		}
		if (*p == '"')
			return p + 1;
	}
	return NULL;
}

static const char *SkipValue(const char *p, const char *e)
{
	p = SkipWs(p, e);
	if (p >= e)
		return NULL;
	if (*p == '"')
		return SkipString(p, e);
	if (*p == '{' || *p == '[') {
		char open = *p, close = (*p == '{') ? '}' : ']';
		int depth = 0;
		while (p < e) {
			if (*p == '"') {
				p = SkipString(p, e);
				if (!p)
					return NULL;
				continue;
			}
			if (*p == open)
				depth++;
			else if (*p == close && --depth == 0)
				return p + 1;
			p++;
		}
		return NULL;
	}
	while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' &&
	       *p != '\n' && *p != '\r' && *p != '\t')
		p++;
	return p;
}

/* Finds a member of the object at `obj`, looking only at that object's own
   keys, so the same name nested deeper cannot be mistaken for it. */
static Boolean Member(const char *obj, const char *end, const char *name,
                      const char **vs, const char **ve)
{
	size_t nlen = SDL_strlen(name);
	const char *p = SkipWs(obj, end);

	if (p >= end || *p != '{')
		return false;
	for (p++;;) {
		p = SkipWs(p, end);
		if (p >= end || *p != '"')
			return false;
		const char *ks = p + 1;
		const char *kend = SkipString(p, end);
		if (!kend)
			return false;
		p = SkipWs(kend, end);
		if (p >= end || *p != ':')
			return false;
		const char *vstart = SkipWs(p + 1, end);
		const char *vend = SkipValue(vstart, end);
		if (!vend)
			return false;
		if ((size_t)(kend - 1 - ks) == nlen && !SDL_strncmp(ks, name, nlen)) {
			*vs = vstart;
			*ve = vend;
			return true;
		}
		p = SkipWs(vend, end);
		if (p >= end || *p != ',')
			return false;
		p++;
	}
}

static Boolean MemberStr(const char *obj, const char *end, const char *name,
                         char *out, size_t outSize)
{
	const char *vs, *ve;

	if (!Member(obj, end, name, &vs, &ve) || *vs != '"')
		return false;
	size_t n = (size_t)(ve - vs) - 2;
	if (n >= outSize)
		n = outSize - 1;
	SDL_memcpy(out, vs + 1, n);
	out[n] = 0;
	return true;
}

static Boolean MemberNum(const char *obj, const char *end, const char *name,
                         double *out)
{
	const char *vs, *ve;
	char buf[64];

	if (!Member(obj, end, name, &vs, &ve))
		return false;
	size_t n = (size_t)(ve - vs);
	if (n == 0 || n >= sizeof buf)
		return false;
	SDL_memcpy(buf, vs, n);
	buf[n] = 0;
	*out = SDL_atof(buf);
	return true;
}

static Boolean ParseAnswers(const char *body, size_t len, JevDecision *out)
{
	const char *end = body + len, *as, *ae, *qs, *qe;
	char choice[64];

	if (!Member(body, end, "answers", &as, &ae))
		return false;

	out->tactic = -1;
	out->confidence = 0;
	out->fire = 0;
	out->usePowerup = 0;

	if (Member(as, ae, "tactic", &qs, &qe)) {
		if (MemberStr(qs, qe, "choice", choice, sizeof choice))
			out->tactic = BotTacticFromName(choice);
		MemberNum(qs, qe, "confidence", &out->confidence);
	}
	if (Member(as, ae, "fire", &qs, &qe))
		MemberNum(qs, qe, "noul", &out->fire);
	if (Member(as, ae, "use_powerup", &qs, &qe))
		MemberNum(qs, qe, "noul", &out->usePowerup);

	return out->tactic >= 0;
}

/* ---- the worker ---- */

typedef struct Reply {
	char	buf[kReplyMax];
	size_t	len;
} Reply;

static size_t OnBody(void *data, size_t size, size_t count, void *userp)
{
	Reply *r = (Reply *)userp;
	size_t n = size * count;

	if (r->len + n < sizeof r->buf) {
		SDL_memcpy(r->buf + r->len, data, n);
		r->len += n;
	}
	return n;	/* claim it all even when full, so curl does not error out */
}

static void NoteFailure(const char *what)
{
	SDL_LockMutex(gLock);
	SDL_strlcpy(gError, what, sizeof gError);
	gFails++;
	gInFlight = false;
	SDL_UnlockMutex(gLock);
}

static int SDLCALL JevWorker(void *unused)
{
	CURL *curl;
	struct curl_slist *headers = NULL;
	char auth[560], body[kBodyMax], state[kStateMax];
	int consecutiveFails = 0;

	(void)unused;

	curl = curl_easy_init();
	if (!curl) {
		NoteFailure("could not start the HTTP client");
		return 0;
	}

	SDL_snprintf(auth, sizeof auth, "Authorization: Bearer %s", gKey);
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, auth);

	curl_easy_setopt(curl, CURLOPT_URL, "https://api.typesafe.ai/v1/systemone");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnBody);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2500L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	/* One handle for the whole match, so the connection is reused and only
	   the first call pays for DNS and the TLS handshake. */

	for (;;) {
		SDL_LockMutex(gLock);
		while (gRunning && !gHaveRequest)
			SDL_WaitCondition(gWake, gLock);
		if (!gRunning) {
			SDL_UnlockMutex(gLock);
			break;
		}
		SDL_memcpy(state, gState, sizeof state);
		gHaveRequest = false;
		SDL_UnlockMutex(gLock);

		int n = SDL_snprintf(body, sizeof body,
		                     "{\"model\":\"%s\",\"state\":%s,%s}",
		                     gModel, state, kQuestions);
		if (n <= 0 || (size_t)n >= sizeof body) {
			NoteFailure("state too large to send");
			continue;
		}

		Reply reply;
		reply.len = 0;
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)n);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);

		Uint64 t0 = SDL_GetTicksNS();
		CURLcode rc = curl_easy_perform(curl);
		double ms = (double)(SDL_GetTicksNS() - t0) / 1.0e6;

		if (rc != CURLE_OK) {
			NoteFailure(curl_easy_strerror(rc));
			consecutiveFails++;
		} else {
			long status = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
			JevDecision d;
			if (status != 200) {
				char msg[256];
				SDL_snprintf(msg, sizeof msg, "HTTP %ld from the API", status);
				NoteFailure(msg);
				consecutiveFails++;
			} else if (!ParseAnswers(reply.buf, reply.len, &d)) {
				NoteFailure("could not read the answer");
				consecutiveFails++;
			} else {
				SDL_LockMutex(gLock);
				gReply = d;
				gHaveReply = true;
				gInFlight = false;
				gCalls++;
				gLatencyMs = ms;
				gError[0] = 0;
				SDL_UnlockMutex(gLock);
				consecutiveFails = 0;
			}
		}

		/* A rejected key, or an account over its limit, fails on every call.
		   Ease off rather than spending the whole match retrying. */
		if (consecutiveFails >= kBackoffAfter)
			SDL_Delay(kBackoffMs);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return 0;
}

/* ---- the game thread's side ---- */

Boolean JevStart(void)
{
	const char *key = SDL_getenv("TYPESAFE_API_KEY");
	const char *model = SDL_getenv("ASCENT_JEV_MODEL");

	if (gThread)
		return true;
	if (!key || !*key) {
		SDL_strlcpy(gError, "TYPESAFE_API_KEY is not set", sizeof gError);
		return false;
	}
	SDL_strlcpy(gKey, key, sizeof gKey);
	SDL_strlcpy(gModel, (model && *model) ? model : "jev-latest",
	            sizeof gModel);

	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		SDL_strlcpy(gError, "could not start libcurl", sizeof gError);
		return false;
	}
	gLock = SDL_CreateMutex();
	gWake = SDL_CreateCondition();
	if (!gLock || !gWake) {
		SDL_strlcpy(gError, "could not create the worker's lock",
		            sizeof gError);
		return false;
	}
	gRunning = true;
	gHaveRequest = gHaveReply = gInFlight = false;
	gCalls = gFails = 0;
	gThread = SDL_CreateThread(JevWorker, "jev", NULL);
	if (!gThread) {
		gRunning = false;
		SDL_strlcpy(gError, "could not start the worker thread",
		            sizeof gError);
		return false;
	}
	return true;
}

void JevStop(void)
{
	if (!gThread)
		return;
	SDL_LockMutex(gLock);
	gRunning = false;
	SDL_SignalCondition(gWake);
	SDL_UnlockMutex(gLock);
	SDL_WaitThread(gThread, NULL);	/* the timeout bounds how long this takes */
	gThread = NULL;
	SDL_DestroyCondition(gWake);
	SDL_DestroyMutex(gLock);
	gWake = NULL;
	gLock = NULL;
	curl_global_cleanup();
}

Boolean JevRunning(void)
{
	return gThread != NULL;
}

Boolean JevIdle(void)
{
	Boolean idle;

	if (!gThread)
		return false;
	SDL_LockMutex(gLock);
	idle = !gInFlight;
	SDL_UnlockMutex(gLock);
	return idle;
}

void JevSubmit(const char *stateJson)
{
	if (!gThread)
		return;
	SDL_LockMutex(gLock);
	SDL_strlcpy(gState, stateJson, sizeof gState);
	gHaveRequest = true;
	gInFlight = true;
	SDL_SignalCondition(gWake);
	SDL_UnlockMutex(gLock);
}

Boolean JevPoll(JevDecision *out)
{
	Boolean got = false;

	if (!gThread)
		return false;
	SDL_LockMutex(gLock);
	if (gHaveReply) {
		*out = gReply;
		gHaveReply = false;
		got = true;
	}
	SDL_UnlockMutex(gLock);
	return got;
}

const char *JevLastError(void)
{
	static char copy[256];

	if (!gLock)
		return gError;
	SDL_LockMutex(gLock);
	SDL_strlcpy(copy, gError, sizeof copy);
	SDL_UnlockMutex(gLock);
	return copy;
}

int JevCallCount(void)
{
	return gCalls;
}

int JevFailCount(void)
{
	return gFails;
}

double JevLastLatencyMs(void)
{
	return gLatencyMs;
}

#else /* no HTTP client in this build */

Boolean JevStart(void) { return false; }
void JevStop(void) {}
Boolean JevRunning(void) { return false; }
Boolean JevIdle(void) { return false; }
void JevSubmit(const char *stateJson) { (void)stateJson; }
Boolean JevPoll(JevDecision *out) { (void)out; return false; }
int JevCallCount(void) { return 0; }
int JevFailCount(void) { return 0; }
double JevLastLatencyMs(void) { return 0; }

const char *JevLastError(void)
{
	return "this build has no HTTP client, so the computer plays its own tactics";
}

#endif
