/* SAT multi-channel sound on SDL3 audio streams.
   Named 'snd ' resources become assets/sounds/<slug>.wav; the slug rule here
   must match tools/gen_sounds (lowercase, alnum kept, runs of anything else
   collapse to one dash). */

#include "../mySAT.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_CHANNELS 8

typedef struct SoundRec {
	Uint8 *buf;
	Uint32 len;
} SoundRec;

static SDL_AudioStream *g_chan[MAX_CHANNELS];
static int g_numChannels;
static Boolean g_soundOn = true;
static SDL_AudioSpec g_spec = { SDL_AUDIO_F32, 2, 44100 };

extern const char *SATAssetPathPublic(const char *rel);

static void Slugify(const char *name, char *out, size_t outLen)
{
	size_t o = 0;
	Boolean pendingDash = false;
	for (const char *p = name; *p && o + 2 < outLen; p++) {
		if (isalnum((unsigned char)*p)) {
			if (pendingDash && o > 0)
				out[o++] = '-';
			pendingDash = false;
			out[o++] = (char)tolower((unsigned char)*p);
		} else {
			pendingDash = true;
		}
	}
	out[o] = 0;
}

short SATSoundInitChannels(short num)
{
	if (num > MAX_CHANNELS)
		num = MAX_CHANNELS;
	for (int i = 0; i < num; i++) {
		g_chan[i] = SDL_OpenAudioDeviceStream(
		    SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &g_spec, NULL, NULL);
		if (g_chan[i])
			SDL_ResumeAudioStreamDevice(g_chan[i]);
	}
	g_numChannels = num;
	return num;
}

Handle SATGetNamedSound(const char *name)
{
	char slug[128], rel[192];
	Slugify(name, slug, sizeof slug);
	snprintf(rel, sizeof rel, "sounds/%s.wav", slug);

	SDL_AudioSpec spec;
	Uint8 *data;
	Uint32 len;
	if (!SDL_LoadWAV(SATAssetPathPublic(rel), &spec, &data, &len)) {
		SDL_Log("missing sound asset: %s", rel);
		return nil;
	}
	Uint8 *conv;
	int convLen;
	if (!SDL_ConvertAudioSamples(&spec, data, (int)len, &g_spec, &conv,
	                             &convLen)) {
		SDL_free(data);
		return nil;
	}
	SDL_free(data);
	SoundRec *rec = calloc(1, sizeof *rec);
	rec->buf = conv;
	rec->len = (Uint32)convLen;
	return rec;
}

void SATSoundPlay(Handle theSound, short priority, Boolean canWait)
{
	(void)priority; (void)canWait;
	SoundRec *rec = theSound;
	if (!rec || !g_soundOn || g_numChannels == 0)
		return;

	int best = -1;
	Uint32 bestQueued = ~0u;
	for (int i = 0; i < g_numChannels; i++) {
		if (!g_chan[i])
			continue;
		Uint32 q = (Uint32)SDL_GetAudioStreamQueued(g_chan[i]);
		if (q == 0) {
			best = i;
			break;
		}
		if (q < bestQueued) {
			bestQueued = q;
			best = i;
		}
	}
	if (best < 0)
		return;
	SDL_ClearAudioStream(g_chan[best]);
	SDL_PutAudioStreamData(g_chan[best], rec->buf, (int)rec->len);
}

Boolean SATSoundDone(void)
{
	for (int i = 0; i < g_numChannels; i++)
		if (g_chan[i] && (SDL_GetAudioStreamQueued(g_chan[i]) > 0 ||
		                  SDL_GetAudioStreamAvailable(g_chan[i]) > 0))
			return false;
	return true;
}

void SATSoundShutup(void)
{
	for (int i = 0; i < g_numChannels; i++)
		if (g_chan[i])
			SDL_ClearAudioStream(g_chan[i]);
}

void SATSoundEvents(void) {}
void SATPreloadChannels(void) {}
void SATSoundOn(void) { g_soundOn = true; }
void SATSoundOff(void)
{
	g_soundOn = false;
	SATSoundShutup();
}
