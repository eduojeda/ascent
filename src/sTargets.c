#include "mySAT.h"
#include "ascent.h"

/*Globals*/

extern Globals		g;

/*Code*/

pascal void SetupTargetLeft(SpritePtr target)
{
	target->task = &HandleTarget;
	target->face = SATGetFace(kLeftTargetID);
	target->hitTask = &HitTaskTargetLeft;
	target->layer = kTargetLayer;
	target->speed.v = 1;
	SetRect(&target->hotRect, 0, 0, kTargetWidth, kTargetHeight);
}																 	

pascal void HandleTarget(SpritePtr target)
{
	target->position.v = target->position.v + target->speed.v;		//Move the target
	if(target->position.v <= 100 || target->position.v >= gSAT.offSizeV - 100 - kTargetHeight)
	{
		target->speed.v = target->speed.v * -1;		//Invert direction of movement if bounds are reached
	}	
}

pascal void HitTaskTargetLeft(SpritePtr me, SpritePtr him)
{
SpritePtr	sparkler;

	if(him->kind == kBallKind && g.ball->mode == kBallFreeMode)	//if the other object is the ball and it is in free fall
	{
		g.rightShipScore += 1;						//Rise the right ship's score
		DisplayScore();						
		him->task = nil;							//Eliminate the ball
		
		switch(SATRand(7))							//Play a random sound
		{
			case 0:
				SATSoundPlay(g.woohooSnd, 2, true);
				break;	
			case 1:	
				SATSoundPlay(g.notBadSnd, 2, true);
				break;	
			case 2:
				SATSoundPlay(g.joySnd, 2, true);
				break;	
			case 3:
				SATSoundPlay(g.wowSnd, 2, true);
				break;	
			case 4:
				SATSoundPlay(g.cool1Snd, 2, true);
				break;
			case 5:
				SATSoundPlay(g.cool2Snd, 2, true);
				break;
			case 6:
				SATSoundPlay(g.whoaSnd, 2, true);
				break;	
		}	
		
		g.spawner->mode = kSpawnerSpawningMode;		//Set things in motion so that the ball gets respawned
		
		sparkler = SATNewSprite(kSparkKind, -3000, -3000, &SetupSparkler);	//Make the target sparkle
		sparkler->who = me;
		sparkler->counter = 40;
	}	
}	

pascal void SetupTargetRight(SpritePtr target)
{
	target->task = &HandleTarget;
	target->face = SATGetFace(kRightTargetID);
	target->hitTask = &HitTaskTargetRight;
	target->layer = kTargetLayer;
	target->speed.v = 1;
	SetRect(&target->hotRect, 0, 0, kTargetWidth, kTargetHeight); 
}
																
pascal void HitTaskTargetRight(SpritePtr me, SpritePtr him)	//same as HitTaskTargetLeft(), only
{															//the left ship's score is raised
SpritePtr	sparkler;

	if(him->kind == kBallKind && g.ball->mode == kBallFreeMode)
	{
		g.leftShipScore += 1;
		DisplayScore();
		him->task = nil;
		
		switch(SATRand(7))
		{
			case 0:
				SATSoundPlay(g.woohooSnd, 2, true);
				break;	
			case 1:	
				SATSoundPlay(g.notBadSnd, 2, true);
				break;	
			case 2:
				SATSoundPlay(g.joySnd, 2, true);
				break;	
			case 3:
				SATSoundPlay(g.wowSnd, 2, true);
				break;	
			case 4:
				SATSoundPlay(g.cool1Snd, 2, true);
				break;
			case 5:
				SATSoundPlay(g.cool2Snd, 2, true);
				break;
			case 6:
				SATSoundPlay(g.whoaSnd, 2, true);
				break;	
		}	
		
		g.spawner->mode = kSpawnerSpawningMode;
		
		sparkler = SATNewSprite(kSparkKind, -3000, -3000, &SetupSparkler);
		sparkler->who = me;
		sparkler->counter = 40;
	}	
}	