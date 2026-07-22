#ifndef GAME_H
#define GAME_H

#include "engine.h"
#include "input.h"
#include "player.h"

typedef enum {
    GSTATE_MENU = 0,
    GSTATE_PLAYING,
    GSTATE_PAUSED,
    GSTATE_DEAD,
    GSTATE_WIN,
} GameState;

typedef struct {
    Engine *eng;
    GameState state;
    GameState prev_state;     /* for un-pausing */
    float menu_timer;         /* blink / animation */
    int restart;              /* set when (re)starting gameplay requested */
    int quit;                 /* request to exit whole program */
} Game;

void game_init(Game *g, Engine *eng);

/* Handle an input event in the context of the FSM (menu navigation etc).
 * Returns 1 if the event was consumed by the FSM, 0 otherwise (so the caller
 * can still process gameplay input). */
int game_handle_event(Game *g, InputState *in, SDL_Event *ev);

/* Per-frame FSM update (state transitions like DEAD->menu). */
void game_update(Game *g, double dt);

/* Draw the overlay for the current state (menu / paused / dead / win).
 * Call AFTER rendering the world so overlays appear on top. */
void game_draw_overlay(Framebuffer *fb, const Game *g, const Player *pl);

#endif