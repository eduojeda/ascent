#include <math.h>
#include "mySAT.h"
#include "ascent.h"

/*Constants*/

#define kShipFwPower		5			//Power increase per frame for the ship's engine when accelerating
#define kShipBwPower		5			//Power decrease per frame for the ship's engine when decelerating
#define kShipMaxForce		60			//Max power the ship's engine can put out in Newtons

/*Function Declarations*/

void LeftShipBoundsCheck(SpritePtr ship);

/*Globals*/

extern Globals		g;
Boolean				controlIsUp, shiftIsUp, tabIsUp;	//Indicates if the key was pressed down or not the previous frame

/*Code*/

pascal void SetupShipLeft(SpritePtr leftShip)
{
	leftShip->task = &HandleShipLeft;
	leftShip->hitTask = &HitTaskShipLeft;
	leftShip->layer = kShipLayer;
	leftShip->speed.h = 0;							//Init speeds and forces
	leftShip->speed.v = 0;
	leftShip->force.h = 0;
	leftShip->force.v = 0;
	leftShip->pos.h = leftShip->position.h;			//Set starting position on the physic's model variables
	leftShip->pos.v = leftShip->position.v;
	leftShip->viscosity = kShipViscosity;
	leftShip->direction = true;
	leftShip->mass = kShipMass;
	leftShip->shields = kShipShields;				//Init the shield's value
	leftShip->mode = kShipAliveMode;				//Start in player-controlled mode
	leftShip->face = g.leftShipFacingRight[3];		//Set the starting face
	g.leftShipMaxForce = kShipMaxForce;				//Init the engine power
	
	SetRect(&leftShip->hotRect, 2,2,45,25);
}

pascal void HandleShipLeft(SpritePtr leftShip)
{
double			varSpeedX, varSpeedY, 				//variation of speed
				viscosityX, viscosityY;				//viscosity of the medium adjusted to the speed of the ship (higher speed, higher viscosity force)
static short 	faceCounter = 0, counter = 0,
				sign, faceIndex;
SpritePtr		debris;

	switch(leftShip->mode)
	{
		case kShipAliveMode:
	
				
			leftShip->pos.h += leftShip->speed.h;				//update the mathematical position
			leftShip->pos.v += leftShip->speed.v;	
			leftShip->position.h = leftShip->pos.h;				//actually move the sprite of the ship
			leftShip->position.v = leftShip->pos.v;
		
			//Endgame checks
			if(g.leftShipScore == g.numPoints || g.rightShipScore == g.numPoints || g.rightShipLives <= 0)
				leftShip->mode = kShipDisabledMode;
				
			//Shields
			if(leftShip->shields < 0)								//The ship has died
			{
				debris = SATNewSprite(kDebrisKind, leftShip->position.h, leftShip->position.v, &SetupDebris1);
				debris->speed.h = (leftShip->speed.h + (SATRand(5)-SATRand(5)/10.0))/kTiempo;
				debris->speed.v = (leftShip->speed.v + (SATRand(3)-SATRand(3)/10.0))/kTiempo;
				debris = SATNewSprite(kDebrisKind, leftShip->position.h, leftShip->position.v, &SetupDebris2);
				debris->speed.h = (leftShip->speed.h + (SATRand(5)-SATRand(5)/10.0))/kTiempo;
				debris->speed.v = (leftShip->speed.v + (SATRand(3)-SATRand(3)/10.0))/kTiempo;
				
				if(g.ball != nil && g.ball->who == leftShip)
					ReleaseBall(g.ball->who);
				leftShip->speed.h = leftShip->speed.h/kTiempo;		//Adjust the speeds to the Ballistic Model's timescale
				leftShip->speed.v = leftShip->speed.v/kTiempo;							
				leftShip->mode = kShipDyingMode;
				g.leftShipLives--;									//Lose one life
				Explode(leftShip, kStdNumSmallExp, kStdNumBigExp, kStdSpeedSmallExp, kStdSpeedBigExp, g.explosionSnd);	//Make a fireball effect
				DisplayLives();
				g.leftSpecial->face = nil;							//Clear the specials indicator
			}	
			
			//Key Controls
				
			//Simple algorithm: if the key is pressed and the force is below the g.leftShipMaxForce limit,
			//rise the engine power in that direction. If the key is not pressed, reduce it
			
			if ((gLeftInput.left) && (leftShip->force.h > -g.leftShipMaxForce))
			{
				leftShip->force.h -= kShipFwPower;
			}else if(!gLeftInput.left && (leftShip->force.h < 0))
			{
				leftShip->force.h += kShipBwPower;	
			}	
			
			if ((gLeftInput.right) && (leftShip->force.h < g.leftShipMaxForce))
			{
				leftShip->force.h += kShipFwPower;
			}else if(!gLeftInput.right && (leftShip->force.h > 0))
			{
				leftShip->force.h -= kShipBwPower;	
			}	
		
			if ((gLeftInput.up) && (leftShip->force.v > -g.leftShipMaxForce))
			{
				leftShip->force.v -= kShipFwPower;
			}else if(!gLeftInput.up && (leftShip->force.v < 0))
			{
				leftShip->force.v += kShipBwPower;	
			}	
			
			if ((gLeftInput.down) && (leftShip->force.v < g.leftShipMaxForce))
			{
				leftShip->force.v += kShipFwPower;
			}else if(!gLeftInput.down && (leftShip->force.v > 0))
			{
				leftShip->force.v -= kShipBwPower;	
			}	
			
			if ((gLeftInput.shoot) && controlIsUp)		//Tests if the Control Key is pressed.
			{															//the controlIsUp flag is there to force the user to release the key and press again to trigger the control again
				controlIsUp = false;
				if(g.ball != nil && (g.ball->mode == kBallCaughtMode) && (g.ball->who == leftShip))	//the ball is being carried by this ship
				{
					SATSoundPlay(g.releaseSnd, 1, true);				//Play the sound
					ReleaseBall(leftShip);								//Let go the ball
				}	
				else
				{
					ShootBullet(leftShip);
				}	
			}else if(!gLeftInput.shoot)		//if control is not pressed			
			{
				controlIsUp = true;	
			}
			
			if ((gLeftInput.special) && tabIsUp)
			{	
				tabIsUp = false;
				if(leftShip->powerup == 0 && g.dirtyWordsMode)
				{
					SATSoundPlay(g.noAmmoSnd, 2, true);
				}							
				else
				{
					ActivatePowerup(leftShip);
				}	
			}else if(!gLeftInput.special)			//if tab is not pressed			
			{
				tabIsUp = true;	
			}
						
			if (gLeftInput.rotate && shiftIsUp)
			{
				shiftIsUp = false;
				leftShip->mode = kShipRotatingMode;			//go into rotation mode
			}else if(!gLeftInput.rotate)
			{
				shiftIsUp = true;	
			}	
			
			//Physics Model
			viscosityX = leftShip->speed.h * leftShip->viscosity;					//calculate the resistance of the enviroment to the advance of the ship Very uncientific code
			viscosityY = leftShip->speed.v * leftShip->viscosity;
			varSpeedX = 0.5 * ((leftShip->force.h - viscosityX)/leftShip->mass);	//similar to the Ballistic Model, without gravity
			varSpeedY = 0.5 * ((leftShip->force.v - viscosityY)/leftShip->mass);	//(although it may be easily added)
			leftShip->speed.h = leftShip->speed.h + varSpeedX;						//Adjust speeds
			leftShip->speed.v = leftShip->speed.v + varSpeedY;
		
			//Bound Collision
			LeftShipBoundsCheck(leftShip);
			
			//Face and HotRect Selection
			SelectFace(leftShip, g.leftShipFacingLeft, g.leftShipFacingRight);
			break;
		
		case kShipRotatingMode:
	
		//in mode 2, the ship still moves and can be killed, but i can't be controlled. It will make
		//the transition between the two directions by changing the face of the sprite
			
			//Shields
		
			if(leftShip->shields < 0)							
			{
				leftShip->speed.h = leftShip->speed.h/kTiempo;	
				leftShip->speed.v = leftShip->speed.v/kTiempo;							
				leftShip->mode = kShipDyingMode;				
				Explode(leftShip, kStdNumSmallExp, kStdNumBigExp, kStdSpeedSmallExp, kStdSpeedBigExp, g.explosionSnd);						
			}	
			
			//Movement
			
			leftShip->pos.h += leftShip->speed.h;		
			leftShip->pos.v += leftShip->speed.v;	
			leftShip->position.h = leftShip->pos.h;	
			leftShip->position.v = leftShip->pos.v;
			
			viscosityX = leftShip->speed.h * leftShip->viscosity;					
			viscosityY = leftShip->speed.v * leftShip->viscosity;
			
			varSpeedX = 0.5 * ((leftShip->force.h - viscosityX)/leftShip->mass);
			varSpeedY = 0.5 * ((leftShip->force.v - viscosityY)/leftShip->mass);
			
			leftShip->speed.h = leftShip->speed.h + varSpeedX;	
			leftShip->speed.v = leftShip->speed.v + varSpeedY;
			
			LeftShipBoundsCheck(leftShip);
			
			//Put engines on standby
			if(leftShip->force.h != 0)
			{
				sign = leftShip->force.h/fabs(leftShip->force.h);					//saves the sign of the force
				leftShip->force.h = fabs(leftShip->force.h) - kShipBwPower;			//reduce the engine power, the idea is that the ship's engines are poining upwards (remember the graphic rotation sequence only looks good if the engines are pointing upward)
				leftShip->force.h *= sign;											//Restores the sign
				SelectFace(leftShip, g.leftShipFacingLeft, g.leftShipFacingRight);	//Runs the face selection routine
				break;
			}	
			
			//Rotate
			if(faceIndex == 3 && counter == 3)
			{
				leftShip->mode = kShipAliveMode;
				leftShip->direction = !leftShip->direction;		//change the direction the ship is facing		
				faceIndex = 0;
				counter = 0;
			}	
			if(leftShip->direction && counter == 3 && leftShip->mode == kShipRotatingMode)
			{
				leftShip->face = g.leftShipRotating[2 - faceIndex];
				faceIndex++;
				counter = 0;
			}	
			if(!leftShip->direction && counter == 3 && leftShip->mode == kShipRotatingMode)
			{
				leftShip->face = g.leftShipRotating[faceIndex];
				faceIndex++;
				counter = 0;
			}	
			counter++;			
			break;
			
		case kShipDyingMode:
	//In mode 3, the ship is handled by the same ballistic model as the ball. If it hits a horizontal
	//bound or the ceiling, it just bounces; if it hits the floor, it Explodes and dies in a neat ball of flame.
	//While falling, it creates small explosions and smoke every 2 frames
			BallisticModel(leftShip, g.gravityAccel);
			
			if(leftShip->pos.h >= gSAT.offSizeH - leftShip->hotRect.right || leftShip->pos.h <= 0)
			{
				leftShip->speed.h = leftShip->speed.h * -1;
			}
			if(leftShip->pos.v <= 0)
			{
				leftShip->speed.v = leftShip->speed.v * -1;
			}
			if(leftShip->pos.v >= gSAT.offSizeV - leftShip->hotRect.bottom)
			{
				Explode(leftShip, kStdNumSmallExp, kStdNumBigExp, kStdSpeedSmallExp, kStdSpeedBigExp, g.explosionSnd);
				leftShip->task = nil;
				if(g.leftShipLives > 0)					//Only reincarnate if it has lives left
					g.leftShipReincarnating = true;
			}	
	
			leftShip->position.h = leftShip->pos.h;	
			leftShip->position.v = leftShip->pos.v;
			
			if(counter == 2)
			{
				SATNewSprite(kSmallExpKind, (leftShip->position.h - 3) + leftShip->hotRect.right/2 + (SATRand(20)-SATRand(20)), (leftShip->position.v - 5) + leftShip->hotRect.bottom/2 + (SATRand(20)-SATRand(20)), &SetupSmallExplosion);
				SATNewSprite(kSmokeKind, (leftShip->position.h - 3) + leftShip->hotRect.right/2 + (SATRand(20)-SATRand(20)), (leftShip->position.v - 15) + (SATRand(15)-SATRand(15)), &SetupSmoke);
				counter = 0;
			}	
			counter++;
			
			leftShip->face = g.bodyDebrisFaces[faceCounter/4];
			faceCounter++;
			if(faceCounter == 27)
				faceCounter = 0;
			
			break;	
		
		case kShipDisabledMode:
	//In mode 4, the ship goes into standby (puts its engines on zero and stops) and recieves no
	//input from the player. It can still be killed.
	
			//Shields
			if(leftShip->shields < 0)							
			{
				leftShip->speed.h = leftShip->speed.h/kTiempo;	
				leftShip->speed.v = leftShip->speed.v/kTiempo;							
				leftShip->mode = kShipDyingMode;				
				Explode(leftShip, kStdNumSmallExp, kStdNumBigExp, kStdSpeedSmallExp, kStdSpeedBigExp, g.explosionSnd);						
			}	
			
			//Movement
			leftShip->pos.h += leftShip->speed.h;		
			leftShip->pos.v += leftShip->speed.v;	
			leftShip->position.h = leftShip->pos.h;	
			leftShip->position.v = leftShip->pos.v;
			
			viscosityX = leftShip->speed.h * leftShip->viscosity;					
			viscosityY = leftShip->speed.v * leftShip->viscosity;
			
			varSpeedX = 0.5 * ((leftShip->force.h - viscosityX)/leftShip->mass);
			varSpeedY = 0.5 * ((leftShip->force.v - viscosityY)/leftShip->mass);
			
			leftShip->speed.h = leftShip->speed.h + varSpeedX;	
			leftShip->speed.v = leftShip->speed.v + varSpeedY;
			
			LeftShipBoundsCheck(leftShip);
			
			//Put engines on standby
			if(leftShip->force.h != 0)
			{
				sign = leftShip->force.h/fabs(leftShip->force.h);					//saves the sign of the force
				leftShip->force.h = fabs(leftShip->force.h) - kShipBwPower;			//reduce the engine power
				leftShip->force.h *= sign;											//Restores the sign
				SelectFace(leftShip, g.leftShipFacingLeft, g.leftShipFacingRight);	//Runs the face selection routine
			}
			if(leftShip->force.v != 0)
			{
				sign = leftShip->force.v/fabs(leftShip->force.v);
				leftShip->force.v = fabs(leftShip->force.v) - kShipBwPower;
				leftShip->force.v *= sign;
			}	
			break;
	}			
}

pascal void HitTaskShipLeft(SpritePtr leftShip, SpritePtr him)
{
	if(him->task == &HandleBall)				 	//if leftShip collided with the ball
	{
		leftShip->mass = kShipMass + kBallMass;		//add the ball's mass and viscosity factor to the ship's mass and viscosity factor
		leftShip->viscosity = kShipViscosity + kBallViscosity;
	}	
	if((him->kind == kBulletKind || him->kind == kRocketKind || him->kind == kMissileKind) && g.ball != nil && g.ball->mode == kBallCaughtMode)
	{
		ReleaseBall(leftShip);						//let the ball go
	}	
/*	if(him->task == &HandleShipRight)
	{
		leftShip->speed.h = leftShip->speed.h*((leftShip->mass - him->mass)/(leftShip->mass + him->mass)) + him->speed.h*((2*him->mass)/(leftShip->mass + him->mass));
		leftShip->speed.v = leftShip->speed.v*((leftShip->mass - him->mass)/(leftShip->mass + him->mass)) + him->speed.v*((2*him->mass)/(leftShip->mass + him->mass));
	
		him->speed.h = him->speed.h*((him->mass - leftShip->mass)/(him->mass + leftShip->mass)) + leftShip->speed.h*((2*leftShip->mass)/(him->mass + leftShip->mass));
		him->speed.v = him->speed.v*((him->mass - leftShip->mass)/(him->mass + leftShip->mass)) + leftShip->speed.v*((2*leftShip->mass)/(him->mass + leftShip->mass)); d
	}*/
}	

void SelectFace(SpritePtr ship, FacePtr shipFacingLeft[], FacePtr shipFacingRight[])
{
short		faceIndex;				//array element that contains the appropiate face of the ship

	//this evaluates the engine power of the ships and selects a graphic for it depending
	//on the range it falls on. (Note:"g.leftShipMaxForce*0.8" should be read as "80% of g.leftShipMaxForce)
	//it also sets the hotRect of the sprite to match the current face
	
	if(ship->direction)
	{
		if(ship->force.h <= -(g.leftShipMaxForce*0.8))
		{
			faceIndex = 0;
			SetRect(&ship->hotRect, 1,4,42,25);
		}else if (ship->force.h <= -(g.leftShipMaxForce*0.35))
		{
			faceIndex = 1;
			SetRect(&ship->hotRect, 1,2,42,25);
		}else if (ship->force.h <= -(g.leftShipMaxForce*0.1))
		{
			faceIndex = 2;
			SetRect(&ship->hotRect, 2,2,42,25);
		}else if (ship->force.h >= -(g.leftShipMaxForce*0.1) && ship->force.h <= (g.leftShipMaxForce*0.1))
		{
			faceIndex = 3;
			SetRect(&ship->hotRect, 2,2,42,25);
		}else if (ship->force.h <= (g.leftShipMaxForce*0.35))
		{
			faceIndex = 4;
			SetRect(&ship->hotRect, 2,1,42,25);
		}else if (ship->force.h <= (g.leftShipMaxForce*0.65))
		{
			faceIndex = 5;
			SetRect(&ship->hotRect, 2,2,42,25);
		}else if (ship->force.h <= (g.leftShipMaxForce*0.8))
		{
			faceIndex = 6;
			SetRect(&ship->hotRect, 2,4,42,25);
		}else if (ship->force.h <= (g.leftShipMaxForce*1.1))
		{
			faceIndex = 7;
			SetRect(&ship->hotRect, 2,6,42,25);
		}
	ship->face = shipFacingRight[faceIndex];
	}
	else		
	{
		if(ship->force.h <= -(g.leftShipMaxForce*0.8))
		{
			faceIndex = 7;
			SetRect(&ship->hotRect, 0,6,42,25);
		}else if(ship->force.h <= -(g.leftShipMaxForce*0.65))
		{
			faceIndex = 6;
			SetRect(&ship->hotRect, 0,4,42,25);
		}else if (ship->force.h <= -(g.leftShipMaxForce*0.35))
		{
			faceIndex = 5;
			SetRect(&ship->hotRect, 0,2,42,25);
		}else if (ship->force.h <= -(g.leftShipMaxForce*0.1))
		{
			faceIndex = 4;
			SetRect(&ship->hotRect, 0,1,42,25);
		}else if (ship->force.h >= -(g.leftShipMaxForce*0.1) && ship->force.h <= (g.leftShipMaxForce*0.1))
		{
			faceIndex = 3;
			SetRect(&ship->hotRect, 0,2,42,25);
		}else if (ship->force.h <= (g.leftShipMaxForce*0.35))
		{
			faceIndex = 2;
			SetRect(&ship->hotRect, 0,2,42,25);
		}else if (ship->force.h <= (g.leftShipMaxForce*0.8))
		{
			faceIndex = 1;
			SetRect(&ship->hotRect, 0,2,42,25);
		}else if (ship->force.h <= (g.leftShipMaxForce*1.1))
		{
			faceIndex = 0;
			SetRect(&ship->hotRect, 0,4,42,25);
		}
	ship->face = shipFacingLeft[faceIndex];
	}
}			

void LeftShipBoundsCheck(SpritePtr ship)
{
		//A different version of the ball's Bounce algorithm, without friction (since the
		//ships won't be rolling on the floor (see Bounce() for comments)
		//This one checks for the limits
		
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
			
		if((ship->position.h <= 0) && (ship->speed.h < 0))
		{
			ship->speed.h = ship->speed.h * -1 * kShipBounceLoss;
			ship->position.h = 0;
		}
		
		if((ship->position.h >= gSAT.offSizeH - ship->hotRect.right - kLimits) && (g.bottomRightLG->mode == kLimitGAliveMode) && (ship->speed.h > 0)) //Can't get past the green limit unless its generator is destroyed
		{
			if(g.ball != nil && g.ball->who == ship && g.ball->mode == kBallCaughtMode)
				ReleaseBall(ship);										//let the ball go
			
			ship->shields -= (kLimitDamage + (SATRand(5)-SATRand(5)));	//Make a somewhat random amount of damage
			if(ship->shields > 0)										//Check if it has just died due to the punch, if it did, we don't want to get kicked back
			{
				ship->speed.h = ship->speed.h * -1 * kShipBounceLoss;
				ship->position.h = gSAT.offSizeH - ship->hotRect.right - kLimits;
				ship->speed.h -= kExpulsionForce/ship->mass;
			}	
	
			DisplayShields();
			SATSoundPlay(g.hitLaserSnd, 2, true);								//Play the sound
		}
		else if((ship->position.h >= gSAT.offSizeH - ship->hotRect.right) && (ship->speed.h > 0))			//Generator has been destroyed, can get past the limit
		{
			ship->speed.h = ship->speed.h * -1 * kShipBounceLoss;
			ship->position.h = gSAT.offSizeH - ship->hotRect.right;
		}	
}		

void LeftShipReincarnationDelay()						//This routine does various things at different times of the reincarnation delay
{
	if(g.LSReincC == 0)
		SATSoundPlay(g.closeBaseSnd, 3, true);	
		
	if(g.LSReincC <= 21)											//Changes the face of the base every 3 frames
	{
		g.blueBase->face = g.blueBaseFaces[g.LSReincC/3];
	}
	
	if(g.LSReincC == 150 && !g.gameStarting && g.dirtyWordsMode)	//Bitch a little ;D
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
	
	if(g.LSReincC == kReincarnDelay-50)					//Here we respawn the ship
	{
		g.leftShip = SATNewSprite(kShipKind, 16, gSAT.offSizeV - kShipHeight, &SetupShipLeft);
		g.leftShip->mode = kShipDisabledMode;	
	}
	
	if(g.LSReincC == kReincarnDelay-21)
		SATSoundPlay(g.openBaseSnd, 3, true);
	
	if(g.LSReincC >= kReincarnDelay-21)					//Changes the face of the base every 3 frames
	{
		g.blueBase->face = g.blueBaseFaces[(kReincarnDelay-g.LSReincC)/3];
	}

	if(g.LSReincC == kReincarnDelay)						//Finally, set a few variables right before the player regains control
	{
	/*	if(g.leftShipLives == 0)
			DisplayWinner(g.leftShip);*/
		g.LSReincC = 0;
		g.leftShipReincarnating = false;
		g.leftShip->speed.v = -15;				//This gives an initial upward push to the ships
		g.leftShip->mode = kShipAliveMode;
		DisplayShields();
	}g.LSReincC++;
}	
