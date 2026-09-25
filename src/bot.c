#include <math.h>
#include <SDL3/SDL.h>

#include "mySAT.h"
#include "ascent.h"
#include "bot.h"
#include "jev.h"

/*Constants*/

#define kBotMaxSpeed		6.0		//What the engines reach against the viscosity
#define kBotSteerGain		0.09	//Desired speed per pixel of error
#define kBotSpeedBand		0.25	//Speed error we do not bother correcting
#define kFireCooldown		9		//Frames between cannon shots
#define kActionCooldown		5		//Frames between powerup or release presses
#define kRotateCooldown		20		//A turn takes about twelve frames on its own
#define kTacticHoldFrames	24		//Shortest time we stay on one tactic
#define kJevPeriodMs		150		//About six or seven calls a second
#define kJevMinConfidence	0.35	//Below this we keep what we were doing
#define kJevStaleMs			2500	//An answer older than this stops steering us
#define kCarryPatience		240		//Frames carrying before we throw regardless

/*The powerup codes and the ball's shot speed live inside sGoodie.c and
  sBall.c as private constants. The bot needs to name them too.*/

#define kBotNoPowerup		0
#define kBotRocket			1
#define kBotMissile			2
#define kBotHealth			3
#define kBotBallShot		4
#define kBotBallMagnet		5
#define kBotBallShotSpeed	20

/*Globals*/

extern Globals		g;

static Boolean		gEnabled, gWantJev, gDebug, gTraceRuns;
static int			gTactic = kTacticChaseBall, gTacticAge;
static const char	*gTacticSource = "script";
static int			gFireCool, gActionCool, gRotateCool;
static int			gCarryAge, gDebugTick;
static Boolean		gCarryRetreat;
static Uint64		gLastSubmit, gLastAnswer;
static double		gFireNoul, gPowerNoul, gConfidence;
static char			gStatus[192];

static const char *kTacticNames[kTacticCount] = {
	"chase_ball", "carry_to_goal", "intercept_carrier", "attack_opponent",
	"grab_powerup", "defend_goal", "evade"
};

/*Code*/

const char *BotTacticName(int tactic)
{
	if (tactic < 0 || tactic >= kTacticCount)
		return "none";
	return kTacticNames[tactic];
}

int BotTacticFromName(const char *name)
{
	int i;

	if (!name)
		return -1;
	for (i = 0; i < kTacticCount; i++)
		if (!SDL_strcmp(name, kTacticNames[i]))
			return i;
	return -1;
}

/* A ship pointer is only safe to follow while its sprite is still in the
   game. Between the wreck hitting the floor and the next spawn the global
   still holds the address of a sprite SAT has already freed. */
static SpritePtr LiveShip(SpritePtr ship, Boolean reincarnating, int lives)
{
	if (!ship || reincarnating || lives <= 0)
		return nil;
	return ship;
}

static SpritePtr Me(void)
{
	return LiveShip(g.rightShip, g.rightShipReincarnating, g.rightShipLives);
}

static SpritePtr Opponent(void)
{
	return LiveShip(g.leftShip, g.leftShipReincarnating, g.leftShipLives);
}

static SpritePtr LiveBall(void)
{
	if (!g.ball || g.ball->task == nil)
		return nil;
	return g.ball;
}

static SpritePtr LiveGoodie(void)
{
	if (!g.goodie || g.goodie->mode != kGoodieNormalMode)
		return nil;
	return g.goodie;
}

static double Clamp(double v, double lo, double hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static double ShipCenterH(SpritePtr s) { return s->position.h + kShipWidth / 2.0; }
static double ShipCenterV(SpritePtr s) { return s->position.v + kShipHeight / 2.0; }

/* The lowest x the red ship can reach. Its own bounds check pushes it back
   and takes shields off it at the beam, so the bot treats it as a wall. */
static double BarrierH(void)
{
	if (g.bottomLeftLG && g.bottomLeftLG->mode == kLimitGAliveMode)
		return kLimits;
	return 0;
}

/* ---- where a thrown ball would land ----

   Worth simulating rather than guessing: the ball leaves with the ship's
   velocity divided by the model's timescale, falls under the same gravity as
   everything else, and the goal is sliding up or down the whole time. */

static double gLastMiss;	/* how far under the goal the last throw modelled */

static Boolean ThrowScores(SpritePtr me, Boolean withBallShot)
{
	double bx, by, vh, vv, goalV, goalStep;
	int i;

	gLastMiss = 9999;
	if (!g.leftTarget)
		return false;

	/* the ball rides under the cannon while carried, on the side it faces */
	if (me->direction)
		bx = me->position.h + me->hotRect.right - 20;
	else
		bx = me->position.h + me->hotRect.left + 20 - kBallDiameter;
	by = me->position.v + me->hotRect.bottom + 1;

	vh = me->speed.h;
	if (withBallShot)
		vh -= kBotBallShotSpeed;	/* the ball-shot powerup, thrown left */
	vh /= kTiempo;
	vv = me->speed.v / kTiempo;

	goalV = g.leftTarget->position.v;
	goalStep = g.leftTarget->speed.v;

	for (i = 0; i < 240; i++) {
		vv += 0.5 * kGravityV * (kTiempo * kTiempo);
		bx += vh * kTiempo;
		by += vv * kTiempo;

		goalV += goalStep;
		if (goalV <= 100 || goalV >= gSAT.offSizeV - 100 - kTargetHeight)
			goalStep = -goalStep;

		if (by >= gSAT.offSizeV - kBallDiameter)
			return false;			/* on the floor, and the goal is not */
		if (by <= 0)
			return false;			/* off the ceiling, too scrappy to count */
		if (bx <= kTargetWidth) {
			gLastMiss = (by + kBallDiameter / 2.0) -
			            (goalV + kTargetHeight / 2.0);
			return by + kBallDiameter >= goalV &&
			       by <= goalV + kTargetHeight;
		}
	}
	return false;
}

/* Advances the goal's slide by a number of frames. */
static double GoalAfter(double v, double *step, int frames)
{
	int i;

	for (i = 0; i < frames; i++) {
		v += *step;
		if (v <= 100 || v >= gSAT.offSizeV - 100 - kTargetHeight)
			*step = -(*step);
	}
	return v;
}

/* The height to fly to on the way in, so that the throw lines up when the
   ship gets to `releaseCentreH`. Aiming from where the ship is now is no use:
   nothing thrown from mid-arena reaches the goal, the ball falls too far. So
   this looks ahead to the release point, and answers for the goal's position
   at the moment the ball would arrive there. */

static Boolean ThrowAimHeight(SpritePtr me, double releaseCentreH,
                              double *aimCentreV)
{
	/* The nominal speed a ship carrying the ball settles at. Using this
	   frame's speed instead would make the target chase its own tail. */
	const double speed = 4;
	double gap, bx, vv = 0, drop = 0, goalV, goalStep, wantBallV;
	int i, approach, flight = -1;

	if (!g.leftTarget)
		return false;

	gap = ShipCenterH(me) - releaseCentreH;
	approach = (gap > 0) ? (int)(gap / speed) : 0;
	if (approach > 240)
		return false;			/* too far out to plan a throw yet */

	goalV = g.leftTarget->position.v;
	goalStep = g.leftTarget->speed.v;
	goalV = GoalAfter(goalV, &goalStep, approach);

	bx = releaseCentreH - kShipWidth / 2.0 + 10;	/* under the left cannon */
	for (i = 0; i < 240; i++) {
		vv += 0.5 * kGravityV * (kTiempo * kTiempo);
		bx -= speed;
		drop += vv * kTiempo;
		if (bx <= kTargetWidth) {
			flight = i + 1;
			break;
		}
	}
	if (flight < 0)
		return false;

	goalV = GoalAfter(goalV, &goalStep, flight);
	wantBallV = goalV + kTargetHeight / 2.0 - kBallDiameter / 2.0 - drop;
	*aimCentreV = wantBallV - 12;	/* the ball hangs 26 below the ship's top,
									   whose centre is 14 below it */
	return *aimCentreV > kShipHeight &&
	       *aimCentreV < gSAT.offSizeV - kShipHeight - 14;
}

/* ---- the scripted tactic, used until Jev answers and whenever it cannot ---- */

static int ScriptedTactic(SpritePtr me, SpritePtr him, SpritePtr ball)
{
	double myX = ShipCenterH(me);

	if (ball && ball->mode == kBallCaughtMode && ball->who == me)
		return kTacticCarryToGoal;

	if (me->shields < 25 && him)
		return kTacticEvade;

	if (ball && ball->mode == kBallCaughtMode && ball->who == him)
		return kTacticInterceptCarrier;

	if (LiveGoodie() && me->powerup == kBotNoPowerup) {
		SpritePtr goodie = LiveGoodie();
		double toGoodie = fabs(goodie->position.h - myX);
		double ballAway = ball ? fabs(ball->position.h - myX) : 1e9;
		if (toGoodie < ballAway)
			return kTacticGrabPowerup;
	}

	if (ball)
		return kTacticChaseBall;
	if (him)
		return kTacticAttackOpponent;
	return kTacticDefendGoal;
}

/* ---- the state Jev reads ---- */

static const char *PowerupName(short powerup)
{
	switch (powerup) {
	case kBotRocket:     return "rocket";
	case kBotMissile:    return "homing_missile";
	case kBotHealth:     return "shield_refill";
	case kBotBallShot:   return "ball_shot";
	case kBotBallMagnet: return "ball_magnet";
	default:             return "none";
	}
}

static const char *BallState(SpritePtr ball, SpritePtr me, SpritePtr him)
{
	if (!ball)
		return "gone_respawning";
	if (ball->mode == kBallMagnetoMode)
		return "being_pulled_by_magnet";
	if (ball->mode != kBallCaughtMode)
		return "loose";
	if (ball->who == me)
		return "carried_by_me";
	if (ball->who == him)
		return "carried_by_opponent";
	return "loose";
}

static void BuildState(SpritePtr me, SpritePtr him, SpritePtr ball,
                       char *out, size_t outSize)
{
	SpritePtr goodie = LiveGoodie();
	double myX = ShipCenterH(me), myY = ShipCenterV(me);
	double barrier = BarrierH();
	double ballX = ball ? ball->position.h : -1;
	double ballY = ball ? ball->position.v : -1;

	SDL_snprintf(out, outSize,
	 "{"
	 "\"arena\":{\"width\":%d,\"height\":%d,"
	 "\"note\":\"x grows rightward, y grows downward, so a smaller y is higher up\"},"
	 "\"me\":{\"ship\":\"red\",\"x\":%d,\"y\":%d,\"speed_x\":%.1f,\"speed_y\":%.1f,"
	 "\"facing\":\"%s\",\"shields\":%d,\"lives\":%d,\"score\":%d,"
	 "\"powerup\":\"%s\",\"carrying_ball\":%s},"
	 "\"opponent\":{\"ship\":\"blue\",\"present\":%s,\"x\":%d,\"y\":%d,"
	 "\"speed_x\":%.1f,\"speed_y\":%.1f,\"shields\":%d,\"lives\":%d,\"score\":%d,"
	 "\"powerup\":\"%s\",\"distance_from_me\":%d,\"height_difference\":%d,"
	 "\"side\":\"%s\"},"
	 "\"ball\":{\"state\":\"%s\",\"x\":%d,\"y\":%d,\"speed_x\":%.1f,\"speed_y\":%.1f,"
	 "\"distance_from_me\":%d,\"distance_from_opponent\":%d},"
	 "\"target_goal\":{\"x\":%d,\"y\":%d,\"note\":\"I score by throwing the ball in here; it slides up and down\"},"
	 "\"my_goal\":{\"x\":%d,\"y\":%d,\"note\":\"the opponent scores here\"},"
	 "\"powerup_canister\":{\"present\":%s,\"x\":%d,\"y\":%d},"
	 "\"barrier\":{\"my_lowest_x\":%d,\"standing\":%s,"
	 "\"note\":\"touching the beam costs me shields and throws me back; the ball flies over it\"},"
	 "\"match\":{\"points_to_win\":%d,\"my_last_tactic\":\"%s\",\"frames_on_it\":%d}"
	 "}",
	 gSAT.offSizeH, gSAT.offSizeV,
	 (int)myX, (int)myY, me->speed.h, me->speed.v,
	 me->direction ? "right" : "left", me->shields, g.rightShipLives,
	 g.rightShipScore, PowerupName(me->powerup),
	 (ball && ball->mode == kBallCaughtMode && ball->who == me) ? "true" : "false",
	 him ? "true" : "false",
	 him ? (int)ShipCenterH(him) : 0, him ? (int)ShipCenterV(him) : 0,
	 him ? him->speed.h : 0.0, him ? him->speed.v : 0.0,
	 him ? him->shields : 0, g.leftShipLives, g.leftShipScore,
	 him ? PowerupName(him->powerup) : "none",
	 him ? (int)fabs(ShipCenterH(him) - myX) : -1,
	 him ? (int)(ShipCenterV(him) - myY) : 0,
	 him ? (ShipCenterH(him) < myX ? "left_of_me" : "right_of_me") : "absent",
	 BallState(ball, me, him),
	 (int)ballX, (int)ballY,
	 ball ? ball->speed.h : 0.0, ball ? ball->speed.v : 0.0,
	 ball ? (int)fabs(ballX - myX) : -1,
	 (ball && him) ? (int)fabs(ballX - ShipCenterH(him)) : -1,
	 g.leftTarget ? (int)g.leftTarget->position.h : 0,
	 g.leftTarget ? (int)g.leftTarget->position.v : 0,
	 g.rightTarget ? (int)g.rightTarget->position.h : gSAT.offSizeH,
	 g.rightTarget ? (int)g.rightTarget->position.v : 0,
	 goodie ? "true" : "false",
	 goodie ? (int)goodie->position.h : -1,
	 goodie ? (int)goodie->position.v : -1,
	 (int)barrier, barrier > 0 ? "true" : "false",
	 g.numPoints, BotTacticName(gTactic), gTacticAge);
}

/* ---- talking to the model ---- */

static void ServiceJev(SpritePtr me, SpritePtr him, SpritePtr ball)
{
	JevDecision d;
	Uint64 now = SDL_GetTicks();
	char state[8192];

	if (!JevRunning())
		return;

	if (JevPoll(&d)) {
		gLastAnswer = now;
		gConfidence = d.confidence;
		gFireNoul = d.fire;
		gPowerNoul = d.usePowerup;
		if (d.tactic >= 0 && d.confidence >= kJevMinConfidence &&
		    d.tactic != gTactic) {
			gTactic = d.tactic;
			gTacticAge = 0;
			gTacticSource = "jev";
			if (gDebug)
				SDL_Log("bot: jev chose %s (confidence %.2f, %.0fms)",
				        BotTacticName(gTactic), d.confidence,
				        JevLastLatencyMs());
		}
	}

	if (now - gLastSubmit >= kJevPeriodMs && JevIdle()) {
		BuildState(me, him, ball, state, sizeof state);
		JevSubmit(state);
		gLastSubmit = now;
	}
}

static Boolean JevAnswerIsFresh(void)
{
	return gLastAnswer != 0 && SDL_GetTicks() - gLastAnswer < kJevStaleMs;
}

/* ---- the reflex layer ---- */

typedef struct Intent {
	double	x, y;			/* where the ship's centre should be */
	Boolean	faceLeft;		/* which way it should point */
	Boolean	needsFacing;	/* ...and whether that matters right now */
	Boolean	dash;			/* cross the distance at full speed, not gently */
} Intent;

static void AimAt(Intent *in, double x, double y)
{
	in->x = x;
	in->y = y;
}

static void PlanTactic(int tactic, SpritePtr me, SpritePtr him,
                       SpritePtr ball, Intent *in)
{
	double myX = ShipCenterH(me), myY = ShipCenterV(me);
	double barrier = BarrierH();
	SpritePtr goodie = LiveGoodie();

	in->needsFacing = false;
	in->faceLeft = true;
	in->dash = false;
	AimAt(in, myX, myY);

	switch (tactic) {
	case kTacticCarryToGoal: {
		double goalY = g.leftTarget ? g.leftTarget->position.v + kTargetHeight / 2.0
		                            : gSAT.offSizeV / 2.0;
		double releaseH = barrier + 70;
		double aimV;

		/* A thrown ball carries only the speed the ship had, so the run at
		   the goal matters as much as the spot it is thrown from. */
		if (gCarryRetreat) {
			AimAt(in, barrier + 380, goalY - 90);
		} else if (ThrowAimHeight(me, releaseH, &aimV)) {
			AimAt(in, releaseH, aimV);
			in->dash = true;
		} else {
			AimAt(in, releaseH, goalY - 90);
			in->dash = true;
		}
		in->needsFacing = true;
		in->faceLeft = true;	/* it can fly backwards; turning costs more */
		break;
	}
	case kTacticChaseBall:
		if (ball) {
			/* lead it, or the ship arrives where the ball used to be */
			double lead = 8;
			AimAt(in, ball->position.h + kBallDiameter / 2.0 +
			          ball->speed.h * kTiempo * lead,
			      ball->position.v + kBallDiameter / 2.0 +
			          ball->speed.v * kTiempo * lead);
		}
		break;
	case kTacticInterceptCarrier:
		if (him) {
			AimAt(in, ShipCenterH(him), ShipCenterV(him));
			in->needsFacing = true;
			in->faceLeft = ShipCenterH(him) < myX;
		}
		break;
	case kTacticAttackOpponent:
		if (him) {
			/* hold a firing lane at his height rather than ramming him */
			double side = (ShipCenterH(him) < myX) ? 1 : -1;
			AimAt(in, ShipCenterH(him) + side * 190, ShipCenterV(him));
			in->needsFacing = true;
			in->faceLeft = ShipCenterH(him) < myX;
		}
		break;
	case kTacticGrabPowerup:
		if (goodie)
			AimAt(in, goodie->position.h + 10, goodie->position.v + 9);
		break;
	case kTacticDefendGoal: {
		double goalX = g.rightTarget ? g.rightTarget->position.h
		                             : gSAT.offSizeH - kTargetWidth;
		double guardY = ball ? ball->position.v : gSAT.offSizeV / 2.0;
		AimAt(in, goalX - 130, guardY);
		in->needsFacing = true;
		in->faceLeft = true;
		break;
	}
	case kTacticEvade: {
		double away = him ? (myX >= ShipCenterH(him) ? 1 : -1) : 1;
		AimAt(in, myX + away * 260, myY - 90);
		break;
	}
	}

	/* Never steer into the beam or out of the arena. */
	in->x = Clamp(in->x, barrier + kShipWidth / 2.0 + 12,
	              gSAT.offSizeH - kShipWidth / 2.0 - 8);
	in->y = Clamp(in->y, kShipHeight, gSAT.offSizeV - kShipHeight - 14);
}

static void SteerTo(SpritePtr me, const Intent *in, ShipInput *out)
{
	double gap = in->x - ShipCenterH(me);
	double wantH = Clamp(gap * kBotSteerGain, -kBotMaxSpeed, kBotMaxSpeed);

	/* Full throttle right up to the mark, never easing in: a throw carries
	   only the speed the ship has, and the aim is worked out for a ship at
	   full speed. Braking is the retreat's job, once the mark is behind. */
	if (in->dash && fabs(gap) > 2)
		wantH = (gap < 0) ? -kBotMaxSpeed : kBotMaxSpeed;
	double wantV = Clamp((in->y - ShipCenterV(me)) * kBotSteerGain,
	                     -kBotMaxSpeed, kBotMaxSpeed);

	if (me->speed.h < wantH - kBotSpeedBand)
		out->right = true;
	else if (me->speed.h > wantH + kBotSpeedBand)
		out->left = true;

	if (me->speed.v < wantV - kBotSpeedBand)
		out->down = true;
	else if (me->speed.v > wantV + kBotSpeedBand)
		out->up = true;
}

/* True when a shot from here would reach him: roughly level, on the side we
   face, and close enough that the bullet does not leave the arena first. */
static Boolean HasFiringSolution(SpritePtr me, SpritePtr him)
{
	double dh, dv, tolerance;

	if (!him || him->mode == kShipDyingMode)
		return false;

	dh = ShipCenterH(him) - ShipCenterH(me);
	dv = ShipCenterV(him) - ShipCenterV(me);

	tolerance = 22;
	if (JevAnswerIsFresh()) {
		if (gFireNoul > 0.6)
			tolerance = 34;			/* the model wants shots taken */
		else if (gFireNoul < 0.25)
			tolerance = 12;			/* ...or wants them held */
	}

	if (fabs(dv) > tolerance || fabs(dh) > 560)
		return false;
	return me->direction ? (dh > 0) : (dh < 0);
}

static Boolean ShouldSpendPowerup(SpritePtr me, SpritePtr him, SpritePtr ball,
                                  Boolean carrying)
{
	if (me->powerup == kBotNoPowerup)
		return false;

	switch (me->powerup) {
	case kBotBallShot:
		return carrying && ThrowScores(me, true);
	case kBotBallMagnet:
		/* only useful while the ball is loose and someone else is nearer */
		return ball && ball->mode == kBallFreeMode && him &&
		       fabs(ball->position.h - ShipCenterH(him)) <
		           fabs(ball->position.h - ShipCenterH(me));
	case kBotRocket:
	case kBotMissile:
		if (JevAnswerIsFresh() && gPowerNoul > 0.55)
			return him != nil;
		return HasFiringSolution(me, him);
	default:
		return false;
	}
}

/* ASCENT_BOT_DEBUG prints a line a second; adding ASCENT_BOT_TRACE prints
   every frame of a run at the goal, where `miss` is how far under the goal
   the throw would land. Tuning the run is guesswork without it. */
static void LogProgress(SpritePtr me, const Intent *in, Boolean carrying)
{
	if (gTraceRuns && carrying && ShipCenterH(me) < BarrierH() + 260) {
		Boolean on = ThrowScores(me, false);
		SDL_Log("run: x=%.0f y=%.0f vx=%.1f vy=%.1f aimY=%.0f goalY=%.0f "
		        "retreat=%d throw=%d miss=%.0f",
		        ShipCenterH(me), ShipCenterV(me), me->speed.h, me->speed.v,
		        in->y, g.leftTarget ? (double)g.leftTarget->position.v : -1.0,
		        gCarryRetreat, on, gLastMiss);
	}

	if (++gDebugTick % 60 == 0)
		SDL_Log("bot: %s | at %.0f,%.0f v %.1f,%.1f %s carry=%d score %d-%d",
		        gStatus, ShipCenterH(me), ShipCenterV(me), me->speed.h,
		        me->speed.v, me->direction ? "faces_right" : "faces_left",
		        carrying, g.rightShipScore, g.leftShipScore);
}

void BotThink(ShipInput *out)
{
	SpritePtr me, him, ball;
	Intent in;
	Boolean carrying, wantShoot = false, wantSpecial = false;

	SDL_memset(out, 0, sizeof *out);

	if (gFireCool > 0) gFireCool--;
	if (gActionCool > 0) gActionCool--;
	if (gRotateCool > 0) gRotateCool--;

	if (!gEnabled)
		return;

	me = Me();
	if (!me || me->mode != kShipAliveMode) {
		gCarryAge = 0;
		return;
	}
	him = Opponent();
	ball = LiveBall();
	carrying = ball && ball->mode == kBallCaughtMode && ball->who == me;

	gTacticAge++;
	ServiceJev(me, him, ball);

	/* The scripted tactic takes over when the model has not answered lately,
	   and always gets to correct a tactic that no longer makes sense. */
	{
		int scripted = ScriptedTactic(me, him, ball);
		Boolean modelDriving = JevAnswerIsFresh() &&
		                       SDL_strcmp(gTacticSource, "jev") == 0;
		Boolean impossible =
		    (gTactic == kTacticCarryToGoal && !carrying) ||
		    (gTactic == kTacticChaseBall && !ball) ||
		    (gTactic == kTacticGrabPowerup && !LiveGoodie()) ||
		    (gTactic == kTacticInterceptCarrier &&
		     !(ball && ball->mode == kBallCaughtMode && ball->who == him));

		if (carrying && gTactic != kTacticCarryToGoal) {
			gTactic = kTacticCarryToGoal;	/* never dawdle holding the ball */
			gTacticAge = 0;
			gTacticSource = "script";
		} else if (impossible ||
		           (!modelDriving && gTacticAge >= kTacticHoldFrames &&
		            scripted != gTactic)) {
			gTactic = scripted;
			gTacticAge = 0;
			gTacticSource = JevRunning() ? "script (waiting on jev)" : "script";
		}
	}

	gCarryAge = carrying ? gCarryAge + 1 : 0;

	PlanTactic(gTactic, me, him, ball, &in);
	SteerTo(me, &in, out);

	/* Turning costs about twelve frames of control, so only when it pays. */
	if (in.needsFacing && gRotateCool == 0) {
		Boolean facingLeft = !me->direction;
		if (facingLeft != in.faceLeft) {
			out->rotate = true;
			gRotateCool = kRotateCooldown;
		}
	}

	if (carrying) {
		Boolean facingLeft = !me->direction;
		Boolean shotIsOn = facingLeft && ThrowScores(me, false);
		Boolean outOfPatience;

		/* A run that reaches the beam without a shot turns into another run.
		   Giving the ball up here would drop it past the beam, in the corner
		   the red ship is not allowed into. */
		if (ShipCenterH(me) <= BarrierH() + 58 && !shotIsOn)
			gCarryRetreat = true;	/* past the throwing point with no shot:
									   pull out and come round again */
		else if (ShipCenterH(me) >= BarrierH() + 330)
			gCarryRetreat = false;

		outOfPatience = facingLeft && gCarryAge > kCarryPatience &&
		                ShipCenterH(me) > BarrierH() + 170;

		if (ShouldSpendPowerup(me, him, ball, carrying) && gActionCool == 0) {
			wantSpecial = true;
		} else if ((shotIsOn || outOfPatience) && gActionCool == 0) {
			wantShoot = true;		/* the shoot key releases a carried ball */
			gCarryAge = 0;
		}
	} else {
		if (gFireCool == 0 && HasFiringSolution(me, him))
			wantShoot = true;
		if (gActionCool == 0 && ShouldSpendPowerup(me, him, ball, carrying))
			wantSpecial = true;
	}

	/* One frame on, then a gap: the ship's own code wants the key released
	   before it will act on the next press. */
	if (wantShoot) {
		out->shoot = true;
		gFireCool = kFireCooldown;
		gActionCool = kActionCooldown;
	}
	if (wantSpecial) {
		out->special = true;
		gActionCool = kActionCooldown;
	}

	SDL_snprintf(gStatus, sizeof gStatus,
	             "%s via %s  conf %.2f  %.0fms  %d ok %d failed  %s",
	             BotTacticName(gTactic), gTacticSource, gConfidence,
	             JevLastLatencyMs(), JevCallCount(), JevFailCount(),
	             JevLastError());

	if (gDebug)
		LogProgress(me, &in, carrying);
}

void BotStart(Boolean useJev)
{
	gEnabled = true;
	gWantJev = useJev;
	gTactic = kTacticChaseBall;
	gTacticAge = 0;
	gTacticSource = "script";
	gCarryAge = 0;
	gFireCool = gActionCool = gRotateCool = 0;
	gLastSubmit = gLastAnswer = 0;
	gFireNoul = gPowerNoul = gConfidence = 0;
	gDebug = SDL_getenv("ASCENT_BOT_DEBUG") != NULL;
	gTraceRuns = SDL_getenv("ASCENT_BOT_TRACE") != NULL;

	if (useJev && !JevStart())
		SDL_Log("bot: playing without Jev (%s)", JevLastError());
}

void BotStop(void)
{
	gEnabled = false;
	if (gWantJev)
		JevStop();
	gWantJev = false;
}

Boolean BotEnabled(void)
{
	return gEnabled;
}

const char *BotStatus(void)
{
	return gStatus;
}
