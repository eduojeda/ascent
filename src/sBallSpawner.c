#include "mySAT.h"
#include "ascent.h"

/*Globals*/

extern Globals		g;

/*Code*/

pascal void SetupBallSpawner(SpritePtr spawner)
{
	spawner->task = &HandleBallSpawner;
	spawner->layer = kBallSpawnerLayer;
	spawner->face = g.ballSpawnerFaces[0];
	SetRect(&spawner->hotRect, 0,0,0,0);
	spawner->counter = 0;
}

pascal void HandleBallSpawner(SpritePtr spawner)
{
	switch(spawner->mode)
	{	
		case kSpawnerSpawningMode:
			if(spawner->counter == 0)
				SATSoundPlay(g.ballSpawnerClosingSnd, 3, true);
				
			if(spawner->counter <= 10)
			{
				g.spawner->face = g.ballSpawnerFaces[spawner->counter/2];
			}
			
			if(spawner->counter == 20)
			{
				g.ball = SATNewSprite(kBallKind, (gSAT.offSizeH/2)-(kBallDiameter/2), -kBallDiameter, &SetupBall); 
				g.ball->speed.h = 0;
				g.ball->speed.v = 0;
				InitVariables(g.ball);
			}
			
			if(spawner->counter == 60)
				SATSoundPlay(g.ballSpawnerOpeningSnd, 3, true);
			
			if(spawner->counter >= 60)
			{	
				g.spawner->face = g.ballSpawnerFaces[(70-spawner->counter)/2];
			}
			
			if(spawner->counter == 70)
			{
				spawner->mode = kSpawnerIdleMode;
				spawner->counter = 0;
			}
			spawner->counter++;
			break;
			
		case kSpawnerIdleMode:
			break;
	}						
}	