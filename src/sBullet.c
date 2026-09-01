#include "mySAT.h"
#include "ascent.h"

/*Constants*/

#define kBulletWidth		15			//Width of the bullet in pixels
#define kBulletHeight		3			//Height of the bullet in pixels
#define kBulletSpeed		15			//Speed of the bullets
#define kBulletMass			10			//Mass of a bullet (arbitrary)

/*Globals*/

extern Globals		g;

/*Code*/

pascal void SetupBullet(SpritePtr bullet)	
{
	SetRect(&bullet->hotRect, 0, 0, kBulletWidth, kBulletHeight);
	bullet->speed.v = (SATRand(10) - SATRand(10))/10.0;	//Make the bullet vertically imprecise
	bullet->task = &HandleBullet;
	bullet->hitTask = &HitTaskBullet;
	bullet->layer = kBulletLayer;
	bullet->mass = kBulletMass;
	bullet->pos.h = bullet->position.h;
	bullet->pos.v = bullet->position.v;
}					

pascal void HandleBullet(SpritePtr bullet)
{
SpritePtr explosion;

	bullet->pos.h += bullet->speed.h;	//It just goes on at kBulletSpeed pixels per frame	
	bullet->position.h = bullet->pos.h;
	
	if(bullet->position.h > gSAT.offSizeH)	
	{
		bullet->task = nil;
		explosion = SATNewSprite(kSmallExpKind, bullet->pos.h, bullet->pos.v, &SetupSmallExplosion);		
		explosion->speed.h = 0;
		explosion->position.h = gSAT.offSizeH - 6;
	}	
	if(bullet->position.h < 0)			
	{
		bullet->task = nil;
		explosion = SATNewSprite(kSmallExpKind, bullet->pos.h, bullet->pos.v, &SetupSmallExplosion);			
		explosion->speed.h = 0;			//It is stopped right there
		explosion->position.h = -6;		//The sprite is positioned so only half of the explosion is seen
	}	

	bullet->pos.v += bullet->speed.v;
	bullet->position.v = bullet->pos.v;
	
	if(bullet->position.v > gSAT.offSizeV)	
	{
		bullet->task = nil;
		explosion = SATNewSprite(kSmallExpKind, bullet->pos.h, bullet->pos.v, &SetupSmallExplosion);			
		explosion->speed.h = bullet->speed.h/3;		//the speed is reduced a little so the explosion lokes better
		explosion->speed.v = 0;
		explosion->position.v = gSAT.offSizeV - 6;
	}	
	if(bullet->position.v < 0)	
	{
		bullet->task = nil;
		explosion = SATNewSprite(kSmallExpKind, bullet->pos.h, bullet->pos.v, &SetupSmallExplosion);				
		explosion->speed.h = bullet->speed.h/3;
		explosion->speed.v = 0;
		explosion->position.v = -6;
	}		
}

pascal void HitTaskBullet(SpritePtr bullet, SpritePtr him)
{
SpritePtr explosion;

	if(him->kind == kShipKind)
	{
		him->shields -= (kBulletDamage + (SATRand(5)-SATRand(5)));		//Make a somewhat random amnount of damage
		SATSoundPlay(g.bulletHitSnd, 2, true);
		DisplayShields();
	}	
	
	if(him->kind == kShipKind || him->kind == kBallKind)				//If the bullet hit a ship or the ball
	{
		him->speed.h += (bullet->speed.h * bullet->mass)/him->mass;		//Add the cinetic energy of the bullet to the other object							
		him->speed.v += (bullet->speed.v * bullet->mass)/him->mass;
	}	
	
	bullet->task = nil;
	explosion = SATNewSprite(kSmallExpKind, bullet->pos.h, bullet->pos.v, &SetupSmallExplosion);
	explosion->speed.h = bullet->speed.h/3;								//The speed of the bullet is reduced so the impact animation looks better
}			

void ShootBullet(SpritePtr ship)
{	
SpritePtr bullet;

	if(ship->direction)								//true equals "right", false equals "left"
	{
		bullet = SATNewSprite(kBulletKind, ship->position.h + ship->hotRect.right + 1, ship->position.v + 22, &SetupBullet);
		bullet->speed.h = kBulletSpeed;				//Set the speed depending on which direction the bullet was shot
	}
	else
	{	
		bullet = SATNewSprite(kBulletKind, ship->position.h - kBulletWidth, ship->position.v + 22, &SetupBullet);
		bullet->speed.h = -kBulletSpeed;	
	}	
	
	if(ship->task == &HandleShipLeft)				//Determine the color and sound of the bullet
	{
		bullet->face = SATGetFace(kGreenBulletID);
		SATSoundPlay(g.greenShotSnd, 1, true);
	}	
	else if(ship->task == &HandleShipRight)
	{
		bullet->face = SATGetFace(kRedBulletID);
		SATSoundPlay(g.redShotSnd, 1, true);
	}	
}	