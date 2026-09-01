/* Ascent — SDL3 port of the 2002 original.
   This file replaces the Classic Mac event loop, menu screen, dialogs and
   window management from the original main.c (preserved at the repository
   root). The game modules (sBall.c, sLeftShip.c, ...) are the originals. */

#include <math.h>
#include <stdlib.h>
#include "mySAT.h"
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

/* ---- text helper: SDL debug text at a scale, in 800x600 coordinates ---- */

static void DrawTextLine(float x, float y, float scale, SDL_Color c,
                         const char *text)
{
	SDL_Renderer *r = SATGetRenderer();
	SDL_SetRenderScale(r, scale, scale);
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
	DrawTextLine(400.0f - TextWidth(scale, text) / 2, y, scale, c, text);
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
	DrawTextCentered(270, 4, green, "PAUSED");
	DrawTextCentered(320, 2, grey, "P: resume    Esc: quit to menu");
}

static void Play(void)
{
	short limitsFrameCounter = 1;
	KeyMap theKeys;
	Boolean pKeyWasUp = true, escKeyWasUp = true;
	Uint64 nextFrame;

	InitNewGame();
	nextFrame = SDL_GetTicksNS();

	do {
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

	do {
		if (!SATPumpEvents())
			break;
		GetMouse(&mouseLoc);

		int item = -1;
		if (PtInRect(mouseLoc, &menuNewGameRect))
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
	DrawPicture(backgroundPic, &backgroundRect);
	DrawPicture(menuPic, &menuRect);
}

static void InitNewGame(void)
{
	if (gCanUseGamma)
		MyFadeToBlack(50);

	SATCustomInit(128, 128, nil, nil, nil, true, true, true, false, true);
	SATConfigure(false, kLayerSort, kForwardCollision, 32);

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

	SATInitToolbox();
	srand((unsigned)SDL_GetTicks() ^ 0x5eed);

	if (gCanUseGamma)
		MyFadeToBlack(50);

	/*CREATE BUFFERS*/
	SetRect(&bufferRect, 0, 0, 300, 200);
	error = NewScreenBuffer(&bufferRect, false, &offScreenGD,
	                        &theOffScreenBuffer);
	SetRect(&bufferRect, 0, 0, 20, gSAT.offSizeV);
	error = NewScreenBuffer(&bufferRect, false, &offScreenGD,
	                        &limitsOffScreenBuffer);
	(void)error;

	/*SET STANDARD KEYCODES*/
	LSKeys.up = kWKeyMap;
	LSKeys.down = kSKeyMap;
	LSKeys.left = kAKeyMap;
	LSKeys.right = kDKeyMap;
	LSKeys.shoot = kControlKeyMap;
	LSKeys.special = kTabKeyMap;
	LSKeys.rotate = kShiftKeyMap;

	RSKeys.up = kUpArrowKeyMap;
	RSKeys.down = kDownArrowKeyMap;
	RSKeys.left = kLeftArrowKeyMap;
	RSKeys.right = kRightArrowKeyMap;
	RSKeys.shoot = kSpaceKeyMap;
	RSKeys.special = kOptionKeyMap;
	RSKeys.rotate = kCommandKeyMap;

	/*LOAD SOUNDS*/
	SATSoundInitChannels(6);
	LoadSounds();

	/*LOAD SPRITE FACES*/
	LoadFaces();

	/*MENU WINDOW*/
	SetRect(&menuRect, gSAT.offSizeH / 2 - 247, 0, gSAT.offSizeH / 2 + 246,
	        391);
	SetRect(&menuNewGameRect, menuRect.left + 42, 164,
	        menuRect.left + 42 + 305, 164 + 47);
	SetRect(&menuSettingsRect, menuRect.left + 236, 237,
	        menuRect.left + 236 + 257, 237 + 60);
	SetRect(&menuAboutRect, menuRect.left + 25, 288, menuRect.left + 25 + 172,
	        288 + 46);
	SetRect(&menuQuitRect, menuRect.left + 353, 321,
	        menuRect.left + 353 + 139, 321 + 67);

	newGameLitPic = GetPicture(132);
	settingsLitPic = GetPicture(133);
	aboutLitPic = GetPicture(134);
	quitLitPic = GetPicture(135);
	backgroundPic = GetPicture(128);
	menuPic = GetPicture(131);

	/*MISC CALLS*/
	g.numLives = 10;
	g.numPoints = 5;
	g.gameDone = true;
	g.dirtyWordsMode = true;

	DrawMenuWindow();
	if (gCanUseGamma)
		MyFadeFromBlack(50);
}

/* ---- settings screen (replaces the Classic dialog) ---- */

static int g_settingsHover = -1;

enum {
	kSetSound = 0, kSetDirtyWords, kSetLives, kSetPoints,
	kSetLeftKeys, kSetRightKeys, kSetDone, kSetCount
};

static Rect SettingsRowRect(int row)
{
	Rect r;
	SetRect(&r, 250, 150 + row * 44, 550, 150 + row * 44 + 32);
	return r;
}

static void SettingsTextOverlay(void)
{
	SDL_Color green = { 60, 255, 60, 255 };
	SDL_Color dim = { 30, 140, 30, 255 };
	char buf[64];

	DrawTextCentered(100, 3, green, "SETTINGS");
	for (int i = 0; i < kSetCount; i++) {
		Rect r = SettingsRowRect(i);
		SDL_Color c = (i == g_settingsHover) ? green : dim;
		switch (i) {
		case kSetSound:
			SDL_snprintf(buf, sizeof buf, "Sound: %s",
			             gSoundOn ? "On" : "Off");
			break;
		case kSetDirtyWords:
			SDL_snprintf(buf, sizeof buf, "Dirty Words: %s",
			             g.dirtyWordsMode ? "On" : "Off");
			break;
		case kSetLives:
			SDL_snprintf(buf, sizeof buf, "Lives: %d  < >", g.numLives);
			break;
		case kSetPoints:
			SDL_snprintf(buf, sizeof buf, "Points to Win: %d  < >",
			             g.numPoints);
			break;
		case kSetLeftKeys:
			SDL_snprintf(buf, sizeof buf, "Configure Left Player Keys...");
			break;
		case kSetRightKeys:
			SDL_snprintf(buf, sizeof buf, "Configure Right Player Keys...");
			break;
		case kSetDone:
			SDL_snprintf(buf, sizeof buf, "Done");
			break;
		}
		DrawTextLine((float)r.left, (float)r.top + 8, 2, c, buf);
	}
}

static void DisplaySettingsScreen(void)
{
	Boolean done = false;
	Point mouse;

	while (!done) {
		if (!SATPumpEvents())
			return;
		GetMouse(&mouse);
		g_settingsHover = -1;
		for (int i = 0; i < kSetCount; i++) {
			Rect r = SettingsRowRect(i);
			if (PtInRect(mouse, &r))
				g_settingsHover = i;
		}

		DrawMenuWindow(); /* background */
		PresentWithText(SettingsTextOverlay);

		if (Button() && g_settingsHover >= 0) {
			int item = g_settingsHover;
			Rect r = SettingsRowRect(item);
			Boolean leftHalf = mouse.h < (r.left + r.right) / 2;
			if (!WaitForMouseRelease())
				return;
			switch (item) {
			case kSetSound:
				gSoundOn = !gSoundOn;
				if (gSoundOn)
					SATSoundOn();
				else
					SATSoundOff();
				break;
			case kSetDirtyWords:
				g.dirtyWordsMode = !g.dirtyWordsMode;
				break;
			case kSetLives:
				g.numLives += leftHalf ? -1 : 1;
				if (g.numLives < 1) g.numLives = 99;
				if (g.numLives > 99) g.numLives = 1;
				break;
			case kSetPoints:
				g.numPoints += leftHalf ? -1 : 1;
				if (g.numPoints < 1) g.numPoints = 99;
				if (g.numPoints > 99) g.numPoints = 1;
				break;
			case kSetLeftKeys:
				SetKeys(&LSKeys, "LEFT (blue) player");
				break;
			case kSetRightKeys:
				SetKeys(&RSKeys, "RIGHT (red) player");
				break;
			case kSetDone:
				done = true;
				break;
			}
			SATSoundPlay(g.menuHighBassSnd, 1, nil);
		}
		SDL_Delay(10);
	}
	DrawMenuWindow();
	SATPresent();
}

/* ---- key configuration ---- */

static const char *g_keyPrompt;
static const char *g_keyPlayer;

static void KeyPromptOverlay(void)
{
	SDL_Color green = { 60, 255, 60, 255 };
	SDL_Color grey = { 200, 200, 200, 255 };
	DrawTextCentered(240, 2, grey, g_keyPlayer);
	DrawTextCentered(280, 2, green, g_keyPrompt);
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
	DrawTextCentered(280, 3, red, "Version 1.0.1");
	DrawTextCentered(330, 2, red, "2002, Eduardo Ojeda");
	DrawTextCentered(365, 2, red,
	                 "Made with Ingemar Ragnemalm's Sprite Animation Toolkit");
	DrawTextCentered(430, 2, green,
	                 "SDL3 port, 2026 - rebuilt from the original source");
	DrawTextCentered(520, 1, blue, "Click to return to the menu");
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
	SATSoundShutup();
	ShowCursor();
	if (gCanUseGamma) {
		SATSetGammaLevel(100);
	}
}
