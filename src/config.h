#ifndef CONFIG_H
#define CONFIG_H

/* Limits and tunables of the whole game, in one place. The values here are the
 * ones docs/requirements.md, section 5, requires: changing one of them means
 * changing that document too.
 *
 * Nothing is included here on purpose. A dedicated server includes this file
 * and must not pick up SDL — or anything else — through it, so the contents
 * are limited to #define and _Static_assert. */

/* ---- Capacity ---- */

#define MAX_PLAYERS 10  /* snapshot size and map density */
#define MAX_SKINS 10    /* characters in assets/players/skins.txt; one per player */
#define MAX_ENEMIES 32  /* co-op only */
#define MAX_ITEMS 64    /* bounded by the uint64_t pickup mask in a snapshot */
#define MAX_DOORS 64    /* */
#define MAX_SPRITES 192 /* enemies + items + barrels + 9 other players */
#define MAX_SPAWNS 24   /* twice as many as there are players */
#define MAX_EVENTS 32   /* per tick and per snapshot, which keeps the nearest */

/* ---- Simulation ---- */

#define TICK_RATE 60
#define FIXED_DT (1.0 / (double)TICK_RATE)

#define RESPAWN_TIME 2.0f
#define SPAWN_PROTECT_TIME 2.0f
#define ITEM_RESPAWN_TIME 30.0f

/* ---- Network ---- */

#define SNAPSHOT_RATE 20     /* Hz, overridable by a launch flag */
#define SNAPSHOT_RATE_MIN 15 /* the lowest that flag may go: room for a home uplink */
#define NET_PORT_DEFAULT 27015

/* A client whose input has not come for this long, in seconds, stands still:
 * the server stops repeating its last buttons and keeps only its facing. */
#define NET_INPUT_TIMEOUT 0.2f

/* N9: the largest snapshot that fits in a 1500-byte MTU with room left for the
 * IP, UDP and ENet headers. One byte over and ENet fragments it, and an
 * unreliable packet with a fragment lost is lost whole. */
#define SNAPSHOT_MAX_BYTES 1400

/* Connections one host holds at once: every player, plus one so that a full
 * server can still let a client in long enough to tell it so (MSG_REJECT). */
#define NET_MAX_PEERS (MAX_PLAYERS + 1)

/* A connected peer that has acknowledged nothing for this long is gone. ENet
 * pings an idle peer every 0.5 s, so a live one never comes near it. */
#define NET_PEER_TIMEOUT_MS 5000 /* R1, R2: a lost peer is noticed within 5 s */

/* How long a client waits from connecting to its first snapshot. A server that
 * answers at all answers within a round trip; one that does not is not there. */
#define NET_CONNECT_TIMEOUT_MS 5000

/* How far in the past a client draws other players and enemies: far enough
 * that the moment drawn lies between two snapshots it already has. Two
 * snapshot intervals at SNAPSHOT_RATE, so that one can be lost on the way. */
#define INTERP_DELAY_MS 100
#define INTERP_DELAY (INTERP_DELAY_MS / 1000.0) /* seconds */

/* Past the newest snapshot, whoever was moving goes on at the speed the last
 * two showed for this long, in seconds, and then stands. */
#define INTERP_EXTRAPOLATE 0.25

/* Snapshots a client keeps to draw INTERP_DELAY back from. */
#define SNAPSHOT_HISTORY 8

/* One frame can show every enemy, every item and every player but the one
 * holding the camera. Barrels come on top of that and are already counted in
 * the value above. */
_Static_assert(MAX_SPRITES >= MAX_ENEMIES + MAX_ITEMS + MAX_PLAYERS - 1,
               "MAX_SPRITES cannot hold one frame's worth of entities");

/* A player's skin is their id. Ids are unique among the players in a match and
 * the same on every client, so no two players share a character and each one
 * looks the same everywhere — while there is a skin for every slot. */
_Static_assert(MAX_SKINS >= MAX_PLAYERS, "a player's skin is their id: every slot needs a skin");

/* A snapshot goes out after a tick, so no rate above the tick rate is reached,
 * and the server sizes the events it keeps between snapshots by the lowest. */
_Static_assert(SNAPSHOT_RATE_MIN > 0 && SNAPSHOT_RATE_MIN <= SNAPSHOT_RATE &&
                       SNAPSHOT_RATE <= TICK_RATE,
               "the snapshot rate must lie in SNAPSHOT_RATE_MIN .. TICK_RATE");

/* At the highest snapshot rate INTERP_DELAY is the most snapshots back, and
 * the moment drawn needs one on each side of it. */
_Static_assert(SNAPSHOT_HISTORY >= INTERP_DELAY_MS * TICK_RATE / 1000 + 2,
               "SNAPSHOT_HISTORY does not reach INTERP_DELAY back at TICK_RATE snapshots a second");

/* Which items are gone travels in a snapshot as a single bitmask. */
_Static_assert(MAX_ITEMS <= 64, "the pickup mask in a snapshot is 64 bits wide");

/* GameEvent.actor (event.h) is a uint8_t index into whichever list the kind
 * names, so no list may outgrow what that index addresses. */
_Static_assert(MAX_PLAYERS <= 256 && MAX_ENEMIES <= 256 && MAX_ITEMS <= 256 && MAX_DOORS <= 256,
               "GameEvent.actor is a uint8_t");

#endif
