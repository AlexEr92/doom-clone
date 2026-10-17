#ifndef NET_TRANSPORT_H
#define NET_TRANSPORT_H

/* The network as the game sees it: hosts, peers by number, packets as bytes.
 * ENet sits behind this file and nowhere else — no ENetPeer or ENetPacket
 * leaves net_transport.c — so the server and the client know nothing about it,
 * and a test can stand in for it. */

#include <stddef.h>
#include <stdint.h>

typedef struct NetHost NetHost; /* opaque */

/* A peer of one host, 0 .. NET_MAX_PEERS-1, from the start of its connection
 * to its NET_EV_DISCONNECT or net_disconnect(). A number freed that way is
 * handed to the next connection. */
typedef int NetPeerId;
#define NET_PEER_NONE (-1)

/* Channel 0 is reliable and ordered: the handshake, chat. Channel 1 carries
 * what the next packet supersedes — input, snapshots: a stale snapshot
 * delivered late is worse than one lost. */
typedef enum { NET_CH_RELIABLE = 0, NET_CH_UNRELIABLE = 1, NET_CH_COUNT } NetChannel;

typedef enum {
    NET_EV_NONE = 0,
    NET_EV_CONNECT,    /* `peer` is connected and may be sent to */
    NET_EV_DISCONNECT, /* `peer` is gone and its number is free again */
    NET_EV_DATA,       /* `size` bytes at `data` arrived from `peer` */
} NetTransportEventType;

typedef struct {
    NetTransportEventType type;
    NetPeerId peer;
    const void *data; /* NET_EV_DATA only; valid until the next net_poll() */
    size_t size;
} NetTransportEvent;

/* A host listening on `port` on every interface, for up to `max_peers`
 * clients, 1 .. NET_MAX_PEERS. NULL if the port is taken, the limit is out of
 * range or ENet will not start. */
NetHost *net_host_server(uint16_t port, int max_peers);

/* A host with no port of its own, for one connection made by net_connect(). */
NetHost *net_host_client(void);

/* Start connecting to `addr` (a name or a dotted address) and return the
 * peer's number at once; NET_EV_CONNECT follows when the other side answers,
 * NET_EV_DISCONNECT if it never does. NET_PEER_NONE when the address does not
 * resolve or the host has no free peer. */
NetPeerId net_connect(NetHost *h, const char *addr, uint16_t port);

/* Let `peer` go once what is already queued for it has been sent — a reject
 * followed by a disconnect still arrives. The number is free on return, and
 * no NET_EV_DISCONNECT is reported for it: the caller already knows. */
void net_disconnect(NetHost *h, NetPeerId peer);

/* Tell every peer the host is going away, then close it. NULL is ignored.
 * What is queued for a peer net_disconnect() let go still leaves, and is all
 * that peer is told: a server that goes away sends its clients a message
 * saying so, and lets each one go. */
void net_destroy(NetHost *h);

/* Queue `size` bytes for `peer` on `channel` (NetChannel). `reliable` packets
 * are retransmitted until acknowledged; unreliable ones, fragments included,
 * are dropped whole when anything is lost. Leaves on the next net_poll(). An
 * unknown peer or a channel out of range is ignored. */
void net_send(NetHost *h, NetPeerId peer, int channel, const void *data, size_t size, int reliable);

/* net_send() to every connected peer. */
void net_broadcast(NetHost *h, int channel, const void *data, size_t size, int reliable);

/* Send what is queued, take one event, and never wait for one. Returns 1 with
 * `*out` filled, or 0 with `out->type` NET_EV_NONE when nothing is left; call
 * it in a loop until 0, once a frame or a tick.
 *
 * A connected peer that has acknowledged nothing for NET_PEER_TIMEOUT_MS is
 * reported here as NET_EV_DISCONNECT. The silence counts in real time, so a
 * host left unpolled for that long loses its peers on the next call. */
int net_poll(NetHost *h, NetTransportEvent *out);

/* Mean round-trip time to `peer` in milliseconds, as ENet measures it. 0 for
 * an unknown peer. */
float net_peer_rtt(const NetHost *h, NetPeerId peer);

/* Share of reliable packets to `peer` lost recently, 0.0 .. 1.0. 0 for an
 * unknown peer. */
float net_peer_loss(const NetHost *h, NetPeerId peer);

/* Milliseconds on the clock peers are timed out by, ENet's: a timeout of the
 * caller's own measured here moves with them when a test sets that clock. It
 * wraps, so two readings are compared by their difference. */
uint32_t net_time(void);

#endif
