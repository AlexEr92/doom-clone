#ifndef NET_SERVER_H
#define NET_SERVER_H

/* The authoritative server: it owns the World, runs world_step() once per
 * fixed step on the newest input of every client, and sends each of them a
 * snapshot of its own several times a second. Clients decide nothing but
 * their own facing (input.h).
 *
 * A listen server is the same server with one player more, who plays at this
 * machine: server_add_local_player() puts them in, server_tick() is handed
 * their input, and nothing of theirs goes over the network. They read the
 * world and its events straight from server_world(), after every tick. */

#include <stdint.h>

struct World;
struct PlayerInput;

typedef struct Server Server; /* opaque */

/* Load the map at `map_path` and listen on `port`. Seeds rand() — shotgun
 * spread is rolled here and on no client — with the seed every MSG_ACCEPT
 * carries. NULL when the map does not load, its file name does not fit in
 * MsgAccept.map, or the port is taken. */
Server *server_create(uint16_t port, const char *map_path);

/* Tell every client the server is gone — MSG_DISCONNECT to each one in the
 * game — and free it. NULL is ignored. */
void server_destroy(Server *s);

/* Put the player who plays at this machine into the world and return their
 * id, or -1 when every slot is taken. There is one such player: a second call
 * returns the id the first one gave. */
int server_add_local_player(Server *s);

/* One fixed step, called once per tick with dt = FIXED_DT: take what the
 * network brought, run world_step() with every client's input and `local` as
 * the local player's (NULL on a dedicated server), and send the snapshots that
 * have come due. */
void server_tick(Server *s, const struct PlayerInput *local, double dt);

/* Snapshots a second, SNAPSHOT_RATE until set. Held to SNAPSHOT_RATE_MIN ..
 * TICK_RATE: one goes out after a tick, and the events kept between two of
 * them have room for the ticks of the lowest rate. */
void server_set_snapshot_rate(Server *s, int hz);

/* The local player's id, -1 on a dedicated server. */
int server_local_player(const Server *s);

struct World *server_world(Server *s);

#endif
