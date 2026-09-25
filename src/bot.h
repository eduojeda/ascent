/* The computer player for the red ship.

   Split in two. The tactic says what to be doing for the next second or so,
   and comes from Jev when a key is configured and the network answers, or
   from a scripted rule when it does not. The reflex layer below it runs every
   frame and turns that tactic into the seven controls a ship reads, so the
   flying, aiming and throwing stay in code at 60 frames a second. */

#ifndef ASCENT_BOT_H
#define ASCENT_BOT_H

#include "compat/mac_types.h"

struct ShipInput;

typedef enum BotTactic {
	kTacticChaseBall = 0,
	kTacticCarryToGoal,
	kTacticInterceptCarrier,
	kTacticAttackOpponent,
	kTacticGrabPowerup,
	kTacticDefendGoal,
	kTacticEvade,
	kTacticCount
} BotTactic;

const char	*BotTacticName(int tactic);
int			BotTacticFromName(const char *name);	/* -1 when unrecognised */

/* Turns the computer player on for this match. Asking for Jev without a key,
   or without a network, leaves the scripted tactics driving. */
void		BotStart(Boolean useJev);
void		BotStop(void);
Boolean		BotEnabled(void);

/* Called once a frame, before the sprites run. */
void		BotThink(struct ShipInput *out);

/* One line for the debug overlay: tactic, where it came from, latency. */
const char	*BotStatus(void);

#endif
