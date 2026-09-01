/* Minimal Classic Mac Toolbox types and calls, backed by SDL3.
   Covers exactly what the Ascent game sources use. */

#ifndef MAC_TYPES_H
#define MAC_TYPES_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#define pascal
#ifndef nil
#define nil 0L
#endif

typedef bool Boolean;
typedef unsigned char Str255[256];
typedef long OSErr;
typedef void *Handle;
typedef void *GDHandle;
typedef void *ProcPtr;
typedef void *RgnHandle;
typedef void *CTabHandle;
typedef void *WindowPtr_fwd;

enum { noErr = 0 };

typedef struct Point {
	short v;
	short h;
} Point;

typedef struct Rect {
	short top, left, bottom, right;
} Rect;

typedef struct RGBColor {
	unsigned short red, green, blue;
} RGBColor;

/* QuickDraw color constants for ForeColor/BackColor */
enum {
	blackColor = 33, whiteColor = 30, redColor = 205,
	greenColor = 341, blueColor = 409
};

enum { srcCopy = 0 };

/* A drawable pixel buffer. GrafPort/BitMap/PixMap all collapse to this. */
typedef struct SurfObj {
	SDL_Surface *s;
} SurfObj;

typedef struct GrafPort {
	SurfObj portBits;
	Rect portRect;
} GrafPort, *GrafPtr, *WindowPtr, *DialogPtr;

typedef SurfObj **PixMapHandle;
typedef struct BitMap BitMap; /* only ever appears as (BitMap*) casts */

typedef struct MacPict {
	SDL_Surface *s;
} MacPict, *PicHandle;

typedef struct PenState {
	Point pnLoc;
} PenState;

typedef unsigned char KeyMap[16];

/* Rect / Point */
void SetRect(Rect *r, short left, short top, short right, short bottom);
void OffsetRect(Rect *r, short dh, short dv);
void InsetRect(Rect *r, short dh, short dv);
Boolean PtInRect(Point pt, const Rect *r);

/* Keyboard: classic KeyMap semantics (bit index = mac keycode ^ 7) */
void GetKeys(KeyMap keys);
Boolean BitTst(const void *bytePtr, long bitNum);
long BitAnd(long a, long b);

/* Time: 60ths of a second since start */
long TickCount(void);

/* Pen and color state (one global port cursor, like classic QD usage here) */
void SetPort(GrafPtr port);
GrafPtr GetCurrentPort(void);
void MoveTo(short h, short v);
void LineTo(short h, short v);
void RGBForeColor(const RGBColor *c);
void ForeColor(long qdColor);
void BackColor(long qdColor);
void PaintRect(const Rect *r);
void FrameRect(const Rect *r);
void EraseRect(const Rect *r);
void GetPenState(PenState *p);
void SetPenState(const PenState *p);
void TextSize(short size);
void TextFont(short font);
void DrawString(const char *s);

/* Pictures: id -> assets/pics/<id>.png. Never returns NULL (placeholder
   surface on missing file, so the original nil-check error paths stay dead). */
PicHandle GetPicture(short id);
void DrawPicture(PicHandle pic, const Rect *dst);
void DisposeHandle(Handle h);

/* Pixel copies between buffers (replaces CopyBits) */
void CopyBits(const void *srcBits, void *dstBits, const Rect *srcRect,
              const Rect *dstRect, short mode, void *maskRgn);
OSErr NewScreenBuffer(const Rect *bounds, Boolean purgeable, GDHandle *gd,
                      PixMapHandle *out);

/* Mouse (in 800x600 logical coordinates) */
void GetMouse(Point *p);
Boolean Button(void);

/* Misc UI */
short Alert(short id, void *filter);
void ExitToShell(void);
void HideCursor(void);
void ShowCursor(void);
void FlushEvents(short mask, short stop);
enum { everyEvent = -1 };

#endif
