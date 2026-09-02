/* SAT sprite engine and QuickDraw subset on SDL3.
   Replaces SAT(PPC).lib and the Toolbox for the Ascent port.

   Rendering model: the original SAT did dirty-rect updates between three
   buffers (backScreen -> offScreen -> window). Here backScreen is a plain
   surface, and every frame the whole scene is recomposed into the screen
   surface and pushed to an 800x600 logical-size texture. */

#include "../mySAT.h"
#include <SDL3_image/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAME_W 800
#define GAME_H 600

SATglobalsRec gSAT;
Boolean gSATQuitRequested = false;

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_frameTex;
static SDL_Surface *g_back;   /* static background */
static SDL_Surface *g_screen; /* per-frame composite; also the menu canvas */
static GrafPort g_backPort, g_screenPort;
static int g_gamma = 100;
static char g_assetRoot[1024];

static SDL_Surface *g_overlaySurf;
static Rect g_overlayRect;

/* ---- internal sprite bookkeeping ---- */

typedef struct SpriteNode {
	SATSprite s;
	Boolean hadTask;
	long seq;
} SpriteNode;

static SpriteNode *g_trash; /* dead sprites; memory stays valid until re-init
                               because game code reads fields after death */
static long g_spriteSeq;

static FacePtr g_faceCache;

/* ---- asset paths ---- */

static const char *AssetPath(const char *rel)
{
	static char buf[1200];
	snprintf(buf, sizeof buf, "%s/%s", g_assetRoot, rel);
	return buf;
}

const char *SATAssetPathPublic(const char *rel) { return AssetPath(rel); }

static void FindAssetRoot(void)
{
	const char *base = SDL_GetBasePath();
	const char *candidates[] = { "assets", "../assets", "../../assets" };
	char probe[1200];
	if (base) {
		for (size_t i = 0; i < SDL_arraysize(candidates); i++) {
			snprintf(probe, sizeof probe, "%s%s/pics", base, candidates[i]);
			if (SDL_GetPathInfo(probe, NULL)) {
				snprintf(g_assetRoot, sizeof g_assetRoot, "%s%s", base,
				         candidates[i]);
				return;
			}
		}
	}
	snprintf(g_assetRoot, sizeof g_assetRoot, "assets");
}

static SDL_Surface *LoadPNG(const char *rel)
{
	SDL_Surface *raw = IMG_Load(AssetPath(rel));
	if (!raw)
		return NULL;
	SDL_Surface *conv = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
	SDL_DestroySurface(raw);
	return conv;
}

static SDL_Surface *PlaceholderSurface(int w, int h)
{
	SDL_Surface *s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
	const Uint32 magenta = 0xffff00ff, dark = 0xff400040;
	Uint32 *px = s->pixels;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			px[y * (s->pitch / 4) + x] =
			    (((x / 4) + (y / 4)) & 1) ? magenta : dark;
	return s;
}

/* ---- window and presentation ---- */

void SATInitToolbox(void)
{
	if (g_window)
		return;
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		exit(1);
	}
	if (!SDL_CreateWindowAndRenderer("Ascent", GAME_W, GAME_H,
	                                 SDL_WINDOW_RESIZABLE, &g_window,
	                                 &g_renderer)) {
		fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
		exit(1);
	}
	SDL_SetRenderLogicalPresentation(g_renderer, GAME_W, GAME_H,
	                                 SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_SetRenderVSync(g_renderer, 1);
	g_frameTex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
	                               SDL_TEXTUREACCESS_STREAMING, GAME_W, GAME_H);
	g_screen = SDL_CreateSurface(GAME_W, GAME_H, SDL_PIXELFORMAT_ARGB8888);
	g_back = SDL_CreateSurface(GAME_W, GAME_H, SDL_PIXELFORMAT_ARGB8888);
	SDL_FillSurfaceRect(g_back, NULL, 0xff000000);
	SDL_FillSurfaceRect(g_screen, NULL, 0xff000000);
	g_screenPort.portBits.s = g_screen;
	SetRect(&g_screenPort.portRect, 0, 0, GAME_W, GAME_H);
	g_backPort.portBits.s = g_back;
	SetRect(&g_backPort.portRect, 0, 0, GAME_W, GAME_H);

	gSAT.wind.port = &g_screenPort;
	SetRect(&gSAT.wind.bounds, 0, 0, GAME_W, GAME_H);
	gSAT.offScreen.port = &g_screenPort;
	gSAT.offScreen.bounds = gSAT.wind.bounds;
	gSAT.backScreen.port = &g_backPort;
	gSAT.backScreen.bounds = gSAT.wind.bounds;
	gSAT.offSizeH = GAME_W;
	gSAT.offSizeV = GAME_H;

	FindAssetRoot();
	SetPort(&g_screenPort);
}

/* Testing hooks: ASCENT_SHOTDIR dumps the composite every 30 presents;
   ASCENT_AUTOQUIT=<n> exits after n presents. */
static void TestHooks(void)
{
	static long presents;
	presents++;
	const char *dir = SDL_getenv("ASCENT_SHOTDIR");
	if (dir && presents % 30 == 0) {
		char path[1200];
		snprintf(path, sizeof path, "%s/frame-%05ld.png", dir, presents);
		IMG_SavePNG(g_screen, path);
	}
	const char *quit = SDL_getenv("ASCENT_AUTOQUIT");
	if (quit && presents >= atol(quit))
		exit(0);
}

/* Renderer-level screenshot for UI drawn on top of the frame texture */
void SATDumpRendererShot(const char *tag)
{
	static long n;
	const char *dir = SDL_getenv("ASCENT_SHOTDIR");
	if (!dir)
		return;
	if (++n % 30 != 0)
		return;
	SDL_Surface *s = SDL_RenderReadPixels(g_renderer, NULL);
	if (s) {
		char path[1200];
		snprintf(path, sizeof path, "%s/ui-%s-%05ld.png", dir, tag, n);
		IMG_SavePNG(s, path);
		SDL_DestroySurface(s);
	}
}

void SATPresent(void)
{
	TestHooks();
	SDL_UpdateTexture(g_frameTex, NULL, g_screen->pixels, g_screen->pitch);
	Uint8 level = (Uint8)((g_gamma * 255) / 100);
	SDL_SetTextureColorMod(g_frameTex, level, level, level);
	SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
	SDL_RenderClear(g_renderer);
	SDL_RenderTexture(g_renderer, g_frameTex, NULL, NULL);
	SDL_RenderPresent(g_renderer);
}

void SATSetGammaLevel(int percent)
{
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	g_gamma = percent;
}

Boolean SATPumpEvents(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_EVENT_QUIT)
			gSATQuitRequested = true;
		if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN &&
		    (e.key.mod & SDL_KMOD_GUI))
			SDL_SetWindowFullscreen(g_window,
			                        !(SDL_GetWindowFlags(g_window) &
			                          SDL_WINDOW_FULLSCREEN));
	}
	return !gSATQuitRequested;
}

/* ---- SAT init / config ---- */

static void FreeTrash(void)
{
	while (g_trash) {
		SpriteNode *n = g_trash;
		g_trash = (SpriteNode *)g_trash->s.next;
		free(n);
	}
}

void SATCustomInit(short pictID, short bwpictID, Rect *area, WindowPtr wind,
                   GDHandle gd, Boolean useMenuBar, Boolean center,
                   Boolean fillScreen, Boolean dither4bit, Boolean beSmart)
{
	(void)bwpictID; (void)area; (void)wind; (void)gd; (void)useMenuBar;
	(void)center; (void)fillScreen; (void)dither4bit; (void)beSmart;

	while (gSAT.sRoot)
		SATKillSprite(gSAT.sRoot);
	FreeTrash();
	SATOverlayClear();

	PicHandle bg = GetPicture(pictID);
	Rect full;
	SetRect(&full, 0, 0, GAME_W, GAME_H);
	GrafPtr save = GetCurrentPort();
	SetPort(&g_backPort);
	DrawPicture(bg, &full);
	SetPort(save);
	DisposeHandle((Handle)bg);
	SDL_BlitSurface(g_back, NULL, g_screen, NULL);
}

void SATConfigure(Boolean fit, short sorting, short collision, short sw)
{
	(void)fit; (void)sorting; (void)collision; (void)sw;
}

void SATSetSpriteRecSize(long size) { (void)size; }
Boolean SATDepthChangeTest(void) { return false; }
void SATHideMBar(WindowPtr w) { (void)w; }
void SATShowMBar(WindowPtr w) { (void)w; }
void SATBackChanged(Rect *r) { (void)r; }

/* ---- sprites ---- */

SpritePtr SATNewSprite(short kind, short hpos, short vpos, TaskPtr setup)
{
	SpriteNode *n = calloc(1, sizeof *n);
	n->seq = ++g_spriteSeq;
	SATSprite *s = &n->s;
	s->kind = kind;
	s->position.h = hpos;
	s->position.v = vpos;
	if (setup)
		setup(s);
	n->hadTask = (s->task != nil);

	/* append at tail to keep creation order (draw tiebreak, run order) */
	if (!gSAT.sRoot) {
		gSAT.sRoot = s;
	} else {
		SATSprite *t = gSAT.sRoot;
		while (t->next)
			t = t->next;
		t->next = s;
		s->prev = t;
	}
	return s;
}

void SATKillSprite(SpritePtr who)
{
	if (who->prev)
		who->prev->next = who->next;
	else
		gSAT.sRoot = who->next;
	if (who->next)
		who->next->prev = who->prev;
	who->prev = nil;
	/* onto the trash list; memory stays valid (game reads dead sprites) */
	who->next = (SATSprite *)g_trash;
	g_trash = (SpriteNode *)who;
}

static int CompareDraw(const void *a, const void *b)
{
	const SATSprite *sa = *(SATSprite *const *)a;
	const SATSprite *sb = *(SATSprite *const *)b;
	if (sa->layer != sb->layer)
		return sa->layer - sb->layer;
	long da = ((const SpriteNode *)sa)->seq - ((const SpriteNode *)sb)->seq;
	return da < 0 ? -1 : (da > 0 ? 1 : 0);
}

static Boolean RectsOverlap(const Rect *a, const Rect *b)
{
	if (a->right <= a->left || a->bottom <= a->top)
		return false;
	if (b->right <= b->left || b->bottom <= b->top)
		return false;
	return a->left < b->right && b->left < a->right && a->top < b->bottom &&
	       b->top < a->bottom;
}

static void RunFrame(void)
{
	enum { MAXS = 512 };
	static SATSprite *list[MAXS];
	int count = 0;

	for (SATSprite *s = gSAT.sRoot; s && count < MAXS; s = s->next)
		list[count++] = s;

	/* task phase: only sprites that had a task at frame start */
	for (int i = 0; i < count; i++) {
		SATSprite *s = list[i];
		if (((SpriteNode *)s)->hadTask == false && s->task != nil)
			((SpriteNode *)s)->hadTask = true;
		if (s->task)
			s->task(s);
	}

	/* collision phase over the same snapshot */
	for (int i = 0; i < count; i++) {
		SATSprite *s = list[i];
		s->hotRect2 = s->hotRect;
		OffsetRect(&s->hotRect2, s->position.h, s->position.v);
	}
	for (int i = 0; i < count; i++) {
		for (int j = i + 1; j < count; j++) {
			SATSprite *a = list[i], *b = list[j];
			if (!RectsOverlap(&a->hotRect2, &b->hotRect2))
				continue;
			if (a->hitTask)
				a->hitTask(a, b);
			if (b->hitTask)
				b->hitTask(b, a);
		}
	}

	/* sweep: a sprite that once had a task and now has none is dead */
	SATSprite *next;
	for (SATSprite *s = gSAT.sRoot; s; s = next) {
		next = s->next;
		if (((SpriteNode *)s)->hadTask && s->task == nil)
			SATKillSprite(s);
		else if (s->task != nil)
			((SpriteNode *)s)->hadTask = true;
	}
}

static void Compose(void)
{
	SDL_BlitSurface(g_back, NULL, g_screen, NULL);

	enum { MAXS = 512 };
	static SATSprite *list[MAXS];
	int count = 0;
	for (SATSprite *s = gSAT.sRoot; s && count < MAXS; s = s->next)
		if (s->face && s->face->surf)
			list[count++] = s;
	qsort(list, count, sizeof list[0], CompareDraw);
	for (int i = 0; i < count; i++) {
		SATSprite *s = list[i];
		SDL_Rect dst = { s->position.h, s->position.v, s->face->width,
			             s->face->height };
		SDL_BlitSurface(s->face->surf, NULL, g_screen, &dst);
	}

	if (g_overlaySurf) {
		SDL_Rect dst = { g_overlayRect.left, g_overlayRect.top,
			             g_overlayRect.right - g_overlayRect.left,
			             g_overlayRect.bottom - g_overlayRect.top };
		SDL_BlitSurfaceScaled(g_overlaySurf, NULL, g_screen, &dst,
		                      SDL_SCALEMODE_LINEAR);
	}
}

void SATRun2(Boolean fast)
{
	(void)fast;
	RunFrame();
	Compose();
	SATPresent();
}

void SATRun(Boolean fast) { SATRun2(fast); }

void SATRedraw(void)
{
	Compose();
	SATPresent();
}

/* ---- overlay (countdown numbers etc., drawn above sprites) ---- */

void SATOverlayPic(short picID, Rect *dst)
{
	SATOverlayClear();
	PicHandle p = GetPicture(picID);
	g_overlaySurf = p->s;
	free(p); /* keep the surface, drop the pict wrapper */
	g_overlayRect = *dst;
}

void SATOverlayClear(void)
{
	if (g_overlaySurf) {
		SDL_DestroySurface(g_overlaySurf);
		g_overlaySurf = NULL;
	}
}

/* ---- faces ---- */

FacePtr SATGetFace(short resNum)
{
	for (FacePtr f = g_faceCache; f; f = f->next)
		if (f->resNum == resNum)
			return f;

	char rel[64];
	snprintf(rel, sizeof rel, "sprites/%d.png", resNum);
	SDL_Surface *s = LoadPNG(rel);
	if (!s) {
		SDL_Log("missing sprite asset: %s", rel);
		s = PlaceholderSurface(24, 24);
	}
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);

	FacePtr f = calloc(1, sizeof *f);
	f->surf = s;
	f->resNum = resNum;
	f->width = s->w;
	f->height = s->h;
	f->next = g_faceCache;
	g_faceCache = f;
	return f;
}

/* ---- misc ---- */

short SATRand(short n)
{
	if (n <= 0)
		return 0;
	return (short)(rand() % n);
}

void SATDrawInt(short i) { (void)i; }

/* ================= QuickDraw subset ================= */

static GrafPtr g_curPort;
static Uint32 g_foreColor = 0xff000000;
static Uint32 g_backColor = 0xffffffff;
static Point g_pen;

void SetPort(GrafPtr port) { g_curPort = port; }
GrafPtr GetCurrentPort(void) { return g_curPort; }

void SATGetPort(SATPort *port) { port->port = g_curPort; }
void SATSetPort(SATPort *port) { g_curPort = port->port; }
void SATSetPortBackScreen(void) { g_curPort = &g_backPort; }
void SATSetPortOffScreen(void) { g_curPort = &g_screenPort; }
void SATSetPortScreen(void) { g_curPort = &g_screenPort; }

void SetRect(Rect *r, short left, short top, short right, short bottom)
{
	r->left = left;
	r->top = top;
	r->right = right;
	r->bottom = bottom;
}

void OffsetRect(Rect *r, short dh, short dv)
{
	r->left += dh;
	r->right += dh;
	r->top += dv;
	r->bottom += dv;
}

void InsetRect(Rect *r, short dh, short dv)
{
	r->left += dh;
	r->right -= dh;
	r->top += dv;
	r->bottom -= dv;
}

Boolean PtInRect(Point pt, const Rect *r)
{
	return pt.h >= r->left && pt.h < r->right && pt.v >= r->top &&
	       pt.v < r->bottom;
}

long TickCount(void) { return (long)(SDL_GetTicks() * 60 / 1000); }

static Uint32 RGBToPixel(const RGBColor *c)
{
	return 0xff000000u | ((Uint32)(c->red >> 8) << 16) |
	       ((Uint32)(c->green >> 8) << 8) | (Uint32)(c->blue >> 8);
}

static Uint32 QDColorToPixel(long qd)
{
	switch (qd) {
	case whiteColor: return 0xffffffff;
	case redColor:   return 0xffdd0000;
	case greenColor: return 0xff00bb00;
	case blueColor:  return 0xff0000dd;
	case blackColor:
	default:         return 0xff000000;
	}
}

void RGBForeColor(const RGBColor *c) { g_foreColor = RGBToPixel(c); }
void ForeColor(long qd) { g_foreColor = QDColorToPixel(qd); }
void BackColor(long qd) { g_backColor = QDColorToPixel(qd); }

void MoveTo(short h, short v)
{
	g_pen.h = h;
	g_pen.v = v;
}

static void PutPixel(SDL_Surface *s, int x, int y, Uint32 c)
{
	if (x < 0 || y < 0 || x >= s->w || y >= s->h)
		return;
	((Uint32 *)s->pixels)[y * (s->pitch / 4) + x] = c;
}

void LineTo(short h, short v)
{
	SDL_Surface *s = g_curPort->portBits.s;
	int x0 = g_pen.h, y0 = g_pen.v, x1 = h, y1 = v;
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (;;) {
		PutPixel(s, x0, y0, g_foreColor);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
	g_pen.h = h;
	g_pen.v = v;
}

static SDL_Rect ClipRect(const Rect *r)
{
	SDL_Rect out = { r->left, r->top, r->right - r->left, r->bottom - r->top };
	return out;
}

void PaintRect(const Rect *r)
{
	SDL_Rect d = ClipRect(r);
	SDL_FillSurfaceRect(g_curPort->portBits.s, &d, g_foreColor);
}

void EraseRect(const Rect *r)
{
	SDL_Rect d = ClipRect(r);
	SDL_FillSurfaceRect(g_curPort->portBits.s, &d, g_backColor);
}

void FrameRect(const Rect *r)
{
	SDL_Surface *s = g_curPort->portBits.s;
	SDL_Rect edges[4] = {
		{ r->left, r->top, r->right - r->left, 1 },
		{ r->left, r->bottom - 1, r->right - r->left, 1 },
		{ r->left, r->top, 1, r->bottom - r->top },
		{ r->right - 1, r->top, 1, r->bottom - r->top },
	};
	for (int i = 0; i < 4; i++)
		SDL_FillSurfaceRect(s, &edges[i], g_foreColor);
}

void GetPenState(PenState *p) { p->pnLoc = g_pen; }
void SetPenState(const PenState *p) { g_pen = p->pnLoc; }
void TextSize(short size) { (void)size; }
void TextFont(short font) { (void)font; }
void DrawString(const char *str) { (void)str; }

/* ---- pictures ---- */

PicHandle GetPicture(short id)
{
	char rel[64];
	snprintf(rel, sizeof rel, "pics/%d.png", id);
	SDL_Surface *s = LoadPNG(rel);
	if (!s) {
		SDL_Log("missing picture asset: %s", rel);
		s = PlaceholderSurface(64, 48);
	}
	PicHandle p = calloc(1, sizeof *p);
	p->s = s;
	return p;
}

void DrawPicture(PicHandle pic, const Rect *dst)
{
	SDL_Rect d = ClipRect(dst);
	SDL_SetSurfaceBlendMode(pic->s, SDL_BLENDMODE_BLEND);
	SDL_BlitSurfaceScaled(pic->s, NULL, g_curPort->portBits.s, &d,
	                      SDL_SCALEMODE_LINEAR);
}

void DisposeHandle(Handle h)
{
	PicHandle p = h;
	if (!p)
		return;
	if (p->s)
		SDL_DestroySurface(p->s);
	free(p);
}

/* ---- buffers (offscreen save/restore areas) ---- */

OSErr NewScreenBuffer(const Rect *bounds, Boolean purgeable, GDHandle *gd,
                      PixMapHandle *out)
{
	(void)purgeable;
	if (gd)
		*gd = NULL;
	SurfObj *o = calloc(1, sizeof *o);
	o->s = SDL_CreateSurface(bounds->right - bounds->left,
	                         bounds->bottom - bounds->top,
	                         SDL_PIXELFORMAT_ARGB8888);
	SurfObj **h = calloc(1, sizeof *h);
	*h = o;
	*out = h;
	return noErr;
}

void CopyBits(const void *srcBits, void *dstBits, const Rect *srcRect,
              const Rect *dstRect, short mode, void *maskRgn)
{
	(void)mode; (void)maskRgn;
	const SurfObj *src = srcBits;
	SurfObj *dst = dstBits;
	SDL_Rect sr = ClipRect(srcRect), dr = ClipRect(dstRect);
	SDL_SetSurfaceBlendMode(src->s, SDL_BLENDMODE_NONE);
	if (sr.w == dr.w && sr.h == dr.h)
		SDL_BlitSurface(src->s, &sr, dst->s, &dr);
	else
		SDL_BlitSurfaceScaled(src->s, &sr, dst->s, &dr, SDL_SCALEMODE_LINEAR);
}

/* ---- keyboard ---- */

typedef struct { SDL_Scancode sc; unsigned char mac; } KeyPair;

static const KeyPair kKeyTable[] = {
	{ SDL_SCANCODE_A, 0 },  { SDL_SCANCODE_S, 1 },  { SDL_SCANCODE_D, 2 },
	{ SDL_SCANCODE_F, 3 },  { SDL_SCANCODE_H, 4 },  { SDL_SCANCODE_G, 5 },
	{ SDL_SCANCODE_Z, 6 },  { SDL_SCANCODE_X, 7 },  { SDL_SCANCODE_C, 8 },
	{ SDL_SCANCODE_V, 9 },  { SDL_SCANCODE_B, 11 }, { SDL_SCANCODE_Q, 12 },
	{ SDL_SCANCODE_W, 13 }, { SDL_SCANCODE_E, 14 }, { SDL_SCANCODE_R, 15 },
	{ SDL_SCANCODE_Y, 16 }, { SDL_SCANCODE_T, 17 }, { SDL_SCANCODE_1, 18 },
	{ SDL_SCANCODE_2, 19 }, { SDL_SCANCODE_3, 20 }, { SDL_SCANCODE_4, 21 },
	{ SDL_SCANCODE_6, 22 }, { SDL_SCANCODE_5, 23 }, { SDL_SCANCODE_9, 25 },
	{ SDL_SCANCODE_7, 26 }, { SDL_SCANCODE_8, 28 }, { SDL_SCANCODE_0, 29 },
	{ SDL_SCANCODE_O, 31 }, { SDL_SCANCODE_U, 32 }, { SDL_SCANCODE_I, 34 },
	{ SDL_SCANCODE_P, 35 }, { SDL_SCANCODE_RETURN, 36 },
	{ SDL_SCANCODE_L, 37 }, { SDL_SCANCODE_J, 38 }, { SDL_SCANCODE_K, 40 },
	{ SDL_SCANCODE_N, 45 }, { SDL_SCANCODE_M, 46 },
	{ SDL_SCANCODE_TAB, 48 }, { SDL_SCANCODE_SPACE, 49 },
	{ SDL_SCANCODE_ESCAPE, 53 },
	{ SDL_SCANCODE_LGUI, 55 },  { SDL_SCANCODE_RGUI, 55 },
	{ SDL_SCANCODE_LSHIFT, 56 },
	{ SDL_SCANCODE_LALT, 58 },
	{ SDL_SCANCODE_LCTRL, 59 },
	{ SDL_SCANCODE_RSHIFT, 60 },
	{ SDL_SCANCODE_RALT, 61 },
	{ SDL_SCANCODE_RCTRL, 62 },
	{ SDL_SCANCODE_LEFT, 123 }, { SDL_SCANCODE_RIGHT, 124 },
	{ SDL_SCANCODE_DOWN, 125 }, { SDL_SCANCODE_UP, 126 },
};

void GetKeys(KeyMap keys)
{
	const bool *state = SDL_GetKeyboardState(NULL);
	memset(keys, 0, 16);
	for (size_t i = 0; i < SDL_arraysize(kKeyTable); i++) {
		if (state[kKeyTable[i].sc]) {
			unsigned char k = kKeyTable[i].mac;
			keys[k >> 3] |= (unsigned char)(1u << (k & 7));
		}
	}
}

Boolean BitTst(const void *bytePtr, long bitNum)
{
	const unsigned char *b = bytePtr;
	return (b[bitNum >> 3] & (1u << (7 - (bitNum & 7)))) != 0;
}

long BitAnd(long a, long b) { return a & b; }

/* Maps an SDL scancode to the game's KeyMap bit number (mac keycode ^ 7),
   for the key-rebinding UI. Returns -1 for keys with no mac equivalent. */
int SATScancodeToKeyMapBit(SDL_Scancode sc)
{
	for (size_t i = 0; i < SDL_arraysize(kKeyTable); i++)
		if (kKeyTable[i].sc == sc)
			return kKeyTable[i].mac ^ 7;
	return -1;
}

/* ---- mouse ---- */

void GetMouse(Point *p)
{
	float wx, wy, lx, ly;
	SDL_GetMouseState(&wx, &wy);
	SDL_RenderCoordinatesFromWindow(g_renderer, wx, wy, &lx, &ly);
	p->h = (short)lx;
	p->v = (short)ly;
}

Boolean Button(void)
{
	float x, y;
	return (SDL_GetMouseState(&x, &y) & SDL_BUTTON_LMASK) != 0;
}

/* ---- misc UI ---- */

short Alert(short id, void *filter)
{
	(void)filter;
	char msg[64];
	snprintf(msg, sizeof msg, "Ascent alert %d", id);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ascent", msg, g_window);
	return 1;
}

void ExitToShell(void) { exit(1); }
void HideCursor(void) { SDL_HideCursor(); }
void ShowCursor(void) { SDL_ShowCursor(); }
void FlushEvents(short mask, short stop)
{
	(void)mask; (void)stop;
	SATPumpEvents();
}

/* Renderer access for screens drawn with SDL debug text (settings, about) */
SDL_Renderer *SATGetRenderer(void) { return g_renderer; }
SDL_Window *SATGetWindow(void) { return g_window; }
SDL_Texture *SATGetFrameTexture(void) { return g_frameTex; }
