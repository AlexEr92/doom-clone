#include "net_transport.h"
#include "config.h"

#include <enet/enet.h>
#include <enet/time.h> /* ENET_TIME_DIFFERENCE, which enet.h leaves out */
#include <stdlib.h>

/* peers[] gives a NetPeerId its ENet peer, and the peer's `data` points back at
 * its own entry there, so either way is one step. An entry is NULL while the
 * number is free; `data` is NULL on a peer with no number — an incoming
 * connection not reported yet, or one net_disconnect() let go. */
struct NetHost {
    ENetHost *host;
    int peer_count; /* entries of peers[] in use: ENet's peerCount */
    ENetPeer *peers[NET_MAX_PEERS];
    ENetPacket *packet; /* the one net_poll() last handed out */
};

/* ENet is started by the first host and stopped with the last one: a listen
 * server may hold a server and a client host at the same time. */
static int enet_users;

static int enet_acquire(void)
{
    if (enet_users == 0 && enet_initialize() != 0) {
        return 0;
    }
    enet_users++;
    return 1;
}

static void enet_release(void)
{
    if (--enet_users == 0) {
        enet_deinitialize();
    }
}

static NetHost *host_create(const ENetAddress *address, int peer_count)
{
    if (!enet_acquire()) {
        return NULL;
    }
    NetHost *h = calloc(1, sizeof(*h));
    if (h) {
        h->host = enet_host_create(address, (size_t)peer_count, NET_CH_COUNT, 0, 0);
    }
    if (!h || !h->host) {
        free(h);
        enet_release();
        return NULL;
    }
    h->peer_count = peer_count;
    return h;
}

NetHost *net_host_server(uint16_t port, int max_peers)
{
    if (max_peers < 1 || max_peers > NET_MAX_PEERS) {
        return NULL;
    }
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;
    return host_create(&address, max_peers);
}

NetHost *net_host_client(void)
{
    return host_create(NULL, 1);
}

static NetPeerId peer_id(const NetHost *h, const ENetPeer *peer)
{
    ENetPeer *const *entry = peer->data;
    return entry ? (NetPeerId)(entry - h->peers) : NET_PEER_NONE;
}

static ENetPeer *peer_at(const NetHost *h, NetPeerId id)
{
    if (!h || id < 0 || id >= h->peer_count) {
        return NULL;
    }
    return h->peers[id];
}

/* There is always a free number for a peer ENet hands out: both tables are
 * peer_count long, and every numbered peer is one of ENet's. */
static NetPeerId number_peer(NetHost *h, ENetPeer *peer)
{
    for (NetPeerId id = 0; id < h->peer_count; id++) {
        if (!h->peers[id]) {
            h->peers[id] = peer;
            peer->data = &h->peers[id];
            return id;
        }
    }
    return NET_PEER_NONE;
}

static void unnumber_peer(NetHost *h, NetPeerId id)
{
    h->peers[id]->data = NULL;
    h->peers[id] = NULL;
}

/* ENet checks its own timeout only when a retransmission falls due, and the
 * gap between those doubles each time: asked for 5 s, it answers in 5 to 9.
 * net_poll() times connected peers out itself; this is what still ends a
 * connection attempt nobody answers. */
static void set_timeout(ENetPeer *peer)
{
    enet_peer_timeout(peer, 0, NET_PEER_TIMEOUT_MS, NET_PEER_TIMEOUT_MS);
}

NetPeerId net_connect(NetHost *h, const char *addr, uint16_t port)
{
    ENetAddress address;
    if (!h || !addr || enet_address_set_host(&address, addr) != 0) {
        return NET_PEER_NONE;
    }
    address.port = port;
    ENetPeer *peer = enet_host_connect(h->host, &address, NET_CH_COUNT, 0);
    if (!peer) {
        return NET_PEER_NONE;
    }
    NetPeerId id = number_peer(h, peer);
    if (id == NET_PEER_NONE) {
        enet_peer_reset(peer);
        return NET_PEER_NONE;
    }
    set_timeout(peer);
    return id;
}

void net_disconnect(NetHost *h, NetPeerId id)
{
    ENetPeer *peer = peer_at(h, id);
    if (!peer) {
        return;
    }
    unnumber_peer(h, id);
    enet_peer_disconnect_later(peer, 0);
}

static void release_packet(NetHost *h)
{
    if (h->packet) {
        enet_packet_destroy(h->packet);
        h->packet = NULL;
    }
}

void net_destroy(NetHost *h)
{
    if (!h) {
        return;
    }
    release_packet(h);
    /* Every peer ENet still holds, numbered or being let go: without this the
     * other side would find out only by its timeout. */
    for (size_t i = 0; i < h->host->peerCount; i++) {
        enet_peer_disconnect_now(&h->host->peers[i], 0);
    }
    enet_host_destroy(h->host);
    free(h);
    enet_release();
}

/* An unreliable packet longer than a datagram would otherwise go out in
 * reliable fragments: a late snapshot instead of a lost one. */
static ENetPacket *packet_create(const void *data, size_t size, int reliable)
{
    return enet_packet_create(data, size,
                              reliable ? ENET_PACKET_FLAG_RELIABLE
                                       : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
}

void net_send(NetHost *h, NetPeerId id, int channel, const void *data, size_t size, int reliable)
{
    ENetPeer *peer = peer_at(h, id);
    if (!peer || channel < 0 || channel >= NET_CH_COUNT) {
        return;
    }
    ENetPacket *packet = packet_create(data, size, reliable);
    /* A peer still connecting refuses it, and then the packet is still ours. */
    if (packet && enet_peer_send(peer, (enet_uint8)channel, packet) != 0 &&
        packet->referenceCount == 0) {
        enet_packet_destroy(packet);
    }
}

void net_broadcast(NetHost *h, int channel, const void *data, size_t size, int reliable)
{
    if (!h || channel < 0 || channel >= NET_CH_COUNT) {
        return;
    }
    ENetPacket *packet = packet_create(data, size, reliable);
    if (packet) {
        enet_host_broadcast(h->host, (enet_uint8)channel, packet);
    }
}

/* A connected peer is pinged at least every half second and acknowledges it,
 * so the time since its last acknowledgement is how long it has been gone,
 * measured as finely as the host is polled. A client's peer has acknowledged
 * nothing yet right after connecting (ENet takes the server's answer as the
 * acknowledgement and does not time it); it is skipped until it does, a round
 * trip later, and ENet's own timeout covers that moment. */
static int poll_timeouts(NetHost *h, NetTransportEvent *out)
{
    for (NetPeerId id = 0; id < h->peer_count; id++) {
        ENetPeer *peer = h->peers[id];
        if (!peer || peer->state != ENET_PEER_STATE_CONNECTED || peer->lastReceiveTime == 0 ||
            ENET_TIME_DIFFERENCE(h->host->serviceTime, peer->lastReceiveTime) <
                    NET_PEER_TIMEOUT_MS) {
            continue;
        }
        unnumber_peer(h, id);
        enet_peer_disconnect_now(peer, 0);
        out->type = NET_EV_DISCONNECT;
        out->peer = id;
        return 1;
    }
    return 0;
}

int net_poll(NetHost *h, NetTransportEvent *out)
{
    if (!out) {
        return 0;
    }
    out->type = NET_EV_NONE;
    out->peer = NET_PEER_NONE;
    out->data = NULL;
    out->size = 0;
    if (!h) {
        return 0;
    }
    release_packet(h);

    ENetEvent ev;
    while (enet_host_service(h->host, &ev, 0) > 0) {
        NetPeerId id = peer_id(h, ev.peer);
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                /* net_connect() numbered its own peer; one that came in gets
                 * its number now. */
                if (id == NET_PEER_NONE) {
                    id = number_peer(h, ev.peer);
                }
                if (id == NET_PEER_NONE) {
                    enet_peer_reset(ev.peer);
                    break;
                }
                set_timeout(ev.peer);
                out->type = NET_EV_CONNECT;
                out->peer = id;
                return 1;
            case ENET_EVENT_TYPE_DISCONNECT:
                if (id == NET_PEER_NONE) {
                    break; /* net_disconnect() already let it go */
                }
                unnumber_peer(h, id);
                out->type = NET_EV_DISCONNECT;
                out->peer = id;
                return 1;
            case ENET_EVENT_TYPE_RECEIVE:
                /* ENet delivers data only from a connected peer, and that has
                 * a number; one being let go is not connected any more. */
                if (id == NET_PEER_NONE) {
                    enet_packet_destroy(ev.packet);
                    break;
                }
                h->packet = ev.packet;
                out->type = NET_EV_DATA;
                out->peer = id;
                out->data = ev.packet->data;
                out->size = ev.packet->dataLength;
                return 1;
            case ENET_EVENT_TYPE_NONE: break;
        }
    }
    return poll_timeouts(h, out);
}

float net_peer_rtt(const NetHost *h, NetPeerId id)
{
    const ENetPeer *peer = peer_at(h, id);
    return peer ? (float)peer->roundTripTime : 0.0f;
}

float net_peer_loss(const NetHost *h, NetPeerId id)
{
    const ENetPeer *peer = peer_at(h, id);
    return peer ? (float)peer->packetLoss / (float)ENET_PEER_PACKET_LOSS_SCALE : 0.0f;
}
