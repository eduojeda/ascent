#include <math.h>
#include "mySAT.h"
#include "ascent.h"

/*Constants*/

#define kShipFwPower		5
#define kShipBwPower		5
#define kShipMaxForce		60

/*Function Declarations*/

void RightShipBoundsCheck(SpritePtr ship);

/*Globals*/

extern Globals		g;
extern Controls		RSKeys;
Boolean				spaceIsUp, commandIsUp, optionIsUp;

/*Code*/

pascal void SetupShipRight(SpritePtr rightShip)
{
	rightShip->task = &HandleShipRight;
	rightShip->hitTask = &HitTaskShipRight;
	rightShip->layer = kShipLayer;
	rightShip->speed.h = 0;
	rightShip->speed.v = 0;
	rightShip->force.h = 0;
	rightShip->force.v = 0;
	rightShip->pos.h = rightShip->position.h;
	rightShip->pos.v = rightShip->position.v;
	rightShip->viscosity = kShipViscosity;
	rightShip->direction = false;
	rightShip->mass = kShipMass;
	rightShip->shields = kShipShields;
	rightShip->mode = kShipAliveMode;
	rightShip->face = g.rightShipFacingLeft[3];
	g.rightShipMaxForce = kShipMaxForce;
	
	SetRect(&rightShip->hotRect, 2,2,45,25);
}

pascal void HandleShipRight(SpritePtr rightShip)
{
double			varSpeedX, varSpeedY, viscosityX, viscosityY;
static short 	counter = 0, sign, faceIndex, faceCounter = 0;
KeyMap			theKeys;
SpritePtr		debris;
	
	switch(rightShip->mode)
	{
		case kShipAliveMode:
		
			GetKeys(theKeys);
				
			rightShip->pos.h += rightShip->speed.h;
			rightShip->pos.v += rightShip->speed.v;	
			rightShip->position.h = rightShip->pos.h;
			rightShip->position.v = rightShip->pos.v;
				
			//Endgame check
			if(g.leftShipScore == g.numPoints || g.rightShipScore == g.numPoints || g.leftShipLives <= 0)
				rightShip->mode = kShipDisabledMode;
			
			//Shields
			if(rightShip->shields < 0)
			{
				debris = SATNewSprite(kDebrisKind, rightShip->position.h, rightShip->position.v, &SetupDebris1);
				debris->speed.h = (rightShip->speed.h + (SATRand(5)-SATRand(5)/10.0))/kTiempo;
				debris->speed.v = (rightShip->speed.v + (SATRand(3)-SATRand(3)/10.0))/kTiempo;
				debris = SATNewSprite(kDebrisKind, rightShip->position.h, rightShip->position.v, &SetupDebris2);
				debris->speed.h = (rightShip->speed.h + (SATRand(5)-SATRand(5)/10.0))/kTiempo;
				debris->speed.v = (rightShip->speed.v + (SATRand(3)-SATRand(3)/10.0))/kTiempo;
				
				if(g.ball != nil && g.ball->who == rightShip)
					ReleaseBall(g.ball->who);
				rightShip->speed.h = rightShip->speed.h/kTiempo;
				rightShip->speed.v = rightShip->speed.v/kTiempo;							
				rightShip->mode = kShipDyingMode;
				g.rightShipLives--;
				Explode(rightShip, kStdNumSmallExp, kStdNumBigExp, kStdSpeedSmallExp, kStdSpeedBigExp, g.explosionSnd);	
				DisplayLives();	
				g.rightSpecial->face = nil;
			}	
				
			//Key Controls
			if ((BitTst(&theKeys, RSKeys.left)) && (rightShip->force.h > -g.rightShipMaxForce))
			{
				rightShip->force.h -= kShipFwPower;
			}else if(!BitTst(&theKeys, RSKeys.left) && (rightShip->force.h < 0))
			{
				rightShip->force.h += kShipBwPower;	
			}	
			
			if ((BitTst(&theKeys, RSKeys.right)) && (rightShip->force.h < g.rightShipMaxForce))
			{
				rightShip->force.h += kShipFwPower;
			}else if(!BitTst(&theKeys, RSKeys.right) && (rightShip->force.h > 0))
			{
				rightShip->force.h -= kShipBwPower;	
			}	
		
			if ((BitTst(&theKeys, RSKeys.up)) && (rightShip->force.v > -g.rightShipMaxForce))
			{
				rightShip->force.v -= kShipFwPower;
			}else if(!BitTst(&theKeys, RSKeys.up) && (rightShip->force.v < 0))
			{
				rightShip->force.v += kShipBwPower;	
			}	
			
			if ((BitTst(&theKeys, RSKeys.down)) && (rightShip->force.v < g.rightShipMaxForce))
			{
				rightShip->force.v += kShipFwPower;
			}else if(!BitTst(&theKeys, RSKeys.down) && (rightShip->force.v > 0))
			{
				rightShip->force.v -= kShipBwPower;	
			}	
				
			if (BitTst(&theKeys, RSKeys.shoot) && spaceIsUp)
			{
				spaceIsUp = false;
				if(g.ball != nil && (g.ball->mode == kBallCaughtMode) && (g.ball->who == rightShip))
				{
					SATSoundPlay(g.releaseSnd, 1, true);
					ReleaseBall(rightShip);	
				}	
				else
				{
					ShootBullet(rightShip);
				}	
			}else if(!BitTst(&theKeys, RSKeys.shoot))
			{
				spaceIsUp = true;	
			}
			
			if ((BitTst(&theKeys, RSKeys.special)) && optionIsUp)
			{	
				optionIsUp = false;
				if(rightShip->powerup == 0 && g.dirtyWordsMode)
				{
					SATSoundPlay(g.noAmmoSnd, 2, true);
				}							
				else
				{
					ActivatePowerup(rightShip);
				}	
			}else if(!BitTst(&theKeys, RSKeys.special))		
			{
				optionIsUp = true;	
			}
						
			if (BitTst(&theKeys, RSKeys.rotate) && commandIsUp)
			{
				commandIsUp = false;
				rightShip->mode = kShipRotatingMode;
			}else if(!BitTst(&theKeys, RSKeys.rotate))
			{
				commandIsUp = true;	
			}	
			
			//Physics Model
			viscosityX = rightShip->speed.h * rightShip->viscosity;
			viscosityY = rightShip->speed.v * rightShip->viscosity;
			varSpeedX = 0.5 * ((rightShip->force.h - viscosityX)/rightShip->mass);
			varSpeedY = 0.5 * ((rightShip->force.v - viscosityY)/rightShip->mass);
			rightShip->speed.h = rightShip->speed.h + varSpeedX;
			rightShip->speed.v = rightShip->speed.v + varSpeedY;
			
			//Bounds Collision
			
			RightShipBoundsCheck(rightShip);
			
			//Face and HotRect Selection
			SelectFace(rightShip, g.rightShipFacingLeft, g.rightShipFacingRight);
			break;
				
		case kShipRotatingMode:

			//Shields
			if(rightShip->shields < 0)							
			{
				rightShip->speed.h = rightShip->speed.h/kTiempo;	
				rightShip->speed.v = rightShip->speed.v/kTiempo;							
				rightShip->mode = kShipDyingMode;				
				Explode(rightShip, kStdNumSmallExp, kStdNumBigExp, kStdSpeedSmallExp, kStdSpeedBigExp, g.explosionSnd);						
			}
			
			//Movement
			rightShip->pos.h += rightShip->speed.h;		
			rightShip->pos.v += rightShip->speed.v;	
			rightShip->position.h = rightShip->pos.h;	
			rightShip->position.v = rightShip->pos.v;
			
			viscosityX = rightShip->speed.h * rightShip->viscosity;					
			viscosityY = rightShip->speed.v * rightShip->viscosity;
			
			varSpeedX = 0.5 * ((rightShip->force.h - viscosityX)/rightShip->mass);
			varSpeedY = 0.5 * ((rightShip->force.v - viscosityY)/rightShip->mass);
			
			rightShip->speed.h = rightShip->speed.h + varSpeedX;	
			rightShip->speed.v = rightShip->speed.v + varSpeedY;
						
			RightShipBoundsCheck(rightShip);
									
			//Put engines on standby
			
			if(rightShip->force.h != 0)
			{
				sign = rightShip->force.h/fabs(rightShip->force.h);
				rightShip->force.h = fabs(rightShip->force.h) - kShipBwPower;
				rightShip->force.h *= sign;										
				SelectFace(rightShip, g.rightShipFacingLeft, g.rightShipFacingRight);
				break;
			}	
			
			//Rotate

			if(faceIndex == 3 && counter == 3)
			{
				rightShip->mode = kShipAliveMode;
				rightShip->direction = !rightShip->direction;
				faceIndex = 0;
				counter = 0;
			}	
			if(rightShip->direction && counter == 3 && rightShip->mode == kShipRotatingMode)
			{
				rightShip->face = g.rightShipRotating[2 - faceIndex];
				faceIndex++;
				counter = 0;
			}	
			if(!rightShip->direction && counter == 3 && rightShip->mode == kShipRotatingMode)
			{
				rightShip->face = g.rightShipRotating[faceIndex];
				faceIndex++;
				counter = 0;
			}	
			counter++;			
			break;
				
		case kShipDyingMode:

			BallisticModel(rightShip, g.gravityAccel);
			
			if(rightShip->pos.h > gSAT.offSizeH - rightShip->hotRect.right || rightShip->pos.h < 0)
			{
				rightShip->speed.h = rightShip->speed.h * -1;
			}
			if(rightShip->pos.v > gSAT.offSizeV - rightShip->hotRect.bottom || rightShip->pos.v < 0)
			{
				Explode(rightShip, kStdNumSmallExp, kStdNumBigExp, kStdSpeedSmallExp, kStdSpeedBigExp, g.explosionSnd);
				rightShip->task = nil;
				if(g.rightShipLives > 0)
					g.rightShipReincarnating = true;
			}	

			rightShip->position.h = rightShip->pos.h;	
			rightShip->position.v = rightShip->pos.v;
			
			if(counter == 2)
			{
				SATNewSprite(kSmallExpKind, (rightShip->position.h - 3) + rightShip->hotRect.right/2 + (SATRand(20)-SATRand(20)), (rightShip->position.v - 5) + rightShip->hotRect.bottom/2 + (SATRand(20)-SATRand(20)), &SetupSmallExplosion);
				SATNewSprite(kSmokeKind, (rightShip->position.h - 3) + rightShip->hotRect.right/2 + (SATRand(20)-SATRand(20)), (rightShip->position.v - 15) + (SATRand(15)-SATRand(15)), &SetupSmoke);
				counter = 0;
			}	
			counter++;
			
			rightShip->face = g.bodyDebrisFaces[faceCounter/4];
			faceCounter++;
			if(faceCounter == 27)
				faceCounter = 0;
				
			break;
			
		case kShipDisabledMode:
			
			//Shields
			if(rightShip->shields < 0)							
			{
				rightShip->speed.h = rightShip->speed.h/kTiempo;	
				rightShip->speed.v = rightShip->speed.v/kTiempo;							
				rightShip->mode = kShipDyingMode;				
				Explode(rightShip, kStdNumSmallExp, kStdNumBigExp, kStdSpeedSmallExp, kStdSpeedBigExp, g.explosionSnd);						
			}
			
			//Movement
			rightShip->pos.h += rightShip->speed.h;		
			rightShip->pos.v += rightShip->speed.v;	
			rightShip->position.h = rightShip->pos.h;	
			rightShip->position.v = rightShip->pos.v;
			
			viscosityX = rightShip->speed.h * rightShip->viscosity;					
			viscosityY = rightShip->speed.v * rightShip->viscosity;
			
			varSpeedX = 0.5 * ((rightShip->force.h - viscosityX)/rightShip->mass);
			varSpeedY = 0.5 * ((rightShip->force.v - viscosityY)/rightShip->mass);
			
			rightShip->speed.h = rightShip->speed.h + varSpeedX;	
			rightShip->speed.v = rightShip->speed.v + varSpeedY;
						
			RightShipBoundsCheck(rightShip);
									
			//Put engines on standby
			
			if(rightShip->force.h != 0)
			{
				sign = rightShip->force.h/fabs(rightShip->force.h);
				rightShip->force.h = fabs(rightShip->force.h) - kShipBwPower;
				rightShip->force.h *= sign;										
				SelectFace(rightShip, g.rightShipFacingLeft, g.rightShipFacingRight);
			}
			if(rightShip->force.v != 0)
			{
				sign = rightShip->force.v/fabs(rightShip->force.v);
				rightShip->force.v = fabs(rightShip->force.v) - kShipBwPower;
				rightShip->force.v *= sign;
			}	
			break;
	}	
}

pascal void HitTaskShipRight(SpritePtr rightShip, SpritePtr him)
{
	if(him->task == &HandleBall)
	{
		rightShip->mass = kShipMass + kBallMass;
		rightShip->viscosity = kShipViscosity + kBallViscosity;
	}	
	if((him->kind == kBulletKind || him->kind == kRocketKind || him->kind == kMissileKind) && g.ball != nil && g.ball->mode == kBallCaughtMode)
	{
		ReleaseBall(rightShip);
	}	
}	

void RightShipBoundsCheck(SpritePtr ship)
{
		if((ship->position.v <= 0) && (ship->speed.v < 0))
		{
				ship->speed.v = ship->speed.v * -1 * kShipBounceLoss;
				ship->position.v = 0;
		}
		
		if((ship->position.v >= gSAT.offSizeV - ship->hotRect.bottom - (kBallDiameter-1)) && (ship->speed.v > 0))
		{
			ship->speed.v = ship->speed.v * -1 * kShipBounceLoss;
			ship->position.v = gSAT.offSizeV - ship->hotRect.bottom - (kBallDiameter-1);
		}
		
		if((ship->position.h <= kLimits) && (g.bottomLeftLG->mode == kLimitGAliveMode) && (ship->speed.h < 0))
		{
			if(g.ball != nil && g.ball->who == ship && g.ball->mode == kBallCaughtMode)
				ReleaseBall(ship);							
				
			ship->shields -= (kLimitDamage + (SATRand(5)-SATRand(5)));
			if(ship->shields > 0)
			{
				ship->speed.h = ship->speed.h * -1 * kShipBounceLoss;
				ship->position.h = kLimits;
				ship->speed.h += kExpulsionForce/ship->mass;
			}	
			DisplayShields();
			SATSoundPlay(g.hitLaserSnd, 2, true);		
		}
		else if((ship->position.h <= 0) && (ship->speed.h < 0))
		{
			ship->speed.h = ship->speed.h * -1 * kShipBounceLoss;
			ship->position.h = 0;
		}
			
		if((ship->position.h >= gSAT.offSizeH - ship->hotRect.right) && (ship->speed.h > 0))
		{
				ship->speed.h = ship->speed.h * -1 * kShipBounceLoss;
				ship->position.h = gSAT.offSizeH - ship->hotRect.right;
		}	
}

void RightShipReincarnationDelay()
{
	if(g.RSReincC == 0)
		SATSoundPlay(g.closeBaseSnd, 3, true);
		
	if(g.RSReincC <= 21)
	{
		g.redBase->face = g.redBaseFaces[g.RSReincC/3];
	}
	
	if(g.RSReincC == 150 && !g.gameStarting && g.dirtyWordsMode)
	{
		switch(SATRand(6))
		{
			case 0:
				SATSoundPlay(g.youFuckerSnd, 2, true);
				break;	
			case 1:	
				SATSoundPlay(g.thisSuxSnd, 2, true);
				break;	
			case 2:
				SATSoundPlay(g.ohSnd, 2, true);
				break;
			case 3:
				SATSoundPlay(g.shoveSnd, 2, true);
				break;	
			case 4:
				SATSoundPlay(g.smackYouSnd, 2, true);
				break;	
			case 5:
				SATSoundPlay(g.notCoolSnd, 2, true);
				break;					
		}	
	}
	
	if(g.RSReincC == kReincarnDelay-30)
	{
		g.rightShip = SATNewSprite(kShipKind, gSAT.offSizeH - kShipWidth - 16, gSAT.offSizeV - kShipHeight, &SetupShipRight);
		g.rightShip->mode = kShipDisabledMode;
	}
	
	if(g.RSReincC == kReincarnDelay-21)
		SATSoundPlay(g.openBaseSnd, 3, true);	
	
	if(g.RSReincC >= kReincarnDelay-21)
	{
		g.redBase->face = g.redBaseFaces[(kReincarnDelay-g.RSReincC)/3];
	}
	
	if(g.RSReincC == kReincarnDelay)
	{
	/*	if(g.rightShipLives == 0)
			DisplayWinner(g.rightShip);*/
		g.RSReincC = 0;
		g.rightShipReincarnating = false;
		g.rightShip->speed.v = -15;
		g.rightShip->mode = kShipAliveMode;
		DisplayShields();
	}g.RSReincC++;
}				