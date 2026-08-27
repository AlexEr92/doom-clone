#ifndef WORLD_H
#define WORLD_H

/* The simulation, and nothing but: no SDL, no Framebuffer, no camera. A
 * dedicated server runs this file and the modules it reaches; a client runs
 * exactly the same code when it predicts. Two implementations of a tick would
 * drift apart the moment they disagree by one float. */

#include "config.h"
#include "map.h"
#include "player.h"
#include "sprite.h"
#include "enemy.h"
#include "item.h"
#include "door.h"
#include "event.h"
#include "input.h"
#include <stdint.h>

typedef struct World {
    Map map;
    PlayerState player;
    SpriteList sprites;
    EnemyList enemies;
    ItemList items;
    DoorList doors;
    EventQueue events; /* what the last tick produced, drained by the client */
    uint32_t tick;     /* ticks since world_load() */
    int loaded;
} World;

/* Load the map and put every entity on it in its starting state. Returns 0 on
 * success, -1 if the map could not be read (w->loaded is then 0). Called again
 * to restart: everything it touches is reset, nothing is carried over. */
int world_load(World *w, const char *map_path);

/* Advance the simulation by exactly one tick. `inputs` holds one PlayerInput
 * per player being simulated; a player with no input for this tick keeps its
 * facing and presses nothing.
 *
 * Clears w->events on entry and fills it with whatever the tick produced, then
 * increments w->tick. Deterministic: given the same world and the same inputs
 * it always lands on the same state, which is what reconciliation replays. */
void world_step(World *w, const PlayerInput inputs[], int count, double dt);

#endif
