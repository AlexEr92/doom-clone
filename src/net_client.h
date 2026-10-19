#ifndef NET_CLIENT_H
#define NET_CLIENT_H

/* The client's end of the network game: it joins a server, sends one input a
 * tick, and keeps a World that is whatever the newest snapshot says. Nothing is
 * simulated here — no world_step(), no prediction — so the client's own player
 * moves a round trip late. Everyone else is drawn from client_view(), a moment
 * in the past between two snapshots (net_interp.h), and moves smoothly.
 *
 * Called from the frame loop: client_send_input() once per fixed step,
 * client_poll() once a frame after them, so the inputs of the frame leave in
 * the same frame. */

#include <stdint.h>

struct World;
struct PlayerInput;

typedef enum {
    CL_DISCONNECTED, /* not connected yet, or the server went away after the join */
    CL_CONNECTING,   /* waiting for MSG_ACCEPT and the first snapshot */
    CL_CONNECTED,    /* in the game: client_world() has a world to draw */
    CL_REJECTED,     /* the join failed; client_reason() says why */
} ClientState;

typedef struct Client Client; /* opaque */

/* A client that is not connected. NULL when out of memory. */
Client *client_create(void);

/* Start joining the server at `addr` (a name or a dotted address) and `port`
 * as the player `name` — what MsgJoin.name carries, cut to NET_NAME_MAX - 1
 * bytes; NULL is no name. client_poll() carries the handshake on. A client
 * whose connection has ended may connect again, as a new player. Returns 0; -1
 * when the client is connected or connecting already, which it stays, or when
 * the address does not resolve, which leaves it CL_REJECTED with the reason. */
int client_connect(Client *c, const char *addr, uint16_t port, const char *name);

/* Tell the server the client is leaving, and free it. NULL is ignored. */
void client_destroy(Client *c);

/* Send the input of one tick, unreliably. The client numbers its inputs itself,
 * one up from the last, and the tick in `in` is not used: the server takes an
 * input only if its number is newer than the last one's. Ignored unless
 * CL_CONNECTED. */
void client_send_input(Client *c, const struct PlayerInput *in);

/* Send what is queued and take what came, once a frame. Every new snapshot is
 * put into the world in turn, and kept for client_view() dated by this call;
 * an older one than the world shows is dropped.
 * After the call World.events holds the events of the snapshots it applied, in
 * order, as many as MAX_EVENTS holds, and nothing else: each arrives once.
 *
 * Here the handshake goes on, and here it fails: no first snapshot within
 * NET_CONNECT_TIMEOUT_MS of client_connect() is CL_REJECTED. A server that
 * goes away after it is CL_DISCONNECTED. */
void client_poll(Client *c);

ClientState client_state(const Client *c);

/* Why the connection ended, for a person to read: the server is full, it runs
 * another version, it did not answer, it went away. NULL while it has not. */
const char *client_reason(const Client *c);

/* The world as the newest snapshot left it. NULL until the first one; after
 * the connection ends, the last one the client saw. */
struct World *client_world(Client *c);

/* The world as a frame draws it now: client_world(), but with the other
 * players and the enemies where they were INTERP_DELAY ago, between the
 * snapshots around that moment, as interp_view() puts them. The local player
 * is client_world()'s. Rebuilt by every call, so once a frame; valid until
 * the next one. NULL while client_world() is. */
struct World *client_view(Client *c);

/* The id of the player this client plays as, from MSG_ACCEPT; -1 before it. */
int client_local_player(const Client *c);

/* What the network carried for the client since client_connect(): payload
 * only — the IP, UDP and ENet headers around it, some 40 bytes a datagram,
 * are not counted. A rate is the difference of two readings. */
typedef struct {
    uint32_t snapshots; /* MSG_SNAPSHOT packets received, stale and broken ones too */
    uint64_t bytes_in;  /* every packet from the server */
    uint64_t bytes_out; /* every packet to it: the join and the inputs */
} ClientStats;

ClientStats client_stats(const Client *c);

#endif
