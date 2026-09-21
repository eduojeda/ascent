/* Ascent — SDL3 port of the 2002 original.
   This file replaces the Classic Mac event loop, menu screen, dialogs and
   window management from the original main.c (preserved at the repository
   root). The game modules (sBall.c, sLeftShip.c, ...) are the originals. */

#include <math.h>
#include <stdlib.h>
#include "mySAT.h"
#include <SDL3/SDL_main.h> /* supplies WinMain on Windows */
#include "gamma.h"
#include "ascent.h"

/* engine extras not in the SAT API */
extern SDL_Renderer *SATGetRenderer(void);
extern int SATScancodeToKeyMapBit(SDL_Scancode sc);
extern const char *SATAssetPathPublic(const char *rel);

/*This Module's Globals*/

Boolean         gFastGraphics = true, gSoundOn = true, gDone = false,
                gGamePaused = false, gCanUseGamma = true;
GDHandle        offScreenGD;
PixMapHandle    limitsOffScreenBuffer = nil, theOffScreenBuffer = nil;
Rect            bufferRect, menuNewGameRect, menuSettingsRect,
                menuAboutRect, menuQuitRect, menuRect;
PicHandle       newGameLitPic, settingsLitPic, aboutLitPic, quitLitPic,
                backgroundPic, menuPic;

/*Main Global Structs Declarations*/

Globals   g;
Controls  LSKeys, RSKeys;

static void Initialize(void);
static void MainEventLoop(void);
static void CleanUp(void);
static void Play(void);
static void InitNewGame(void);
static void DrawMenuWindow(void);
static void LayoutMenu(void);
static void CreateLimitsBuffer(void);
static void SavePrefs(void);
static void DisplaySettingsScreen(void);
static void DisplayAboutScreen(void);
static void SetKeys(Controls *keys, const char *playerName);

/*Code*/

int main(int argc, char *argv[])
{
	(void)argc; (void)argv;
	Initialize();
	MainEventLoop();
	CleanUp();
	return 0;
}

/* ---- text helper: SDL debug text at a scale, in play-area coordinates ----
   The overlay screens were laid out for the original 800x600; VC() recentres
   a "designed for 600 tall" y coordinate on the current play area. */

#define VC(y) ((y) + gSAT.offSizeV / 2 - 300)

/* These screens draw straight to the renderer, whose coordinates are device
   pixels, in the same game coordinates as everything else — so each one goes
   through the same scale the compat layer applies to the composite. */
static SDL_FRect Dev(SDL_FRect r)
{
	float f = SATDeviceScale();
	return (SDL_FRect){ r.x * f, r.y * f, r.w * f, r.h * f };
}

static void DrawTextLine(float x, float y, float scale, SDL_Color c,
                         const char *text)
{
	SDL_Renderer *r = SATGetRenderer();
	float s = scale * SATDeviceScale();
	SDL_SetRenderScale(r, s, s);
	SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
	SDL_RenderDebugText(r, x / scale, y / scale, text);
	SDL_SetRenderScale(r, 1, 1);
}

static float TextWidth(float scale, const char *text)
{
	return (float)SDL_strlen(text) * 8.0f * scale;
}

static void DrawTextCentered(float y, float scale, SDL_Color c,
                             const char *text)
{
	DrawTextLine(gSAT.offSizeH / 2.0f - TextWidth(scale, text) / 2, y, scale,
	             c, text);
}

/* Presents the current composite, then runs `drawText` on top of it and
   presents that instead. Used by pause/settings/about screens. */
static void PresentWithText(void (*drawText)(void))
{
	extern SDL_Texture *SATGetFrameTexture(void);
	SDL_Renderer *r = SATGetRenderer();
	SATPresent(); /* refresh the frame texture from the composite */
	SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
	SDL_RenderClear(r);
	SDL_RenderTexture(r, SATGetFrameTexture(), NULL, NULL);
	drawText();
	extern void SATDumpRendererShot(const char *tag);
	SATDumpRendererShot("dlg");
	SDL_RenderPresent(r);
}

static Boolean WaitForMouseRelease(void)
{
	while (Button()) {
		if (!SATPumpEvents())
			return false;
		SDL_Delay(10);
	}
	return true;
}

/* ---- the game ---- */

static void PauseTextOverlay(void)
{
	SDL_Color green = { 60, 255, 60, 255 };
	SDL_Color grey = { 200, 200, 200, 255 };
	DrawTextCentered(VC(270), 4, green, "PAUSED");
	DrawTextCentered(VC(320), 2, grey, "P: resume    Esc: quit to menu");
}

static void Play(void)
{
	short limitsFrameCounter = 1;
	KeyMap theKeys;
	Boolean pKeyWasUp = true, escKeyWasUp = true;
	Uint64 nextFrame;
	long selfTestTick = 0;

	if (SDL_getenv("ASCENT_SELFTEST")) /* exercise death/winner paths */
		g.numLives = 1;

	InitNewGame();
	nextFrame = SDL_GetTicksNS();

	do {
		if (SDL_getenv("ASCENT_SELFTEST")) {
			selfTestTick++;
			if (selfTestTick == 600 && g.leftShip &&
			    g.leftShip->mode == kShipAliveMode)
				g.leftShip->shields = -1;
		}
		if (!SATPumpEvents()) {
			CleanUp();
			exit(0);
		}

		if (g.gameStarting)
			DisplayCountdown();

		if (limitsFrameCounter == 15) {
			DrawLimits();
			limitsFrameCounter = 1;
		}
		limitsFrameCounter++;

		if (g.leftShipScore == g.numPoints || g.rightShipLives == 0)
			DisplayWinner(g.leftShip, g.rightShip);
		if (g.rightShipScore == g.numPoints || g.leftShipLives == 0)
			DisplayWinner(g.rightShip, g.leftShip);

		if (g.leftShipReincarnating)
			LeftShipReincarnationDelay();
		if (g.rightShipReincarnating)
			RightShipReincarnationDelay();

		SATRun2(gFastGraphics);

		/*Pause Behavior*/
		GetKeys(theKeys);
		if (BitTst(&theKeys, kPKeyMap) && pKeyWasUp) {
			SATSoundPlay(g.pauseSnd, 1, nil);
			gGamePaused = true;
			pKeyWasUp = false;
			ShowCursor();
			while (gGamePaused) {
				if (!SATPumpEvents()) {
					CleanUp();
					exit(0);
				}
				PresentWithText(PauseTextOverlay);
				GetKeys(theKeys);
				if (BitTst(&theKeys, kPKeyMap)) {
					if (pKeyWasUp)
						gGamePaused = false;
				} else {
					pKeyWasUp = true;
				}
				if (BitTst(&theKeys, 53 ^ 7)) { /* Escape */
					g.gameDone = true;
					gGamePaused = false;
				}
				SDL_Delay(10);
			}
			pKeyWasUp = false;
			HideCursor();
			nextFrame = SDL_GetTicksNS();
		} else if (!BitTst(&theKeys, kPKeyMap)) {
			pKeyWasUp = true;
		}

		if (BitTst(&theKeys, 53 ^ 7) && escKeyWasUp) { /* Escape aborts */
			escKeyWasUp = false;
			g.gameDone = true;
		} else if (!BitTst(&theKeys, 53 ^ 7)) {
			escKeyWasUp = true;
		}

		/* pace to 60 frames per second */
		nextFrame += 1000000000ull / 60;
		Uint64 now = SDL_GetTicksNS();
		if (nextFrame > now)
			SDL_DelayNS(nextFrame - now);
		else
			nextFrame = now;
	} while (!g.gameDone);

	if (gCanUseGamma)
		MyFadeToBlack(50);

	/*Post-Game Calls*/
	ShowCursor();
	do {
		SATKillSprite(gSAT.sRoot);
	} while (gSAT.sRoot != nil);

	DrawMenuWindow();

	if (gCanUseGamma)
		MyFadeFromBlack(50);
}

static void MainEventLoop(void)
{
	Point mouseLoc;
	Boolean mouseOverMenuItem = false;
	int hoveredItem = -1;

	if (SDL_getenv("ASCENT_AUTOSTART")) /* testing hook: straight into a game */
		Play();
	if (SDL_getenv("ASCENT_SHOWSETTINGS")) /* testing hook */
		DisplaySettingsScreen();
	if (SDL_getenv("ASCENT_TESTRESIZE")) { /* testing hook: the OK-commit
		                                      resize path, without the dialog */
		int w = 0, h = 0;
		if (SDL_sscanf(SDL_getenv("ASCENT_TESTRESIZE"), "%dx%d", &w, &h) == 2) {
			SATSetPlayAreaSize(w, h);
			CreateLimitsBuffer();
			LayoutMenu();
			SavePrefs();
			DrawMenuWindow();
		}
	}

	do {
		if (!SATPumpEvents())
			break;
		GetMouse(&mouseLoc);

		int item = -1;
		if (SDL_getenv("ASCENT_HOVER")) /* testing hook: fake a hover */
			item = SDL_atoi(SDL_getenv("ASCENT_HOVER"));
		else if (PtInRect(mouseLoc, &menuNewGameRect))
			item = 0;
		else if (PtInRect(mouseLoc, &menuSettingsRect))
			item = 1;
		else if (PtInRect(mouseLoc, &menuAboutRect))
			item = 2;
		else if (PtInRect(mouseLoc, &menuQuitRect))
			item = 3;

		if (item != hoveredItem) {
			DrawMenuWindow();
			if (item >= 0) {
				SATSoundPlay(g.menuLowBassSnd, 1, nil);
				switch (item) {
				case 0: DrawPicture(newGameLitPic, &menuNewGameRect); break;
				case 1: DrawPicture(settingsLitPic, &menuSettingsRect); break;
				case 2: DrawPicture(aboutLitPic, &menuAboutRect); break;
				case 3: DrawPicture(quitLitPic, &menuQuitRect); break;
				}
			}
			hoveredItem = item;
			(void)mouseOverMenuItem;
		}
		SATPresent();

		if (item >= 0 && Button()) {
			SATSoundPlay(g.menuHighBassSnd, 1, nil);
			if (!WaitForMouseRelease())
				break;
			switch (item) {
			case 0:
				Play();
				break;
			case 1:
				DisplaySettingsScreen();
				break;
			case 2:
				DisplayAboutScreen();
				break;
			case 3:
				SATSoundEvents();
				if (gCanUseGamma)
					MyFadeToBlack(50);
				gDone = true;
				break;
			}
			hoveredItem = -2; /* force redraw */
		}
		SDL_Delay(10);
	} while (!gDone && !gSATQuitRequested);
}

static void DrawMenuWindow(void)
{
	Rect backgroundRect;

	SetPort(gSAT.wind.port);
	SetRect(&backgroundRect, 0, 0, gSAT.offSizeH, gSAT.offSizeV);
	SATDrawBackground(backgroundPic, &backgroundRect);
	SATDrawPictureExtendedToTop(menuPic, &menuRect);
}

/* The menu art and its hover rectangles were laid out for 800x600, where the
   sign's center sat 255px down the 600-tall screen and its pipes met the top
   edge. Holding that proportion keeps the sign middle-top at any height; the
   pipes are extended up to the edge when drawn. */
static void LayoutMenu(void)
{
	short cx = gSAT.offSizeH / 2;
	short top = (short)(gSAT.offSizeV * 255 / 600) - 255;

	if (top < 0)
		top = 0;

	SetRect(&menuRect, cx - 247, top, cx + 246, top + 391);
	SetRect(&menuNewGameRect, menuRect.left + 42, top + 164,
	        menuRect.left + 42 + 305, top + 164 + 47);
	SetRect(&menuSettingsRect, menuRect.left + 236, top + 237,
	        menuRect.left + 236 + 257, top + 237 + 60);
	SetRect(&menuAboutRect, menuRect.left + 25, top + 288,
	        menuRect.left + 25 + 172, top + 288 + 46);
	SetRect(&menuQuitRect, menuRect.left + 353, top + 321,
	        menuRect.left + 353 + 139, top + 321 + 67);
}

/* The strip buffer DrawLimits saves background into spans the full play-area
   height, so it is rebuilt whenever the play area changes. */
static void CreateLimitsBuffer(void)
{
	OSErr error;

	if (limitsOffScreenBuffer)
		DisposeScreenBuffer(limitsOffScreenBuffer);
	SetRect(&bufferRect, 0, 0, 20, gSAT.offSizeV);
	error = NewScreenBuffer(&bufferRect, false, &offScreenGD,
	                        &limitsOffScreenBuffer);
	(void)error;
}

/* ---- preferences (persisted across launches) ---- */

static const char *PrefsFilePath(void)
{
	static char path[1200];
	if (!path[0]) {
		char *dir = SDL_GetPrefPath("", "Ascent");
		if (!dir)
			return NULL;
		SDL_snprintf(path, sizeof path, "%sprefs.txt", dir);
		SDL_free(dir);
	}
	return path;
}

static void SavePrefs(void)
{
	const char *path = PrefsFilePath();
	if (!path)
		return;
	SDL_IOStream *f = SDL_IOFromFile(path, "w");
	if (!f)
		return;
	int winW, winH;
	SATGetPlayAreaSize(&winW, &winH);
	char buf[512];
	int n = SDL_snprintf(buf, sizeof buf,
	    "width=%d\nheight=%d\nzoom=%d\nlives=%d\npoints=%d\n"
	    "fast=%d\nsound=%d\nadult=%d\n"
	    "lskeys=%d,%d,%d,%d,%d,%d,%d\nrskeys=%d,%d,%d,%d,%d,%d,%d\n",
	    winW, winH, SATGetZoom(), g.numLives, g.numPoints,
	    gFastGraphics ? 1 : 0, gSoundOn ? 1 : 0, g.dirtyWordsMode ? 1 : 0,
	    LSKeys.up, LSKeys.down, LSKeys.left, LSKeys.right, LSKeys.shoot,
	    LSKeys.special, LSKeys.rotate,
	    RSKeys.up, RSKeys.down, RSKeys.left, RSKeys.right, RSKeys.shoot,
	    RSKeys.special, RSKeys.rotate);
	SDL_WriteIO(f, buf, (size_t)n);
	SDL_CloseIO(f);
}

static int ClampSetting(int v) { return v < 1 ? 1 : (v > 99 ? 99 : v); }

static void LoadPrefs(void)
{
	const char *path = PrefsFilePath();
	if (!path)
		return;
	size_t len = 0;
	char *data = SDL_LoadFile(path, &len);
	if (!data)
		return;
	int w = 0, h = 0, zoom = 0, v;
	Controls ls = LSKeys, rs = RSKeys;
	char *line = data;
	while (line && *line) {
		char *next = SDL_strchr(line, '\n');
		if (next)
			*next++ = 0;
		if (SDL_sscanf(line, "width=%d", &w) == 1 ||
		    SDL_sscanf(line, "height=%d", &h) == 1 ||
		    SDL_sscanf(line, "zoom=%d", &zoom) == 1) {
		} else if (SDL_sscanf(line, "lives=%d", &v) == 1) {
			g.numLives = ClampSetting(v);
		} else if (SDL_sscanf(line, "points=%d", &v) == 1) {
			g.numPoints = ClampSetting(v);
		} else if (SDL_sscanf(line, "fast=%d", &v) == 1) {
			gFastGraphics = v != 0;
		} else if (SDL_sscanf(line, "sound=%d", &v) == 1) {
			gSoundOn = v != 0;
		} else if (SDL_sscanf(line, "adult=%d", &v) == 1) {
			g.dirtyWordsMode = v != 0;
		} else if (SDL_sscanf(line, "lskeys=%d,%d,%d,%d,%d,%d,%d", &ls.up,
		                      &ls.down, &ls.left, &ls.right, &ls.shoot,
		                      &ls.special, &ls.rotate) == 7) {
			LSKeys = ls;
		} else if (SDL_sscanf(line, "rskeys=%d,%d,%d,%d,%d,%d,%d", &rs.up,
		                      &rs.down, &rs.left, &rs.right, &rs.shoot,
		                      &rs.special, &rs.rotate) == 7) {
			RSKeys = rs;
		}
		line = next;
	}
	SDL_free(data);
	if (w && h)
		SATSetPlayAreaSize(w, h);
	if (zoom)
		SATSetZoom(zoom);
}

static void InitNewGame(void)
{
	if (gCanUseGamma)
		MyFadeToBlack(50);

	SATCustomInit(128, 128, nil, nil, nil, true, true, true, false, true);
	SATConfigure(false, kLayerSort, kForwardCollision, 32);
	g.ball = nil; /* SATCustomInit freed last game's; the spawner makes a new one */

	/*Misc Calls*/
	g.leftShipReincarnating = true;
	g.rightShipReincarnating = true;
	g.gameDone = false;
	g.leftShipScore = 0;
	g.rightShipScore = 0;
	g.leftShipLives = g.numLives;
	g.rightShipLives = g.numLives;
	g.gravityAccel.h = kGravityH;
	g.gravityAccel.v = kGravityV;
	g.gameStarting = true;
	gGamePaused = false;
	g.LSReincC = 0;
	g.RSReincC = 0;
	g.countDownC = 0;
	g.winnerC = 0;

	DrawBackground();
	HideCursor();
	DisplayScore();
	DisplayLives();
	SATRedraw();

	if (gCanUseGamma)
		MyFadeFromBlack(50);
}

static void LoadSounds(void)
{
	g.redShotSnd = SATGetNamedSound("RedShot");
	g.greenShotSnd = SATGetNamedSound("GreenShot");
	g.hitLaserSnd = SATGetNamedSound("Hit Laser");
	g.catchSnd = SATGetNamedSound("Catch");
	g.releaseSnd = SATGetNamedSound("Release");
	g.explosionSnd = SATGetNamedSound("Explosion");
	g.bulletHitSnd = SATGetNamedSound("BulletHit");
	g.missileSnd = SATGetNamedSound("Missile");
	g.rocketSnd = SATGetNamedSound("Rocket");
	g.bounceSnd = SATGetNamedSound("Bounce");
	g.noAmmoSnd = SATGetNamedSound("ClickClick...Oh Shit!");
	g.magnetoSnd = SATGetNamedSound("Magneto");
	g.openBaseSnd = SATGetNamedSound("Base Opening");
	g.closeBaseSnd = SATGetNamedSound("Base Closing");
	g.shieldReloadSnd = SATGetNamedSound("ShieldReload");
	g.ballShotSnd = SATGetNamedSound("BallShot");
	g.ballSpawnerOpeningSnd = SATGetNamedSound("BallSpawnerOpening");
	g.ballSpawnerClosingSnd = SATGetNamedSound("BallSpawnerClosing");
	g.menuLowBassSnd = SATGetNamedSound("Low Bass");
	g.menuHighBassSnd = SATGetNamedSound("High Bass");
	g.pauseSnd = SATGetNamedSound("Pause");
	g.goodieBounceSnd = SATGetNamedSound("Goodie Bounce");
	g.goodieCatchSnd = SATGetNamedSound("Goodie Catch");
	g.goodieAppearsSnd = SATGetNamedSound("Goodie Appears");
	g.threeSnd = SATGetNamedSound("Three");
	g.twoSnd = SATGetNamedSound("Two");
	g.oneSnd = SATGetNamedSound("One");
	g.goSnd = SATGetNamedSound("Go");
	g.applauseSnd = SATGetNamedSound("Applause");
	g.aboutSnd = SATGetNamedSound("Barney");
	g.woohooSnd = SATGetNamedSound("WooHoo!");
	g.wowSnd = SATGetNamedSound("Wow!");
	g.joySnd = SATGetNamedSound("Joy!");
	g.notBadSnd = SATGetNamedSound("Not Bad!");
	g.cool1Snd = SATGetNamedSound("Cool1");
	g.cool2Snd = SATGetNamedSound("Cool2");
	g.whoaSnd = SATGetNamedSound("Whoa!");
	g.shoveSnd = SATGetNamedSound("Shove");
	g.smackYouSnd = SATGetNamedSound("Smack You");
	g.notCoolSnd = SATGetNamedSound("Not Cool");
	g.youFuckerSnd = SATGetNamedSound("You Fucker!");
	g.thisSuxSnd = SATGetNamedSound("This Sux");
	g.ohSnd = SATGetNamedSound("Oh");
}

static void LoadFaces(void)
{
	short faceIndex;

	for (faceIndex = 0; faceIndex <= 7; faceIndex++)
		g.leftShipFacingRight[faceIndex] =
		    SATGetFace(kLeftShipFacingRightID + faceIndex);
	for (faceIndex = 0; faceIndex <= 7; faceIndex++)
		g.leftShipFacingLeft[faceIndex] =
		    SATGetFace(kLeftShipFacingLeftID + faceIndex);
	for (faceIndex = 0; faceIndex <= 2; faceIndex++)
		g.leftShipRotating[faceIndex] =
		    SATGetFace(kLeftShipRotatingID + faceIndex);
	for (faceIndex = 0; faceIndex <= 7; faceIndex++)
		g.rightShipFacingRight[faceIndex] =
		    SATGetFace(kRightShipFacingRightID + faceIndex);
	for (faceIndex = 0; faceIndex <= 7; faceIndex++)
		g.rightShipFacingLeft[faceIndex] =
		    SATGetFace(kRightShipFacingLeftID + faceIndex);
	for (faceIndex = 0; faceIndex <= 2; faceIndex++)
		g.rightShipRotating[faceIndex] =
		    SATGetFace(kRightShipRotatingID + faceIndex);
	for (faceIndex = 0; faceIndex <= 7; faceIndex++)
		g.bigExplosionFaces[faceIndex] =
		    SATGetFace(kBigExplosionID + faceIndex);
	for (faceIndex = 0; faceIndex <= 7; faceIndex++)
		g.smallExplosionFaces[faceIndex] =
		    SATGetFace(kSmallExplosionID + faceIndex);
	for (faceIndex = 0; faceIndex <= 9; faceIndex++)
		g.numberFaces[faceIndex] = SATGetFace(kNumbersID + faceIndex);
	for (faceIndex = 0; faceIndex <= 5; faceIndex++)
		g.smokeFaces[faceIndex] = SATGetFace(kSmokesID + faceIndex);
	for (faceIndex = 0; faceIndex <= 5; faceIndex++)
		g.sparkFaces[faceIndex] = SATGetFace(kSparksID + faceIndex);
	for (faceIndex = 0; faceIndex <= 4; faceIndex++)
		g.missileFacingLeft[faceIndex] =
		    SATGetFace(kMissileFacingLeftID + faceIndex);
	for (faceIndex = 0; faceIndex <= 4; faceIndex++)
		g.missileFacingRight[faceIndex] =
		    SATGetFace(kMissileFacingRightID + faceIndex);
	for (faceIndex = 0; faceIndex <= 7; faceIndex++)
		g.blueBaseFaces[faceIndex] = SATGetFace(kBlueBaseID + faceIndex);
	for (faceIndex = 0; faceIndex <= 7; faceIndex++)
		g.redBaseFaces[faceIndex] = SATGetFace(kRedBaseID + faceIndex);
	for (faceIndex = 0; faceIndex <= 5; faceIndex++)
		g.ballSpawnerFaces[faceIndex] =
		    SATGetFace(kBallSpawnerID + faceIndex);
	for (faceIndex = 0; faceIndex <= 4; faceIndex++)
		g.goodieFaces[faceIndex] = SATGetFace(kGoodieID + faceIndex);
	for (faceIndex = 0; faceIndex <= 6; faceIndex++)
		g.bodyDebrisFaces[faceIndex] = SATGetFace(kBodyDebrisID + faceIndex);
	for (faceIndex = 0; faceIndex <= 4; faceIndex++)
		g.engineDebrisFaces[faceIndex] =
		    SATGetFace(kEngineDebrisID + faceIndex);
}

static void Initialize(void)
{
	OSErr error;

	/*DEFAULT SETTINGS (prefs may override)*/
	g.numLives = 10;
	g.numPoints = 5;
	g.gameDone = true;
	g.dirtyWordsMode = true;

	/*SET STANDARD KEYCODES*/
	LSKeys.up = kWKeyMap;
	LSKeys.down = kSKeyMap;
	LSKeys.left = kAKeyMap;
	LSKeys.right = kDKeyMap;
	LSKeys.shoot = kFKeyMap;
	LSKeys.special = kHKeyMap;
	LSKeys.rotate = kGKeyMap;

	RSKeys.up = kUpArrowKeyMap;
	RSKeys.down = kDownArrowKeyMap;
	RSKeys.left = kLeftArrowKeyMap;
	RSKeys.right = kRightArrowKeyMap;
	RSKeys.shoot = kCommaKeyMap;
	RSKeys.special = kSlashKeyMap;
	RSKeys.rotate = kPeriodKeyMap;

	LoadPrefs(); /* may also request a play-area size for SATInitToolbox */

	SATInitToolbox();
	srand((unsigned)SDL_GetTicks() ^ 0x5eed);

	if (gCanUseGamma)
		MyFadeToBlack(50);

	/*CREATE BUFFERS*/
	SetRect(&bufferRect, 0, 0, 300, 200);
	error = NewScreenBuffer(&bufferRect, false, &offScreenGD,
	                        &theOffScreenBuffer);
	(void)error;
	CreateLimitsBuffer();

	/*LOAD SOUNDS*/
	SATSoundInitChannels(6);
	LoadSounds();
	if (!gSoundOn)
		SATSoundOff();

	/*LOAD SPRITE FACES*/
	LoadFaces();

	/*MENU WINDOW*/
	LayoutMenu();
	newGameLitPic = GetPicture(132);
	settingsLitPic = GetPicture(133);
	aboutLitPic = GetPicture(134);
	quitLitPic = GetPicture(135);
	backgroundPic = GetPicture(128);
	menuPic = GetPicture(131);

	DrawMenuWindow();
	if (gCanUseGamma)
		MyFadeFromBlack(50);
}

/* ---- settings screen ----
   A faithful redraw of the original DLOG/DITL 128 ("Settings", 274x172),
   recovered from the resource fork, rendered at 2x. Item rectangles and
   labels are the 2002 ones, except the play-area row, which is a port
   addition. */

#define DLG_SCALE 2
#define DLG_W (274 * DLG_SCALE)
#define DLG_H (172 * DLG_SCALE)
#define DLG_X ((gSAT.offSizeH - DLG_W) / 2)
#define DLG_Y VC(120)

typedef struct DlgItem {
	short l, t, r, b;
	const char *label;
} DlgItem;

enum {
	kItOK, kItCancel, kItFastAnim, kItSound, kItAdult,
	kItLives, kItPoints, kItWidth, kItHeight, kItZoom, kItBlueKeys,
	kItRedKeys, kItCount
};

static const DlgItem kSettingsItems[kItCount] = {
	[kItOK]       = { 200, 110, 258, 130, "OK" },
	[kItCancel]   = { 200, 140, 258, 160, "Cancel" },
	[kItFastAnim] = { 10, 18, 130, 36, "Fast Animation" },
	[kItSound]    = { 10, 38, 76, 56, "Sound" },
	[kItAdult]    = { 10, 58, 116, 76, "Adult Mode" },
	[kItLives]    = { 190, 20, 214, 37, NULL },
	[kItPoints]   = { 190, 40, 214, 57, NULL },
	[kItWidth]    = { 94, 138, 136, 155, NULL },
	[kItHeight]   = { 150, 138, 192, 155, NULL },
	[kItZoom]     = { 94, 114, 136, 131, NULL },
	[kItBlueKeys] = { 10, 80, 136, 99, "Blue Player Keys..." },
	[kItRedKeys]  = { 140, 80, 266, 99, "Red Player Keys..." },
};

static SDL_FRect ItemRect(int i)
{
	const DlgItem *it = &kSettingsItems[i];
	return (SDL_FRect){ DLG_X + it->l * DLG_SCALE, DLG_Y + it->t * DLG_SCALE,
		                (it->r - it->l) * DLG_SCALE,
		                (it->b - it->t) * DLG_SCALE };
}

static void RFill(SDL_FRect r, int c1, int c2, int c3)
{
	SDL_Renderer *rd = SATGetRenderer();
	SDL_FRect d = Dev(r);
	SDL_SetRenderDrawColor(rd, c1, c2, c3, 255);
	SDL_RenderFillRect(rd, &d);
}

static void RFrame(SDL_FRect r, int c1, int c2, int c3)
{
	SDL_Renderer *rd = SATGetRenderer();
	SDL_FRect d = Dev(r);
	SDL_SetRenderDrawColor(rd, c1, c2, c3, 255);
	SDL_RenderRect(rd, &d);
}

static void DrawDialogBox(SDL_FRect box)
{
	SDL_FRect shadow = { box.x + 4, box.y + 4, box.w, box.h };
	RFill(shadow, 0, 0, 40);
	RFill(box, 221, 221, 221);
	RFrame(box, 0, 0, 0);
	SDL_FRect inner = { box.x + 1, box.y + 1, box.w - 2, box.h - 2 };
	RFrame(inner, 255, 255, 255);
}

static void DrawButton(int i, Boolean isDefault)
{
	SDL_FRect r = ItemRect(i);
	SDL_Color black = { 0, 0, 0, 255 };
	RFill(r, 238, 238, 238);
	RFrame(r, 0, 0, 0);
	const char *label = kSettingsItems[i].label;
	float scale = 2;
	while (scale > 1 && TextWidth(scale, label) > r.w - 8)
		scale -= 0.25f;
	DrawTextLine(r.x + r.w / 2 - TextWidth(scale, label) / 2,
	             r.y + r.h / 2 - 4 * scale, scale, black, label);
	if (isDefault) {
		SDL_FRect ring = { r.x - 5, r.y - 5, r.w + 10, r.h + 10 };
		for (int k = 0; k < 3; k++) {
			RFrame(ring, 0, 0, 0);
			ring.x += 1; ring.y += 1; ring.w -= 2; ring.h -= 2;
		}
	}
}

static void DrawCheckbox(int i, Boolean on)
{
	SDL_FRect r = ItemRect(i);
	SDL_Color black = { 0, 0, 0, 255 };
	SDL_FRect box = { r.x, r.y + r.h / 2 - 8, 16, 16 };
	RFill(box, 255, 255, 255);
	RFrame(box, 0, 0, 0);
	if (on) {
		SDL_Renderer *rd = SATGetRenderer();
		SDL_FRect t = Dev((SDL_FRect){ box.x + 2, box.y + 2, 11, 11 });
		SDL_SetRenderDrawColor(rd, 0, 0, 0, 255);
		SDL_RenderLine(rd, t.x, t.y, t.x + t.w, t.y + t.h);
		SDL_RenderLine(rd, t.x + t.w, t.y, t.x, t.y + t.h);
	}
	DrawTextLine(r.x + 24, r.y + r.h / 2 - 8, 2, black,
	             kSettingsItems[i].label);
}

static void DrawEditField(int i, const char *text, Boolean focused)
{
	SDL_FRect r = ItemRect(i);
	SDL_Color black = { 0, 0, 0, 255 };
	RFill(r, 255, 255, 255);
	RFrame(r, 0, 0, 0);
	if (focused) {
		SDL_FRect f = { r.x - 2, r.y - 2, r.w + 4, r.h + 4 };
		RFrame(f, 70, 70, 70);
	}
	DrawTextLine(r.x + 6, r.y + r.h / 2 - 8, 2, black, text);
}

/* dialog state shared with the draw callback */
static Boolean s_fast, s_sound, s_adult;
static char s_lives[4], s_points[4], s_width[8], s_height[8], s_zoom[8];
static int s_focus; /* one of the edit-field items */

static char *FocusedField(void)
{
	switch (s_focus) {
	case kItPoints: return s_points;
	case kItWidth:  return s_width;
	case kItHeight: return s_height;
	case kItZoom:   return s_zoom;
	default:        return s_lives;
	}
}

static int FocusedFieldMax(void)
{
	if (s_focus == kItWidth || s_focus == kItHeight)
		return 4;
	return (s_focus == kItZoom) ? 3 : 2;
}

static void DrawSettingsDialog(void)
{
	SDL_Color black = { 0, 0, 0, 255 };
	SDL_FRect box = { DLG_X, DLG_Y, DLG_W, DLG_H };
	DrawDialogBox(box);
	DrawButton(kItOK, true);
	DrawButton(kItCancel, false);
	DrawButton(kItBlueKeys, false);
	DrawButton(kItRedKeys, false);
	DrawCheckbox(kItFastAnim, s_fast);
	DrawCheckbox(kItSound, s_sound);
	DrawCheckbox(kItAdult, s_adult);
	DrawEditField(kItLives, s_lives, s_focus == kItLives);
	DrawEditField(kItPoints, s_points, s_focus == kItPoints);
	DrawEditField(kItWidth, s_width, s_focus == kItWidth);
	DrawEditField(kItHeight, s_height, s_focus == kItHeight);
	DrawEditField(kItZoom, s_zoom, s_focus == kItZoom);
	/* right-aligned against the edit fields; the debug font is wider than
	   Chicago 12 was, so the DITL's left edges would collide */
	DrawTextLine(DLG_X + 187 * DLG_SCALE - TextWidth(2, "Lives:"),
	             DLG_Y + 22 * DLG_SCALE, 2, black, "Lives:");
	DrawTextLine(DLG_X + 187 * DLG_SCALE - TextWidth(2, "Points:"),
	             DLG_Y + 42 * DLG_SCALE, 2, black, "Points:");
	DrawTextLine(DLG_X + 220 * DLG_SCALE, DLG_Y + 22 * DLG_SCALE, 2, black,
	             "(0-99)");
	DrawTextLine(DLG_X + 220 * DLG_SCALE, DLG_Y + 42 * DLG_SCALE, 2, black,
	             "(0-99)");
	DrawTextLine(DLG_X + 10 * DLG_SCALE, DLG_Y + 142 * DLG_SCALE, 2, black,
	             "Play area:");
	DrawTextLine(DLG_X + 143 * DLG_SCALE - 8, DLG_Y + 142 * DLG_SCALE, 2,
	             black, "x");
	DrawTextLine(DLG_X + 10 * DLG_SCALE, DLG_Y + 118 * DLG_SCALE, 2, black,
	             "Zoom:");
	DrawTextLine(DLG_X + 140 * DLG_SCALE, DLG_Y + 118 * DLG_SCALE, 2, black,
	             "%");
}

static Boolean PtInFRect(float x, float y, SDL_FRect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void FieldTypeDigit(char *field, size_t maxDigits, char digit)
{
	size_t n = SDL_strlen(field);
	if (n >= maxDigits) {
		field[0] = digit;
		field[1] = 0;
	} else {
		field[n] = digit;
		field[n + 1] = 0;
	}
}

static void DisplaySettingsScreen(void)
{
	s_fast = gFastGraphics;
	s_sound = gSoundOn;
	s_adult = g.dirtyWordsMode;
	SDL_snprintf(s_lives, sizeof s_lives, "%d", g.numLives);
	SDL_snprintf(s_points, sizeof s_points, "%d", g.numPoints);
	int winW, winH;
	SATGetPlayAreaSize(&winW, &winH);
	SDL_snprintf(s_width, sizeof s_width, "%d", winW);
	SDL_snprintf(s_height, sizeof s_height, "%d", winH);
	SDL_snprintf(s_zoom, sizeof s_zoom, "%d", SATGetZoom());
	s_focus = kItLives;

	for (;;) {
		SDL_Event e;
		int clicked = -1;
		Boolean commit = false, cancel = false;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_EVENT_QUIT) {
				gSATQuitRequested = true;
				return;
			}
			if (e.type == SDL_EVENT_KEY_DOWN) {
				SDL_Keycode k = e.key.key;
				if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
					commit = true;
				else if (k == SDLK_ESCAPE)
					cancel = true;
				else if (k == SDLK_TAB) {
					static const int cycle[] = { kItLives, kItPoints,
						                         kItZoom, kItWidth,
						                         kItHeight };
					int n = (int)SDL_arraysize(cycle);
					for (int i = 0; i < n; i++)
						if (cycle[i] == s_focus) {
							s_focus = cycle[(i + 1) % n];
							break;
						}
				} else if (k == SDLK_BACKSPACE) {
					char *f = FocusedField();
					size_t n = SDL_strlen(f);
					if (n)
						f[n - 1] = 0;
				} else if (k >= SDLK_0 && k <= SDLK_9)
					FieldTypeDigit(FocusedField(), FocusedFieldMax(),
					               (char)('0' + (k - SDLK_0)));
				else if (k >= SDLK_KP_1 && k <= SDLK_KP_0) {
					int d = (k == SDLK_KP_0) ? 0 : (int)(k - SDLK_KP_1) + 1;
					FieldTypeDigit(FocusedField(), FocusedFieldMax(),
					               (char)('0' + d));
				}
			}
			if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
			    e.button.button == SDL_BUTTON_LEFT) {
				SDL_ConvertEventToRenderCoordinates(SATGetRenderer(), &e);
				float f = SATDeviceScale();
				for (int i = 0; i < kItCount; i++)
					if (PtInFRect(e.button.x / f, e.button.y / f,
					              ItemRect(i)))
						clicked = i;
			}
		}

		if (clicked >= 0) {
			SATSoundPlay(g.menuLowBassSnd, 1, nil);
			switch (clicked) {
			case kItOK: commit = true; break;
			case kItCancel: cancel = true; break;
			case kItFastAnim: s_fast = !s_fast; break;
			case kItSound: s_sound = !s_sound; break;
			case kItAdult: s_adult = !s_adult; break;
			case kItLives: s_focus = kItLives; break;
			case kItPoints: s_focus = kItPoints; break;
			case kItWidth: s_focus = kItWidth; break;
			case kItHeight: s_focus = kItHeight; break;
			case kItZoom: s_focus = kItZoom; break;
			case kItBlueKeys:
				if (!WaitForMouseRelease())
					return;
				SetKeys(&LSKeys, "Blue player");
				break;
			case kItRedKeys:
				if (!WaitForMouseRelease())
					return;
				SetKeys(&RSKeys, "Red player");
				break;
			}
		}

		if (commit) {
			gFastGraphics = s_fast;
			g.dirtyWordsMode = s_adult;
			if (s_sound != gSoundOn) {
				gSoundOn = s_sound;
				if (gSoundOn)
					SATSoundOn();
				else
					SATSoundOff();
			}
			g.numLives = ClampSetting(SDL_atoi(s_lives));
			g.numPoints = ClampSetting(SDL_atoi(s_points));
			/* an emptied field means "keep the current value" */
			int curW, curH;
			SATGetPlayAreaSize(&curW, &curH);
			int w = s_width[0] ? SDL_atoi(s_width) : curW;
			int h = s_height[0] ? SDL_atoi(s_height) : curH;
			int z = s_zoom[0] ? SDL_atoi(s_zoom) : SATGetZoom();
			if (w != curW || h != curH || z != SATGetZoom()) {
				SATSetPlayAreaSize(w, h);
				SATSetZoom(z);
				CreateLimitsBuffer();
				LayoutMenu();
			}
			SavePrefs();
		}
		if (commit || cancel) {
			SATSoundPlay(g.menuHighBassSnd, 1, nil);
			break;
		}

		DrawMenuWindow();
		PresentWithText(DrawSettingsDialog);
		SDL_Delay(10);
	}
	DrawMenuWindow();
	SATPresent();
	(void)WaitForMouseRelease();
}

/* ---- key configuration ---- */

static const char *g_keyPrompt;
static const char *g_keyPlayer;

static void KeyPromptOverlay(void)
{
	/* styled after the original SetKeys dialog (DLOG 129, 242x44, at 2x) */
	SDL_Color black = { 0, 0, 0, 255 };
	float w = 242 * DLG_SCALE, h = 44 * DLG_SCALE;
	SDL_FRect box = { (gSAT.offSizeH - w) / 2, VC(240), w, h };
	DrawDialogBox(box);
	DrawTextLine(box.x + box.w / 2 - TextWidth(2, g_keyPlayer) / 2,
	             box.y + 14, 2, black, g_keyPlayer);
	DrawTextLine(box.x + box.w / 2 - TextWidth(2, g_keyPrompt) / 2,
	             box.y + h - 30, 2, black, g_keyPrompt);
}

static int WaitForKeyBit(void)
{
	SDL_Event e;
	for (;;) {
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_EVENT_QUIT) {
				gSATQuitRequested = true;
				return -1;
			}
			if (e.type == SDL_EVENT_KEY_DOWN) {
				int bit = SATScancodeToKeyMapBit(e.key.scancode);
				if (bit >= 0)
					return bit;
			}
		}
		DrawMenuWindow();
		PresentWithText(KeyPromptOverlay);
		SDL_Delay(10);
	}
}

static void SetKeys(Controls *keys, const char *playerName)
{
	static const char *prompts[7] = {
		"Press the key for moving up...",
		"Press the key for moving down...",
		"Press the key for moving left...",
		"Press the key for moving right...",
		"Press the key for shooting/releasing the ball...",
		"Press the key for rotating...",
		"Press the key for specials...",
	};
	int *slots[7] = { &keys->up, &keys->down, &keys->left, &keys->right,
		              &keys->shoot, &keys->rotate, &keys->special };

	g_keyPlayer = playerName;
	for (int i = 0; i < 7; i++) {
		g_keyPrompt = prompts[i];
		int bit = WaitForKeyBit();
		if (bit < 0)
			return;
		*slots[i] = bit;
		SATSoundPlay(g.menuLowBassSnd, 1, nil);
		SDL_Delay(150); /* let go before the next prompt reads the key */
	}
}

/* ---- about screen ---- */

static void AboutTextOverlay(void)
{
	SDL_Color red = { 220, 40, 40, 255 };
	SDL_Color green = { 60, 255, 60, 255 };
	SDL_Color blue = { 120, 140, 255, 255 };
	DrawTextCentered(VC(280), 3, red, "Version 1.0.1");
	DrawTextCentered(VC(330), 2, red, "2002, Eduardo Ojeda");
	DrawTextCentered(VC(365), 2, red,
	                 "Made with Ingemar Ragnemalm's Sprite Animation Toolkit");
	DrawTextCentered(VC(430), 2, green,
	                 "SDL3 port, 2026 - rebuilt from the original source");
	DrawTextCentered(VC(520), 1, blue, "Click to return to the menu");
}

static void DisplayAboutScreen(void)
{
	PicHandle titlePic;
	Rect titleRect, full;

	MyFadeToBlack(30);

	SetPort(gSAT.wind.port);
	ForeColor(blackColor);
	SetRect(&full, 0, 0, gSAT.offSizeH, gSAT.offSizeV);
	PaintRect(&full);
	SetRect(&titleRect, gSAT.offSizeH / 2 - 195,
	        gSAT.offSizeV / 2 - 150 - 53, gSAT.offSizeH / 2 + 196,
	        gSAT.offSizeV / 2 - 150 + 54);
	titlePic = GetPicture(137);
	DrawPicture(titlePic, &titleRect);
	DisposeHandle((Handle)titlePic);

	PresentWithText(AboutTextOverlay);
	MyFadeFromBlack(30);
	SATSoundPlay(g.aboutSnd, 1, nil);

	for (;;) {
		if (!SATPumpEvents())
			return;
		if (Button())
			break;
		PresentWithText(AboutTextOverlay);
		SDL_Delay(10);
	}
	if (!WaitForMouseRelease())
		return;

	MyFadeToBlack(30);
	DrawMenuWindow();
	SATPresent();
	MyFadeFromBlack(30);
}

static void CleanUp(void)
{
	SavePrefs(); /* key bindings can change without passing through OK */
	SATSoundShutup();
	ShowCursor();
	if (gCanUseGamma) {
		SATSetGammaLevel(100);
	}
}
