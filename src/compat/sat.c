/* SAT sprite engine and QuickDraw subset on SDL3.
   Replaces SAT(PPC).lib and the Toolbox for the Ascent port.

   Rendering model: the original SAT did dirty-rect updates between three
   buffers (backScreen -> offScreen -> window). Here backScreen is a plain
   surface, and every frame the whole scene is recomposed into the screen
   surface and pushed to a texture. The play area (the game's coordinate
   space) defaults to the largest preset that fits the display and can be
   changed at runtime via SATSetPlayAreaSize.

   Game coordinates are not screen pixels. The game draws in a space of
   gSAT.offSizeH x gSAT.offSizeV; zoom decides how many screen pixels one
   game unit is worth. Every coordinate crossing into this file is mapped
   through DP(), the composition surfaces are the full window size, and
   sprite art is resampled once when it loads, so the finished frame reaches
   the window untouched rather than being magnified after the fact. */

#include "../mySAT.h"
#include <SDL3_image/SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_GAME_W 800 /* the 2002 layout; HUD/menu art assumes at least this */
#define MIN_GAME_H 600

static int g_gameW = MIN_GAME_W, g_gameH = MIN_GAME_H;
static Boolean g_sizeRequested; /* a size was set before the window existed */

/* Sprites are drawn at the fixed pixel sizes of the 2002 art, so on a big
   play area they look tiny. Rather than rescale the art and every hard-coded
   rectangle in the game code, the game keeps a smaller coordinate space and
   every game unit is worth more than one screen pixel: at 130 the whole
   image, sprites included, is 30% bigger and the arena holds proportionally
   less space. */
static int g_zoom = 130; /* percent */

/* Game coordinate -> device pixel. Rounds symmetrically so that a rectangle
   straddling the origin keeps its size. */
static int DP(int v)
{
	return v >= 0 ? (v * g_zoom + 50) / 100 : -(((-v) * g_zoom + 50) / 100);
}

static SDL_Rect DevRect(const Rect *r)
{
	int l = DP(r->left), t = DP(r->top);
	SDL_Rect out = { l, t, DP(r->right) - l, DP(r->bottom) - t };
	return out;
}

float SATDeviceScale(void) { return g_zoom / 100.0f; }

static int ZoomedLogical(int windowPx, int minPx)
{
	int v = windowPx * 100 / g_zoom;
	return v < minPx ? minPx : v;
}

/* Zooming shrinks the arena, so it can only go as far as the 800x600 the
   2002 HUD and menu layout need. A small window therefore allows less zoom. */
static void ClampZoom(void)
{
	int byW = g_gameW * 100 / MIN_GAME_W;
	int byH = g_gameH * 100 / MIN_GAME_H;
	int max = byW < byH ? byW : byH;

	if (g_zoom > max)
		g_zoom = max;
	if (g_zoom < 100)
		g_zoom = 100;
}

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

/* Resamples with an area (box) filter over premultiplied alpha. Enlarging
   pixel art this way keeps hard edges hard — a destination pixel that falls
   inside one source pixel copies it exactly, and only the ones straddling a
   boundary blend — while shrinking it averages properly instead of dropping
   rows. Bilinear would soften every pixel; nearest would double some and
   not others at a fractional scale. */
static SDL_Surface *ResampleArea(SDL_Surface *src, int dw, int dh)
{
	SDL_Surface *dst = SDL_CreateSurface(dw, dh, SDL_PIXELFORMAT_ARGB8888);
	const Uint32 *sp = src->pixels;
	Uint32 *dp = dst->pixels;
	int spitch = src->pitch / 4, dpitch = dst->pitch / 4;
	double xr = (double)src->w / dw, yr = (double)src->h / dh;

	for (int y = 0; y < dh; y++) {
		double y0 = y * yr, y1 = y0 + yr;
		int iy1 = (int)ceil(y1);
		for (int x = 0; x < dw; x++) {
			double x0 = x * xr, x1 = x0 + xr;
			int ix1 = (int)ceil(x1);
			double area = 0, wa = 0, wr = 0, wg = 0, wb = 0;
			for (int sy = (int)y0; sy < iy1 && sy < src->h; sy++) {
				double cy = SDL_min(sy + 1, y1) - SDL_max((double)sy, y0);
				for (int sx = (int)x0; sx < ix1 && sx < src->w; sx++) {
					double w = cy * (SDL_min(sx + 1, x1) -
					                 SDL_max((double)sx, x0));
					Uint32 p = sp[sy * spitch + sx];
					double a = (p >> 24) & 0xff;
					area += w;
					wa += w * a;
					wr += w * a * ((p >> 16) & 0xff);
					wg += w * a * ((p >> 8) & 0xff);
					wb += w * a * (p & 0xff);
				}
			}
			Uint32 A = area > 0 ? (Uint32)(wa / area + 0.5) : 0;
			Uint32 R = wa > 0 ? (Uint32)(wr / wa + 0.5) : 0;
			Uint32 G = wa > 0 ? (Uint32)(wg / wa + 0.5) : 0;
			Uint32 B = wa > 0 ? (Uint32)(wb / wa + 0.5) : 0;
			if (A > 255) A = 255;
			if (R > 255) R = 255;
			if (G > 255) G = 255;
			if (B > 255) B = 255;
			dp[y * dpitch + x] = (A << 24) | (R << 16) | (G << 8) | B;
		}
	}
	return dst;
}

/* ---- window and presentation ---- */

static void ClampSizeToDisplay(int *w, int *h)
{
	SDL_Rect b;
	if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &b)) {
		if (*w > b.w)
			*w = b.w;
		if (*h > b.h - 28) /* leave room for the title bar */
			*h = b.h - 28;
	}
	if (*w < MIN_GAME_W)
		*w = MIN_GAME_W;
	if (*h < MIN_GAME_H)
		*h = MIN_GAME_H;
}

static void PickDefaultSize(void)
{
	/* largest preset that fits the display with some breathing room */
	static const int presets[][2] = {
		{ 1600, 1000 }, { 1440, 900 }, { 1280, 800 },
		{ 1152, 720 },  { 1024, 768 }, { 800, 600 },
	};
	SDL_Rect b;
	if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &b))
		return;
	for (size_t i = 0; i < SDL_arraysize(presets); i++) {
		if (presets[i][0] <= b.w - 32 && presets[i][1] <= b.h - 64) {
			g_gameW = presets[i][0];
			g_gameH = presets[i][1];
			return;
		}
	}
}

static void RescaleFaces(void);

static void SetupPlayAreaBuffers(void)
{
	int lw, lh, dw, dh;

	ClampZoom();
	lw = ZoomedLogical(g_gameW, MIN_GAME_W);
	lh = ZoomedLogical(g_gameH, MIN_GAME_H);
	dw = DP(lw);
	dh = DP(lh);

	if (g_frameTex)
		SDL_DestroyTexture(g_frameTex);
	if (g_screen)
		SDL_DestroySurface(g_screen);
	if (g_back)
		SDL_DestroySurface(g_back);
	g_frameTex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
	                               SDL_TEXTUREACCESS_STREAMING, dw, dh);
	g_screen = SDL_CreateSurface(dw, dh, SDL_PIXELFORMAT_ARGB8888);
	g_back = SDL_CreateSurface(dw, dh, SDL_PIXELFORMAT_ARGB8888);
	SDL_FillSurfaceRect(g_back, NULL, 0xff000000);
	SDL_FillSurfaceRect(g_screen, NULL, 0xff000000);
	g_screenPort.portBits.s = g_screen;
	SetRect(&g_screenPort.portRect, 0, 0, lw, lh);
	g_backPort.portBits.s = g_back;
	SetRect(&g_backPort.portRect, 0, 0, lw, lh);

	gSAT.wind.port = &g_screenPort;
	SetRect(&gSAT.wind.bounds, 0, 0, lw, lh);
	gSAT.offScreen.port = &g_screenPort;
	gSAT.offScreen.bounds = gSAT.wind.bounds;
	gSAT.backScreen.port = &g_backPort;
	gSAT.backScreen.bounds = gSAT.wind.bounds;
	gSAT.offSizeH = (short)lw;
	gSAT.offSizeV = (short)lh;

	/* The frame is already window-sized, so this is 1:1 unless the user
	   resizes the window, when it letterboxes instead of distorting. */
	SDL_SetRenderLogicalPresentation(g_renderer, dw, dh,
	                                 SDL_LOGICAL_PRESENTATION_LETTERBOX);
	RescaleFaces();
}

void SATInitToolbox(void)
{
	if (g_window)
		return;
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		exit(1);
	}
	if (g_sizeRequested)
		ClampSizeToDisplay(&g_gameW, &g_gameH);
	else
		PickDefaultSize();
	if (!SDL_CreateWindowAndRenderer("Ascent", g_gameW, g_gameH,
	                                 SDL_WINDOW_RESIZABLE, &g_window,
	                                 &g_renderer)) {
		fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
		exit(1);
	}
	SDL_SetRenderVSync(g_renderer, 1);
	SetupPlayAreaBuffers();

	FindAssetRoot();
	SetPort(&g_screenPort);
}

/* Change the play area. Before the window exists this just records the wish;
   afterwards it rebuilds the composition buffers and resizes the window.
   Call it between games only (sprites hold positions in the old bounds). */
void SATSetPlayAreaSize(int w, int h)
{
	if (!g_window) {
		if (w < MIN_GAME_W)
			w = MIN_GAME_W;
		if (h < MIN_GAME_H)
			h = MIN_GAME_H;
		g_gameW = w;
		g_gameH = h;
		g_sizeRequested = true;
		return;
	}
	ClampSizeToDisplay(&w, &h);
	if (w == g_gameW && h == g_gameH)
		return;
	g_gameW = w;
	g_gameH = h;
	SetupPlayAreaBuffers();
	if (!(SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN)) {
		SDL_SetWindowSize(g_window, w, h);
		SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED,
		                      SDL_WINDOWPOS_CENTERED);
	}
}

/* Magnification of the whole scene, in percent. Safe between games only,
   for the same reason as SATSetPlayAreaSize. */
void SATSetZoom(int percent)
{
	if (percent < 100)
		percent = 100;
	if (percent > 400)
		percent = 400;
	if (percent == g_zoom)
		return;
	g_zoom = percent;
	/* before the window exists the real size is unknown; the setup path
	   clamps against it later */
	if (g_window)
		SetupPlayAreaBuffers();
}

int SATGetZoom(void) { return g_zoom; }

/* The window size the user chose. Distinct from gSAT.offSize*, which is the
   smaller area the scene is composed into when zoomed. */
void SATGetPlayAreaSize(int *w, int *h)
{
	*w = g_gameW;
	*h = g_gameH;
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
	if (SDL_getenv("ASCENT_RENDERSHOT")) /* the magnified image, as displayed */
		SATDumpRendererShot("frame");
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
	SetRect(&full, 0, 0, gSAT.offSizeH, gSAT.offSizeV);
	GrafPtr save = GetCurrentPort();
	SetPort(&g_backPort);
	SATDrawBackground(bg, &full);
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
		SDL_Rect dst = { DP(s->position.h), DP(s->position.v), 0, 0 };
		SDL_BlitSurface(s->face->drawn, NULL, g_screen, &dst);
	}

	if (g_overlaySurf) {
		SDL_Rect dst = { DP(g_overlayRect.left), DP(g_overlayRect.top), 0, 0 };
		SDL_BlitSurface(g_overlaySurf, NULL, g_screen, &dst);
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
	SDL_Rect d = DevRect(dst);
	if (d.w < 1)
		d.w = 1;
	if (d.h < 1)
		d.h = 1;
	/* sized once here, so the per-frame blit is a straight copy */
	g_overlaySurf = ResampleArea(p->s, d.w, d.h);
	SDL_SetSurfaceBlendMode(g_overlaySurf, SDL_BLENDMODE_BLEND);
	DisposeHandle((Handle)p);
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

static void SetFaceDrawn(FacePtr f)
{
	int dw = DP(f->width), dh = DP(f->height);

	if (f->drawn && f->drawn != f->surf)
		SDL_DestroySurface(f->drawn);
	if (dw < 1)
		dw = 1;
	if (dh < 1)
		dh = 1;
	f->drawn = (dw == f->surf->w && dh == f->surf->h)
	               ? f->surf
	               : ResampleArea(f->surf, dw, dh);
	SDL_SetSurfaceBlendMode(f->drawn, SDL_BLENDMODE_BLEND);
}

static void RescaleFaces(void)
{
	for (FacePtr f = g_faceCache; f; f = f->next)
		SetFaceDrawn(f);
}

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
	SetFaceDrawn(f);
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

/* One game pixel, which covers a block of device pixels. Stepping a line
   this way keeps neighbouring one-pixel lines touching — the limit beams are
   five of them side by side — where rounding each device coordinate on its
   own would leave gaps. */
static void PutPixel(SDL_Surface *s, int x, int y, Uint32 c)
{
	int l = DP(x), t = DP(y);
	SDL_Rect d = { l, t, DP(x + 1) - l, DP(y + 1) - t };
	SDL_FillSurfaceRect(s, &d, c);
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

void PaintRect(const Rect *r)
{
	SDL_Rect d = DevRect(r);
	SDL_FillSurfaceRect(g_curPort->portBits.s, &d, g_foreColor);
}

void EraseRect(const Rect *r)
{
	SDL_Rect d = DevRect(r);
	SDL_FillSurfaceRect(g_curPort->portBits.s, &d, g_backColor);
}

void FrameRect(const Rect *r)
{
	SDL_Surface *s = g_curPort->portBits.s;
	SDL_Rect b = DevRect(r);
	int t = DP(1); /* the border is one game pixel wide */
	if (t < 1)
		t = 1;
	SDL_Rect edges[4] = {
		{ b.x, b.y, b.w, t },
		{ b.x, b.y + b.h - t, b.w, t },
		{ b.x, b.y, t, b.h },
		{ b.x + b.w - t, b.y, t, b.h },
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
	SDL_Rect d = DevRect(dst);
	SDL_SetSurfaceBlendMode(pic->s, SDL_BLENDMODE_BLEND);
	SDL_BlitSurfaceScaled(pic->s, NULL, g_curPort->portBits.s, &d,
	                      SDL_SCALEMODE_LINEAR);
}

/* Draws a picture and continues its top row upward to the top of the port.
   The menu art's two pipes run off its top edge and met the screen edge at
   the 2002 size; its topmost rows are pure vertical pipe, so repeating that
   row reconnects them at any play-area height. */
void SATDrawPictureExtendedToTop(PicHandle pic, const Rect *dst)
{
	SDL_Rect src = { 0, 0, pic->s->w, 1 };
	SDL_Rect box = DevRect(dst);
	SDL_SetSurfaceBlendMode(pic->s, SDL_BLENDMODE_BLEND);
	for (int y = 0; y < box.y; y++) {
		SDL_Rect d = { box.x, y, box.w, 1 };
		SDL_BlitSurfaceScaled(pic->s, &src, g_curPort->portBits.s, &d,
		                      SDL_SCALEMODE_LINEAR);
	}
	DrawPicture(pic, dst);
}

/* Fills an area with the starfield background. At the original size the
   picture is used untouched. On a larger play area, tiling shows seams and
   stretching blurs the stars, so: the picture is scaled uniformly to cover
   (the nebula scales well), slightly dimmed, and single-pixel stars are
   re-scattered on top at the original density — from a fixed-seed local
   generator, so the field is stable between redraws and doesn't disturb
   the game's rand() sequence. */
void SATDrawBackground(PicHandle pic, const Rect *area)
{
	SDL_Surface *dst = g_curPort->portBits.s;
	SDL_Rect a = DevRect(area);
	int aw = a.w, ah = a.h;
	int sw = pic->s->w, sh = pic->s->h;

	SDL_SetSurfaceBlendMode(pic->s, SDL_BLENDMODE_NONE);
	if (aw <= sw && ah <= sh) {
		SDL_Rect d = { a.x, a.y, sw, sh };
		SDL_BlitSurface(pic->s, NULL, dst, &d);
		return;
	}

	/* uniform scale to cover, center crop */
	double s = (double)aw / sw;
	if ((double)ah / sh > s)
		s = (double)ah / sh;
	int bw = (int)(sw * s + 0.5), bh = (int)(sh * s + 0.5);
	SDL_Rect d = { a.x - (bw - aw) / 2, a.y - (bh - ah) / 2, bw, bh };
	SDL_SetSurfaceColorMod(pic->s, 230, 230, 230);
	SDL_BlitSurfaceScaled(pic->s, NULL, dst, &d, SDL_SCALEMODE_LINEAR);
	SDL_SetSurfaceColorMod(pic->s, 255, 255, 255);

	/* Star count comes from the area in game units, not device pixels, so
	   the field keeps the same density on screen whatever the zoom. Each
	   star stays a single device pixel — that is the point of drawing them
	   here rather than letting the picture's own stars be scaled. */
	static const Uint8 mags[6] = { 90, 120, 150, 190, 230, 255 };
	long stars = (long)(420.0 * (area->right - area->left) *
	                    (area->bottom - area->top) / (800.0 * 600.0) + 0.5);
	Uint32 rng = 0x20020901u;
	Uint32 *px = dst->pixels;
	int pitch = dst->pitch / 4;
	for (long i = 0; i < stars; i++) {
		rng = rng * 1664525u + 1013904223u;
		int x = a.x + (int)((rng >> 8) % (Uint32)aw);
		rng = rng * 1664525u + 1013904223u;
		int y = a.y + (int)((rng >> 8) % (Uint32)ah);
		rng = rng * 1664525u + 1013904223u;
		Uint32 v = mags[(rng >> 8) % 6];
		if (x < 0 || y < 0 || x >= dst->w || y >= dst->h)
			continue;
		Uint32 old = px[y * pitch + x];
		Uint32 r = (old >> 16) & 0xff, g = (old >> 8) & 0xff, b = old & 0xff;
		Uint32 vb = v + 20 > 255 ? 255 : v + 20;
		if (r < v) r = v;
		if (g < v) g = v;
		if (b < vb) b = vb;
		px[y * pitch + x] = 0xff000000u | (r << 16) | (g << 8) | b;
	}
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
	SDL_Rect b = DevRect(bounds);
	SurfObj *o = calloc(1, sizeof *o);
	o->s = SDL_CreateSurface(b.w, b.h, SDL_PIXELFORMAT_ARGB8888);
	SurfObj **h = calloc(1, sizeof *h);
	*h = o;
	*out = h;
	return noErr;
}

void DisposeScreenBuffer(PixMapHandle h)
{
	if (!h)
		return;
	if (*h) {
		SDL_DestroySurface((*h)->s);
		free(*h);
	}
	free(h);
}

void CopyBits(const void *srcBits, void *dstBits, const Rect *srcRect,
              const Rect *dstRect, short mode, void *maskRgn)
{
	(void)mode; (void)maskRgn;
	const SurfObj *src = srcBits;
	SurfObj *dst = dstBits;
	SDL_Rect sr = DevRect(srcRect), dr = DevRect(dstRect);
	/* Equal in game units means a straight copy, whatever rounding did to
	   the two rectangles' device edges. The limits strip is saved and
	   restored this way and must not soften on the round trip. */
	if (srcRect->right - srcRect->left == dstRect->right - dstRect->left &&
	    srcRect->bottom - srcRect->top == dstRect->bottom - dstRect->top) {
		dr.w = sr.w;
		dr.h = sr.h;
	}
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
	{ SDL_SCANCODE_COMMA, 43 }, { SDL_SCANCODE_SLASH, 44 },
	{ SDL_SCANCODE_PERIOD, 47 },
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
	p->h = (short)(lx / SATDeviceScale());
	p->v = (short)(ly / SATDeviceScale());
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
