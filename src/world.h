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
    /* The tick each player last came in at each of map.spawns, plus one: 0 is
     * never. Read by world_pick_spawn(), cleared for whoever takes a slot. */
    uint32_t spawned_at[MAX_PLAYERS][MAX_SPAWNS];
    int loaded;
} World;

/* Load the map and put every entity on it in its starting state. Returns 0 on
 * success, -1 if the map could not be read (w->loaded is then 0). Called again
 * to restart: everything it touches is reset, nothing is carried over.
 *
 * The world comes back with no players in it and local_player at -1: who joins
 * is the caller's business, and a dedicated server never joins at all. */
int world_load(World *w, const char *map_path);

/* Put a player on the map at `at` and return their id, or -1 when every slot
 * is taken. The id is the index into w->players and is what an input in
 * world_step() is matched against. `at` NULL is a multiplayer join: the point
 * is world_pick_spawn()'s, and the map's start when the map has no pool. */
int world_add_player(World *w, const SpawnPoint *at);

/* The safest of map.spawns for player `player_id` to come in at, or NULL for a
 * map without any. A point a live player stands on is chosen only when every
 * point is taken. Of the rest each is scored and the highest wins, the first
 * of equals; the player themselves count for nothing, alive or not. The score
 * is the distance to the nearest live player, in cells, less
 *   - if any live player has a line of sight to the point: more than
 *     everything else together, so that a point out of sight always wins
 *     over one in somebody's — and among those in sight, distance decides,
 *     not how many see it: one player a step away is worse than two across
 *     the map;
 *   - for every live enemy within its detect range of it: as far as the
 *     point is inside that range;
 *   - if this player came in at it less than 10 s ago: 8, so that a player is
 *     not sent back where somebody may be waiting while another point is
 *     about as good.
 * Reads no clock and no rand(): the same world gives the same point. */
const SpawnPoint *world_pick_spawn(const World *w, int player_id);

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
