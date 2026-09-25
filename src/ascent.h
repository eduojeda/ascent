/*App Constants*/

#define kAboutMenu				1			//Menu item constants
#define kNewMenu				1
#define kAbortMenu				2
#define kSettingsMenu			3
#define kReturnToGameMenu		5
#define kFastGMenu				7
#define kSoundMenu				8
#define kDirtyWordsMenu			9
#define kQuitMenu				11

#define kUpArrowKeyMap			121			// key map offsets for right player
#define kDownArrowKeyMap		122
#define kRightArrowKeyMap		123
#define kLeftArrowKeyMap		124
#define kCommaKeyMap			44
#define kPeriodKeyMap			40
#define kSlashKeyMap			43

#define kAKeyMap				7			// key map offsets for left player
#define	kDKeyMap				5
#define	kWKeyMap				10
#define kSKeyMap				6
#define kFKeyMap				4
#define kGKeyMap				2
#define kHKeyMap				3

#define kPKeyMap				36

/* Settings Dialog Item List Constants */

#define kCBFastGraphicsID		3
#define kCBSoundID				4
#define kCBDirtyWordsID			5
#define kTXLivesID				6
#define kTXPointsID				7
#define kBtnLeftKeyConfigID		13	
#define kBtnRightKeyConfigID	14		

/* Alerts */

#define kEnvironAlert			128
#define kNoMemAlert				129
#define kUnknownErrorAlert		130
#define kColorAlert				131

/* Physics Model Constants */

#define kTiempo					0.166		//Time scale
#define	kFriction				0.995		//Loss of speed per frame due to friction
#define kFrictTrshld			0.4			//Speed threshold to stop the ball (if the ball speed goes below this while rolling, it wil stop dead)
#define kShipViscosity			10			//Arbitrary measure of the viscosity of the "atmosphere", adjusted so a normal ship's speed never exceeds 6 pixels per frame by much
#define kShipBounceLoss			0.25		//Amount of speed remaining on the ship after bouncing
#define kBallViscosity			5			//How much is added to the ship's "viscosity factor" if it is traveling with the ball
#define kBallBounceLoss			0.9			//Amount of speed remaining on the ball after bouncing
#define kBallDiameter			10			//Diameter of the ball in pixels
#define kBallMass				10			//Mass of the ball in Kilograms
#define kShipMass				75			//Mass of a ship in Kilograms
#define kGravityV				9.82 * 6	//Vertical acceleration of gravity, augmented to make it more noticeable on a 1/6 timescale
#define kGravityH				0			//Horizontal acceleration of gravity, currently zero because I'm not using it (but still fully implemented)
#define kGoodieBounceLoss		0.9			//Amount of speed remaining on the goodie after bouncing
#define kGameSpeed				1			//Speed of the game (1 is 60fps, 2 is 30fps... you get the idea)

/* Game Constants */

#define	kLimits					125			//Distance from the limit's core to the border of the screen
#define kShipLives				10			//Amount of initial lives
#define kWinningScore			5			//Goals to win
#define kShipShields			100			//Amount of energy the ships shields can hold
#define kLimitGShields			2000		//Amount of damage a Limit Generator can take
#define kExpulsionForce			1200		//Repelling horizontal force that a ship recieves when it touches a limit
#define kReincarnDelay			300			//Ticks between ship death and reincarnation
#define kCountdownDelay			300			//Ticks for the starting countdown
#define kWinnerDelay			300			//Ticks for the winner display
#define kBulletDamage			20			//Average amount of damage weapon hits do
#define kRocketDamage			110
#define kMissileDamage			50
#define kLimitDamage			15			//Average amount of damage touching a limit does

#define kShipHeight				28			//Average height of the ship sprite in pixels
#define kShipWidth				45			//Average width of the ship sprite in pixels
#define kTargetHeight			30			//Height of the targets in pixels
#define kTargetWidth			10			//Width of the targets in pixels

#define kStdNumSmallExp			12			//Standard number of small explosions in an explosion
#define kStdNumBigExp			5			//Standard number of big explosions in an explosion
#define kStdSpeedSmallExp		10			//Standard speed of the small explosions
#define kStdSpeedBigExp			3			//Standard speed of the big explosions

#define kShipLayer				1			//Layers for all the objects
#define kBallLayer				2
#define kGoodieLayer			2
#define kBulletLayer			3
#define kRocketLayer			3
#define kMissileLayer			3
#define kTargetLayer			4	
#define kExpLayer				5
#define kSmokeLayer				6
#define kSparkLayer				6
#define kBaseLayer				7
#define kBallSpawnerLayer		7

#define kShipKind				1			//Kinds for all the objects
#define kBallKind				2
#define kTargetKind				3			
#define kBulletKind				4
#define kLaserGKind 			5
#define kBigExpKind				6
#define kSmallExpKind			7
#define	kNumberKind				8
#define kGoodieKind				9
#define kRocketKind				10
#define kBaseKind				11
#define kSmokeKind				12
#define kMissileKind			13
#define kGoodieIndKind			14
#define kSparkKind				15
#define kBallSpawnerKind		16
#define kDebrisKind				17

#define kBallFreeMode			1			//Modes
#define kBallCaughtMode			2
#define kBallMagnetoMode		3
#define kShipAliveMode			1
#define kShipRotatingMode		2	
#define kShipDyingMode			3
#define kShipDisabledMode		4
#define kLimitGAliveMode		1
#define kLimitGDestroyedMode	2
#define kGoodieNormalMode		1
#define kGoodieLimboMode		2
#define kSpawnerSpawningMode	1
#define kSpawnerIdleMode		2

/* Base Resource IDs */

#define kBallID					128
#define kLeftTargetID			129
#define kRightTargetID			130
#define kLimitRecieverOnID		131
#define kLimitRecieverOffID		132
#define kLimitGID				133
#define kLimitGDeadID			134
#define kBallMagnetoIndicatorID	138
#define kBallShotIndicatorID	139
#define kRocketFacingRightID	140
#define kRocketFacingLeftID		141
#define kGreenBulletID			150
#define kRedBulletID			151
#define kGoodieID				160
#define kSmallExplosionID		200
#define kBigExplosionID			300
#define kSmokesID				400
#define kMissileFacingRightID	500
#define kMissileFacingLeftID	510
#define kSparksID				600
#define kBlueBaseID				700
#define kRedBaseID				800
#define kBallSpawnerID			900
#define kLeftShipFacingRightID	1000	
#define kLeftShipFacingLeftID	1100
#define kLeftShipRotatingID		1200
#define kRightShipFacingRightID	2000
#define kRightShipFacingLeftID	2100
#define kRightShipRotatingID	2200
#define kBodyDebrisID			3000
#define kEngineDebrisID			3100
#define kNumbersID				4000

/* Global Data Structures */

typedef struct Globals
{
	FacePtr		leftShipFacingRight[8];			//Graphics for the left ship pointing right
	FacePtr		leftShipFacingLeft[8];			//Graphics for the left ship pointing left
	FacePtr		leftShipRotating[3];			//Graphics for the left ship rotating
	FacePtr		rightShipFacingRight[8];		//Graphics for the right ship pointing right
	FacePtr		rightShipFacingLeft[8];			//Graphics for the right ship pointing left
	FacePtr		rightShipRotating[3];			//Graphics for the right ship rotating
	FacePtr		missileFacingLeft[5];			//Graphics for the missile pointing left
	FacePtr		missileFacingRight[5];			//Graphics for the missile pointing right
	FacePtr		smallExplosionFaces[8];			//Graphics for the bullet impact animation
	FacePtr		bigExplosionFaces[8];			//Graphics for the big explosion animation
	FacePtr		numberFaces[10];				//Graphics for the numbers
	FacePtr		smokeFaces[6];					//Graphics for the smoke
	FacePtr		sparkFaces[6];					//Graphics for the sparks
	FacePtr		blueBaseFaces[8];				//Graphics for the blue base
	FacePtr		redBaseFaces[8];				//Graphics for the red base
	FacePtr		ballSpawnerFaces[6];			//Graphics for the ball spawner
	FacePtr		goodieFaces[5];					//Graphics for the goodie
	FacePtr		bodyDebrisFaces[7];				//Graphics for the body debris
	FacePtr		engineDebrisFaces[5];			//Graphics for the engine debris

	SpritePtr	ball, leftShip, rightShip, 					//Data structures for some of the objects 
				leftTarget, rightTarget,					//The goals
				leftSpecial, rightSpecial,					//The goodie indicators
				bottomRightLG, bottomLeftLG,				//The limit generators at the bottom of the screen
				blueBase, redBase, spawner,					//The two bases and the thing the ball comes out of
				goodie,	puller;								//goodie is the powerup cannister, puller is a pointer to the ship that the ball is attracted to when in Magneto mode
		
	Handle		magnetoSnd, bounceSnd, shieldReloadSnd,  	//Handles to the sounds
				redShotSnd, greenShotSnd, hitLaserSnd, 
				catchSnd, releaseSnd, explosionSnd, ballShotSnd, 
				bulletHitSnd, missileSnd, rocketSnd, menuHighBassSnd,
				noAmmoSnd, openBaseSnd, closeBaseSnd, menuLowBassSnd,
				ballSpawnerOpeningSnd, ballSpawnerClosingSnd, pauseSnd,
				woohooSnd, wowSnd, joySnd, notBadSnd, aboutSnd,
				cool1Snd, cool2Snd, whoaSnd, shoveSnd, smackYouSnd,
				youFuckerSnd, thisSuxSnd, ohSnd, notCoolSnd,
				goodieBounceSnd, goodieCatchSnd, goodieAppearsSnd,
				threeSnd, twoSnd, oneSnd, goSnd, applauseSnd;	
				
	Boolean		leftShipReincarnating, rightShipReincarnating,
				gameDone, gameFinishing, gameStarting, dirtyWordsMode;	
				
	int			leftShipScore, rightShipScore,		//The score, maxForce and lives for each ship 	
				leftShipLives, rightShipLives,
				leftShipMaxForce, rightShipMaxForce,
				numLives, numPoints,
				LSReincC, RSReincC,					//Counters that can't be static variables (they need to be reinited every NewGame)		
				countDownC, winnerC, goodieC;
				
	Vector		gravityAccel;						//Normal gravity acceleration vector	
	
}Globals;	

typedef struct Controls
{
	int up, down, left, right, shoot, special, rotate;
}Controls;

/*One frame's worth of control for one ship. The 2002 ship tasks read the
  keyboard directly; they now read this instead, so a ship can be driven by
  something other than a person without touching the flight model.*/

typedef struct ShipInput
{
	Boolean up, down, left, right, shoot, special, rotate;
}ShipInput;

extern ShipInput	gLeftInput, gRightInput;

/*App Function Prototypes*/
/*(The Classic event-loop prototypes lived here; the SDL port keeps its
  entry points static in main.c.)*/

/* Ballistic Simulator Function Declarations */

extern void		Bounce(SpritePtr object, int XSize, int YSize, float bounceLoss, Handle bounceSnd);
extern void		InitVariables(SpritePtr object);
extern void		BallisticModel(SpritePtr object, Vector gravityAccel);
extern void		PreModel(double launchAngle, double launchForce);
extern void		ReleaseBall(SpritePtr ship);
extern Vector	MagnetoPull(SpritePtr object, float forceMagnitude);

/*Misc Functions Declarations */

extern void 	DisplayScore(void);
extern void		DisplayLives(void);
extern void 	DrawBackground(void);
extern void 	DrawLimits(void);	
extern void 	PseudoDebugger(void);
extern void 	ShowFPS(short FPSCounter);
extern void 	Explode(SpritePtr object, short numSmallExp, short numBigExp, short smallExpSpeed, short bigExpSpeed, Handle explosionSnd);
extern void		ExplodeAtCoords(int xPos, int yPos, short numSmallExp, short numBigExp, short smallExpSpeed, short bigExpSpeed);
extern void		DisplayShields(void);
extern void		GoodieRespawnTimer(void);
extern void		DisplayCountdown(void);
extern void		DisplayWinner(SpritePtr winner, SpritePtr loser);
extern void		MyFadeToBlack(int time);
extern void		MyFadeFromBlack(int time);

/* SAT Units Function Declarations */

extern pascal void SetupBall(SpritePtr ball);
extern pascal void HandleBall(SpritePtr ball);
extern pascal void HitTaskBall(SpritePtr me, SpritePtr him);

extern pascal void SetupShipLeft(SpritePtr leftShip);
extern pascal void HandleShipLeft(SpritePtr leftShip);
extern pascal void HitTaskShipLeft(SpritePtr leftShip, SpritePtr him);
extern void		   LeftShipReincarnationDelay();
extern void 	   SelectFace(SpritePtr ship, FacePtr shipFacingLeft[], FacePtr shipFacingRight[]);
extern pascal void SetupShipRight(SpritePtr rightShip);
extern pascal void HandleShipRight(SpritePtr rightShip);
extern pascal void HitTaskShipRight(SpritePtr rightShip, SpritePtr him);
extern void		   RightShipReincarnationDelay();

extern void 	   ActivatePowerup(SpritePtr ship);
extern void		   ShootRocket(SpritePtr ship);
extern void		   ShootMissile(SpritePtr ship);
extern void		   ShootBullet(SpritePtr ship);
extern void		   ShootBall(SpritePtr ship);
extern void		   ReloadShields(SpritePtr ship);
extern void		   BallMagneto(SpritePtr ship);

extern pascal void SetupRocket(SpritePtr rocket);
extern pascal void HandleRocket(SpritePtr rocket);
extern pascal void HitTaskRocket(SpritePtr rocket, SpritePtr him);

extern pascal void SetupMissile(SpritePtr missile);
extern pascal void HandleMissile(SpritePtr missile);
extern pascal void HitTaskMissile(SpritePtr missile, SpritePtr him);

extern pascal void SetupTargetLeft(SpritePtr target);
extern pascal void HitTaskTargetLeft(SpritePtr me, SpritePtr him);
extern pascal void SetupTargetRight(SpritePtr target);
extern pascal void HitTaskTargetRight(SpritePtr me, SpritePtr him);
extern pascal void HandleTarget(SpritePtr target);

extern pascal void SetupBullet(SpritePtr bullet);	
extern pascal void HandleBullet(SpritePtr bullet);
extern pascal void HitTaskBullet(SpritePtr bullet, SpritePtr him);

extern pascal void SetupLimitGenerator(SpritePtr generator);
extern pascal void SetupLimitReciever(SpritePtr reciever);
extern pascal void HandleLimitGenerator(SpritePtr generator);
extern pascal void HandleLimitReciever(SpritePtr reciever);
extern pascal void HitTaskLimitGenerator(SpritePtr me, SpritePtr him);

extern pascal void SetupBigExplosion(SpritePtr me);
extern pascal void HandleBigExplosion(SpritePtr me);
extern pascal void SetupSmallExplosion(SpritePtr me);
extern pascal void HandleSmallExplosion(SpritePtr me);

extern pascal void SetupNumber(SpritePtr num);
extern pascal void HandleNumber(SpritePtr num);

extern pascal void SetupRedBase(SpritePtr base);
extern pascal void HandleBase(SpritePtr base);
extern pascal void SetupBlueBase(SpritePtr base);

extern pascal void SetupGoodie(SpritePtr goodie);
extern pascal void HandleGoodie(SpritePtr goodie);
extern pascal void HitTaskGoodie(SpritePtr goodie, SpritePtr him);

extern pascal void SetupSmoke(SpritePtr smoke);
extern pascal void HandleSmoke(SpritePtr smoke);

extern pascal void SetupSpark(SpritePtr spark);
extern pascal void HandleSpark(SpritePtr spark);
extern pascal void SetupSparkler(SpritePtr sparkler);
extern pascal void HandleSparkler(SpritePtr sparkler);
extern void 	   Sparkle(SpritePtr object, short numSparks, short sparkSpeed);

extern pascal void SetupBallSpawner(SpritePtr spawner);
extern pascal void HandleBallSpawner(SpritePtr spawner);

extern pascal void SetupWinnerLoser(SpritePtr wl);
extern pascal void HandleWinnerLoser(SpritePtr wl);

extern pascal void SetupDebris1(SpritePtr deb);
extern pascal void HandleDebris1(SpritePtr deb);
extern pascal void SetupDebris2(SpritePtr deb);
extern pascal void HandleDebris2(SpritePtr deb);