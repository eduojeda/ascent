/* Asks TypeSafe's Jev which tactic the computer player should adopt.

   The game runs a frame every 16ms; a Jev call takes 70-500ms. So the model
   never touches the controls. It picks a tactic a few times a second on a
   background thread, and bot.c flies the ship. Nothing here blocks the game
   loop: the newest state is offered, the newest answer is collected, and a
   match plays on unchanged when the key is missing or the network is down. */

#ifndef ASCENT_JEV_H
#define ASCENT_JEV_H

#include "compat/mac_types.h"

typedef struct JevDecision {
	int			tactic;			/* BotTactic, or -1 when unreadable */
	double		confidence;		/* 0..1, how sure the choice is */
	double		fire;			/* noul: worth shooting now */
	double		usePowerup;		/* noul: worth spending the powerup */
} JevDecision;

/* Reads TYPESAFE_API_KEY and starts the worker. False means no key, or the
   client would not initialize; the caller then plays without the model. */
Boolean		JevStart(void);
void		JevStop(void);
Boolean		JevRunning(void);

/* True when no request is outstanding, so a new state is worth sending. */
Boolean		JevIdle(void);

/* Hands over one state object (JSON, without the surrounding request). The
   text is copied; an older unsent state is discarded in favour of this one. */
void		JevSubmit(const char *stateJson);

/* Collects an answer that has arrived since the last call. */
Boolean		JevPoll(JevDecision *out);

/* Diagnostics for the status line: the last failure, and how many calls
   have completed or failed since the match began. */
const char	*JevLastError(void);
int			JevCallCount(void);
int			JevFailCount(void);
double		JevLastLatencyMs(void);

#endif
