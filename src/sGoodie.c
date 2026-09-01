#include "mySAT.h"
#include "ascent.h"

/* Globals */

extern Globals		g;
int 				t;

/*Constants*/

#define kNumberOfPowerUps		5			//Total number of PowerUps

#define kNoPowerup				0			//Powerup's internal codes
#define kRocketPowerup			1
#define kMissilePowerup			2
#define kHealthPowerup			3
#define kBallShotPowerup		4
#define kBallMagnetoPowerup		5
#define kHiSpeedPowerup			6			//DISABLED

#define kGoodieHeight			18			//Height of the goodie in pixels
#define kGoodieWidth			20			//Width of the goodie in pixels
#define kGoodieCrazyness		50			//There are 1/kGoodieCrazyness chances that the goodie will jump erratically that frame
#define kGoodieRespawnDelay		1000		//Max number of ticks until the goodie reapears after beign grabbed

/*Code*/

pascal void SetupGoodie(SpritePtr goodie)
{
	goodie->speed.h = (SATRand(5)-SATRand(5))*3;			//Starting speed of the goodie, something between ±3 and ±15
	goodie->task = &HandleGoodie;
	InitVariables(goodie);
	goodie->hitTask = &HitTaskGoodie;
	goodie->powerup = SATRand(kNumberOfPowerUps) + 1;
	goodie->mode = kGoodieNormalMode;
	goodie->counter = 0;
	SetRect(&goodie->hotRect, 3,3,kGoodieWidth,kGoodieHeight);
	
	t = SATRand(kGoodieRespawnDelay)+1;
}

pascal void HandleGoodie(SpritePtr goodie)
{
static int faceCounter = 2, faceIncrement = 1;

	switch(goodie->mode)
	{
		case kGoodieNormalMode:
			BallisticModel(goodie, g.gravityAccel);
			Bounce(goodie, kGoodieWidth, kGoodieHeight, kGoodieBounceLoss, g.goodieBounceSnd);
			goodie->position.h = goodie->pos.h;								//move the actual graphic to the coordinates we calculated in BallisticModel()
			goodie->position.v = goodie->pos.v;
			
			if(SATRand(kGoodieCrazyness) == 1)								//a small chance every frame of doing an erratic move
			{
				goodie->speed.h = (SATRand(20)-SATRand(20))*3;
				goodie->speed.v = (SATRand(5)-SATRand(20))*3;
			}
			
			goodie->face = g.goodieFaces[faceCounter/10];					//Loops the goodie's faces
			faceCounter += faceIncrement;
			if(faceCounter >= 49 || faceCounter <= 1)
				faceIncrement *= -1;
				
			break;
			
		case kGoodieLimboMode:
			if(goodie->counter == t)
			{
				goodie->task = nil;
				SATNewSprite(kGoodieKind, SATRand(gSAT.offSizeH - 100) + 100, 0, &SetupGoodie);
				SATSoundPlay(g.goodieAppearsSnd, 1, nil);
				goodie->counter = 0;
			}goodie->counter++;
			break;
	}				
}

pascal void HitTaskGoodie(SpritePtr goodie, SpritePtr him)
{
	if(him->kind == kShipKind && him->mode != kShipDyingMode)		//Make sure it doesn't get grabbed by a piece of debris :)
	{
		him->powerup = goodie->powerup;			//The ship gets the powerup
		goodie->mode = kGoodieLimboMode;		//The goodie enters limbo mode and dissapears
		goodie->face = nil;
		goodie->position.h = -3000;				//We move it far, far away so we are sure it is no longer in the game
		goodie->position.v = -3000;
		SATSoundPlay(g.goodieCatchSnd, 1, nil);	//Play the sound
	}
	
	if(him->task == &HandleShipLeft)
	{	
		switch(him->powerup)
		{
			case kRocketPowerup:
				g.leftSpecial->face = SATGetFace(kRocketFacingRightID);
				break;
				
			case kMissilePowerup:
				g.leftSpecial->face = g.missileFacingRight[2];
				break;	
				
			case kHealthPowerup:
				ReloadShields(him);
				break;		
			
			case kBallShotPowerup:
				g.leftSpecial->face = SATGetFace(kBallShotIndicatorID);	
				break;
				
			case kBallMagnetoPowerup:
				g.leftSpecial->face = SATGetFace(kBallMagnetoIndicatorID);	
				break;		
			
/*			case kHiSpeedPowerup:
				g.leftSpecial->face = SATGetFace(kHiSpeedIndicatorID);	
				break;*/
		}
	}else if(him->task == &HandleShipRight)
	{
		switch(him->powerup)
		{
			case kRocketPowerup:
				g.rightSpecial->face = SATGetFace(kRocketFacingLeftID);
				break;
				
			case kMissilePowerup:
				g.rightSpecial->face = g.missileFacingLeft[2];
				break;	
				
			case kHealthPowerup:
				ReloadShields(him);
				break;	
				
			case kBallShotPowerup:
				g.rightSpecial->face = SATGetFace(kBallShotIndicatorID);	
				break;
				
			case kBallMagnetoPowerup:
				g.rightSpecial->face = SATGetFace(kBallMagnetoIndicatorID);	
				break;	
			
/*			case kHiSpeedPowerup:
				g.rightSpecial->face = SATGetFace(kHiSpeedIndicatorID);	
				break;	*/
		}
	}
}

void ActivatePowerup(SpritePtr ship)
{
	switch(ship->powerup)
	{
		case kRocketPowerup:
			ShootRocket(ship);
			break;
			
		case kMissilePowerup:
			ShootMissile(ship);
			break;
			
		case kBallShotPowerup:
			ShootBall(ship);
			break;
			
		case kBallMagnetoPowerup:
			BallMagneto(ship);
			break;
		
/*		case kHiSpeedPowerup:
			HiSpeed(ship);	
			break;		*/	
	}
	
	ship->powerup = 0;
	
	if(ship->task == &HandleShipLeft)
	{
		g.leftSpecial->face = nil;
	}else if(ship->task == &HandleShipRight)
	{
		g.rightSpecial->face = nil;
	}
}

void ReloadShields(SpritePtr ship)
{
SpritePtr	sparkler;

	ship->shields = kShipShields;
	DisplayShields();
	sparkler = SATNewSprite(kSparkKind, -3000, -3000, &SetupSparkler);
	sparkler->who = ship;
	sparkler->counter = 120;
	SATSoundPlay(g.shieldReloadSnd, 3, true);
}

/*void HiSpeed(ship)
{
	
}*/

