#include "mySAT.h"
#include "ascent.h"

/*Globals*/

extern Globals		g;

/*Code*/

pascal void SetupBigExplosion(SpritePtr exp)
{
	exp->task = &HandleBigExplosion;
	exp->layer = kExpLayer;
	exp->counter = 0;
	exp->speed.h = 0;
	exp->speed.v = 0;
	SetRect(&exp->hotRect, 0,0,0,0);
}	

pascal void HandleBigExplosion(SpritePtr exp)
{
	exp->face = g.bigExplosionFaces[exp->counter/3];							
	exp->counter++;														
	
	if(exp->counter == 24)
		exp->task = nil;

	exp->position.h += exp->speed.h;
	exp->position.v += exp->speed.v;
}	

pascal void SetupSmallExplosion(SpritePtr exp)
{
	exp->task = &HandleSmallExplosion;
	exp->layer = kExpLayer;
	exp->counter = 0;
	exp->speed.h = 0;
	exp->speed.v = 0;
	SetRect(&exp->hotRect, 0,0,0,0);
}	

pascal void HandleSmallExplosion(SpritePtr exp)
{
	exp->face = g.smallExplosionFaces[exp->counter/2];							
	exp->counter++;														
	
	if(exp->counter == 16)
	{
		exp->task = nil;
	}	
	
	exp->position.h += exp->speed.h;
	exp->position.v += exp->speed.v;
}	

void Explode(SpritePtr object, short numSmallExp, short numBigExp, short smallExpSpeed, short bigExpSpeed, Handle explosionSnd)
{
SpritePtr	exp;
short		i,j;

	for(i = 0 ; i <= numSmallExp ; i++)									//Creates numSmallExp small explosions
	{	
		exp = SATNewSprite(kSmallExpKind, object->position.h + object->hotRect.right/2 + (SATRand(25)-SATRand(25)), object->position.v + object->hotRect.bottom/2 + (SATRand(20)-SATRand(20)), &SetupSmallExplosion);
		exp->speed.h = SATRand(smallExpSpeed)-SATRand(smallExpSpeed);	//Gives the explosion a random direction of movement
		exp->speed.v = SATRand(smallExpSpeed)-SATRand(smallExpSpeed);	
	}	
	
	for(j = 0 ; j <= numBigExp ; j++)
	{
		exp = SATNewSprite(kBigExpKind, object->position.h + object->hotRect.right/2 + (SATRand(5)-SATRand(5)), object->position.v + object->hotRect.bottom/2 + (SATRand(5)-SATRand(5)), &SetupBigExplosion);
		exp->speed.h = SATRand(bigExpSpeed)-SATRand(bigExpSpeed);
		exp->speed.v = SATRand(bigExpSpeed)-SATRand(bigExpSpeed);
	}
	SATSoundPlay(explosionSnd, 3, true);								
	
	DisplayLives();
}

void ExplodeAtCoords(int xPos, int yPos, short numSmallExp, short numBigExp, short smallExpSpeed, short bigExpSpeed)
{
SpritePtr	exp;
short 		i,j;

	for(i = 0 ; i <= numSmallExp ; i++)									//Creates numSmallExp small explosions
	{	
		exp = SATNewSprite(kSmallExpKind, xPos + (SATRand(25)-SATRand(25)), yPos + (SATRand(20)-SATRand(20)), &SetupSmallExplosion);
		exp->speed.h = SATRand(smallExpSpeed)-SATRand(smallExpSpeed);	//Gives the explosion a random direction of movement
		exp->speed.v = SATRand(smallExpSpeed)-SATRand(smallExpSpeed);	
	}	
	
	for(j = 0 ; j <= numBigExp ; j++)
	{
		exp = SATNewSprite(kBigExpKind, xPos + (SATRand(5)-SATRand(5)), yPos + (SATRand(5)-SATRand(5)), &SetupBigExplosion);
		exp->speed.h = SATRand(bigExpSpeed)-SATRand(bigExpSpeed);
		exp->speed.v = SATRand(bigExpSpeed)-SATRand(bigExpSpeed);
	}
	SATSoundPlay(g.explosionSnd, 3, true);
}