#ifndef WORLD_H
#define WORLD_H

/* The simulation, and nothing but: no SDL, no Framebuffer, no camera. A
 * dedicated server runs this file and the modules it reaches; a client runs
 * exactly the same code when it predicts. Two implementations of a tick would
 * drift apart the moment they disagree by one float. */

#include "config.h"
#include "map.h"
#include "player.h"
#include "enemy.h"
#include "item.h"
#include "door.h"
#include "event.h"
#include "input.h"
#include <stdint.h>

typedef struct World {
    Map map;
    PlayerState players[MAX_PLAYERS];
    int player_count; /* slots ever handed out; a freed one stays in the count */
    int local_player; /* the player this process looks through, -1 on a server */
    EnemyList enemies;
    ItemList items;
    DoorList doors;
    EventQueue events; /* what the last tick produced, drained by the client */
    uint32_t tick;     /* ticks since world_load() */
    int loaded;
} World;

/* Load the map and put every entity on it in its starting state. Returns 0 on
 * success, -1 if the map could not be read (w->loaded is then 0). Called again
 * to restart: everything it touches is reset, nothing is carried over.
 *
 * The world comes back with no players in it and local_player at -1: who joins
 * is the caller's business, and a dedicated server never joins at all. */
int world_load(World *w, const char *map_path);

/* Put a player on the map at spawn point `spawn_index` and return their id,
 * or -1 when every slot is taken. The id is the index into w->players and is
 * what an input in world_step() is matched against. */
int world_add_player(World *w, int spawn_index);

/* Take player `id` out of the world. The slot is marked free rather than
 * removed, so the ids of everyone else keep meaning what they meant, and the
 * next world_add_player() reuses it. */
void world_remove_player(World *w, int id);

/* Advance the simulation by exactly one tick. `inputs` is indexed by player
 * id and `count` is how far it reaches; a player with no input for this tick
 * keeps its facing and presses nothing.
 *
 * Players are visited in order of ascending id, always — with player-to-player
 * collision the result depends on that order, and the server and the client
 * predicting it have to walk the array the same way.
 *
 * Clears w->events on entry and fills it with whatever the tick produced, then
 * increments w->tick. Deterministic: given the same world and the same inputs
 * it always lands on the same state, which is what reconciliation replays. */
void world_step(World *w, const PlayerInput inputs[], int count, double dt);

#endif
