/* SAT (Sprite Animation Toolkit) API subset reimplemented on SDL3.
   Same names and semantics as Ingemar Ragnemalm's SAT 2.5 as used by Ascent;
   the original mySAT.h is preserved at the repository root. */

#ifndef __SAT__
#define __SAT__

#include "compat/mac_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SATPort {
	GrafPtr port;
	Rect bounds;
} SATPort;

typedef struct SATSprite *SpritePtr;

typedef struct Face {
	SDL_Surface *surf;
	short resNum;
	short width, height;
	struct Face *next;
} Face, *FacePtr;

typedef pascal void (*TaskPtr)(SpritePtr);
typedef pascal void (*HitTaskPtr)(SpritePtr, SpritePtr);

typedef struct Vector {
	double h, v;
} Vector;

typedef struct SATSprite {
	short kind;
	Point position;
	Rect hotRect, hotRect2;
	FacePtr face;
	TaskPtr task;
	HitTaskPtr hitTask;
	struct SATSprite *next, *prev;
	FacePtr oldFace;
	Boolean dirty;
	short layer;
	/* Application fields (Ascent's physics model) */
	Vector speed;
	Vector force;
	Vector pos;
	int mass;
	double viscosity;
	Boolean direction;
	short shields;
	short powerup;
	SpritePtr who;
	int counter;
	short mode;
	char *appPtr;
	long appLong;
} SATSprite;

typedef struct {
	SATPort wind;
	short offSizeH, offSizeV;
	SATPort offScreen, backScreen;
	SpritePtr sRoot;
	Boolean anyMonsters;
} SATglobalsRec;

enum { kVPositionSort = 0, kLayerSort, kNoSort };
enum { kKindCollision = 0, kForwardCollision, kBackwardCollision,
       kNoCollision, kForwardOneCollision };

extern SATglobalsRec gSAT;

/* Initialization */
void SATConfigure(Boolean PICTfit, short newSorting, short newCollision,
                  short searchWidth);
void SATCustomInit(short pictID, short bwpictID, Rect *SATdrawingArea,
                   WindowPtr preloadedWind, GDHandle gd, Boolean useMenuBar,
                   Boolean centerDrawingArea, Boolean fillScreen,
                   Boolean dither4bit, Boolean beSmart);
void SATInitToolbox(void);
void SATSetSpriteRecSize(long theSize);

/* Screen */
Boolean SATDepthChangeTest(void);
void SATRedraw(void);
void SATBackChanged(Rect *r);
void SATGetPort(SATPort *port);
void SATSetPort(SATPort *port);
void SATSetPortBackScreen(void);
void SATSetPortOffScreen(void);
void SATSetPortScreen(void);
void SATHideMBar(WindowPtr wind);
void SATShowMBar(WindowPtr wind);

/* Sprites */
FacePtr SATGetFace(short resNum);
SpritePtr SATNewSprite(short kind, short hpos, short vpos, TaskPtr setup);
void SATKillSprite(SpritePtr who);
void SATRun(Boolean fast);
void SATRun2(Boolean fast);

/* Utilities */
short SATRand(short n);
void SATDrawInt(short i);

/* Sound */
void SATSoundPlay(Handle theSound, short priority, Boolean canWait);
void SATSoundShutup(void);
void SATSoundEvents(void);
Boolean SATSoundDone(void);
Handle SATGetNamedSound(const char *name);
void SATSoundOn(void);
void SATSoundOff(void);
short SATSoundInitChannels(short num);
void SATPreloadChannels(void);

/* Port additions (not in original SAT): frame presentation and overlays */
void SATPresent(void);                       /* push composite to the window */
void SATOverlayPic(short picID, Rect *dst);  /* picture drawn above sprites */
void SATOverlayClear(void);
void SATSetGammaLevel(int percent);          /* 0 = black, 100 = full */
Boolean SATPumpEvents(void);                 /* returns false on quit request */
void SATSetPlayAreaSize(int w, int h);       /* clamped; safe between games */
void SATTilePicture(PicHandle pic, const Rect *area);
extern Boolean gSATQuitRequested;

#ifdef __cplusplus
}
#endif

#endif
