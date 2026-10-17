#include "net_client.h"
#include "net_protocol.h"
#include "net_transport.h"
#include "world.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MSG_ACCEPT names the map by its file name, and the client looks for it where
 * its own copy of the game keeps maps: both ends are the same build. */
#define MAP_DIR "assets/maps/"

struct Client {
    NetHost *net;
    NetPeerId server; /* NET_PEER_NONE once the connection has ended */
    ClientState state;
    char reason[64];       /* empty while nothing has gone wrong */
    uint32_t connect_time; /* net_time() at client_connect() */
    int linked;            /* the transport connection is up */
    int accepted;          /* MSG_ACCEPT came: the map is loaded, local_player set */
    int has_world;         /* ... and a snapshot has been put into it */
    uint32_t input_tick;   /* the number the next input goes out with */
    ClientStats stats;
    World world;
    MsgSnapshot snapshot;
};

Client *client_create(void)
{
    Client *c = calloc(1, sizeof(*c));
    if (c) {
        c->server = NET_PEER_NONE;
        c->state = CL_DISCONNECTED;
        c->world.local_player = -1;
    }
    return c;
}

void client_destroy(Client *c)
{
    if (!c) {
        return;
    }
    net_destroy(c->net);
    free(c);
}

/* Let the server go, and say why. Before the first snapshot that is a failed
 * join; after it, a lost game. */
static void client_end(Client *c, const char *reason)
{
    if (c->server != NET_PEER_NONE) {
        net_disconnect(c->net, c->server);
        c->server = NET_PEER_NONE;
    }
    c->state = c->state == CL_CONNECTED ? CL_DISCONNECTED : CL_REJECTED;
    snprintf(c->reason, sizeof(c->reason), "%s", reason);
}

int client_connect(Client *c, const char *addr, uint16_t port)
{
    if (c->state == CL_CONNECTING || c->state == CL_CONNECTED) {
        return -1;
    }
    /* A fresh host for every connection: the one peer of the last host may
     * still be on its way out. */
    net_destroy(c->net);
    c->net = net_host_client();
    c->server = c->net ? net_connect(c->net, addr, port) : NET_PEER_NONE;
    c->state = CL_CONNECTING;
    c->reason[0] = '\0';
    c->connect_time = net_time();
    c->linked = 0;
    c->accepted = 0;
    c->has_world = 0;
    c->input_tick = 0;
    memset(&c->stats, 0, sizeof(c->stats));
    c->world.local_player = -1;
    if (c->server == NET_PEER_NONE) {
        client_end(c, c->net ? "the address does not resolve" : "the network does not start");
        return -1;
    }
    return 0;
}

/* MsgJoin.name stays empty: nothing names a player yet. */
static void client_send_join(Client *c)
{
    MsgJoin msg;
    memset(&msg, 0, sizeof(msg));
    msg.h.type = MSG_JOIN;
    msg.proto = PROTO_VERSION;
    net_send(c->net, c->server, NET_CH_RELIABLE, &msg, sizeof(msg), 1);
    c->stats.bytes_out += sizeof(msg);
}

/* A file name, as MSG_ACCEPT promises, and not a path that would lead out of
 * MAP_DIR. */
static int map_name_valid(const char *name)
{
    const char *end = memchr(name, '\0', NET_MAP_MAX);
    return end && end != name && !strpbrk(name, "/\\");
}

static void client_on_accept(Client *c, const void *data, size_t size)
{
    MsgAccept msg;
    if (c->accepted || size != sizeof(msg)) {
        return;
    }
    memcpy(&msg, data, sizeof(msg));
    if (msg.your_id >= MAX_PLAYERS || !map_name_valid(msg.map)) {
        client_end(c, "the server's answer makes no sense");
        return;
    }
    char path[sizeof(MAP_DIR) + NET_MAP_MAX];
    snprintf(path, sizeof(path), MAP_DIR "%s", msg.map);
    if (world_load(&c->world, path) != 0) {
        char reason[sizeof(c->reason)];
        snprintf(reason, sizeof(reason), "no map %s here", msg.map);
        client_end(c, reason);
        return;
    }
    /* The seed is not used: shotgun spread is rolled on the server alone. */
    c->world.local_player = msg.your_id;
    c->accepted = 1;
}

/* Read leniently: a server of another version may send a longer MsgReject,
 * and the reason is the one thing its player has to know. */
static void client_on_reject(Client *c, const uint8_t *data, size_t size)
{
    if (c->accepted || size < sizeof(MsgReject)) {
        return;
    }
    switch (data[offsetof(MsgReject, reason)]) {
        case REJECT_SERVER_FULL: client_end(c, "the server is full"); break;
        case REJECT_VERSION: client_end(c, "the server runs another version of the game"); break;
        default: client_end(c, "the server turned the client away"); break;
    }
}

/* Snapshots travel unreliably, so one may be lost, come twice or come after a
 * newer one; only a newer one is put into the world. Its events go after
 * those of the snapshots this poll has already applied. */
static void client_on_snapshot(Client *c, const void *data, size_t size)
{
    if (!c->accepted || snapshot_decode(&c->snapshot, data, size) != 0) {
        return;
    }
    if (c->has_world && (int32_t)(c->snapshot.tick - c->world.tick) <= 0) {
        return;
    }
    EventQueue earlier = c->world.events;
    snapshot_apply(&c->world, &c->snapshot);
    for (int i = 0; i < c->world.events.count && earlier.count < MAX_EVENTS; i++) {
        earlier.items[earlier.count++] = c->world.events.items[i];
    }
    c->world.events = earlier;
    c->has_world = 1;
    c->state = CL_CONNECTED;
}

static void client_on_data(Client *c, const void *data, size_t size)
{
    MsgHeader h;
    if (size < sizeof(h)) {
        return;
    }
    memcpy(&h, data, sizeof(h));
    switch (h.type) {
        case MSG_ACCEPT: client_on_accept(c, data, size); break;
        case MSG_REJECT: client_on_reject(c, data, size); break;
        case MSG_SNAPSHOT:
            c->stats.snapshots++;
            client_on_snapshot(c, data, size);
            break;
        case MSG_DISCONNECT: client_end(c, "the server closed the game"); break;
        default: break;
    }
}

void client_poll(Client *c)
{
    if (!c->net) {
        return;
    }
    event_queue_clear(&c->world.events);
    /* The host has one peer, the server, and once client_end() has let it go
     * the transport reports nothing more of it. */
    NetTransportEvent ev;
    while (net_poll(c->net, &ev)) {
        if (ev.type == NET_EV_CONNECT) {
            c->linked = 1;
            client_send_join(c);
        } else if (ev.type == NET_EV_DISCONNECT) {
            /* The number is free already: nothing to let go. */
            c->server = NET_PEER_NONE;
            client_end(c, c->state == CL_CONNECTED ? "the connection to the server is lost"
                          : c->linked              ? "the server closed the connection"
                                                   : "the server did not answer");
        } else if (ev.type == NET_EV_DATA) {
            c->stats.bytes_in += ev.size;
            client_on_data(c, ev.data, ev.size);
        }
    }
    if (c->state == CL_CONNECTING &&
        (uint32_t)(net_time() - c->connect_time) >= NET_CONNECT_TIMEOUT_MS) {
        client_end(c, "the server did not answer");
    }
}

void client_send_input(Client *c, const PlayerInput *in)
{
    if (c->state != CL_CONNECTED) {
        return;
    }
    MsgInput msg;
    memset(&msg, 0, sizeof(msg));
    msg.h.type = MSG_INPUT;
    msg.tick = c->input_tick++;
    msg.buttons = in->buttons;
    msg.angle = in->angle;
    net_send(c->net, c->server, NET_CH_UNRELIABLE, &msg, sizeof(msg), 0);
    c->stats.bytes_out += sizeof(msg);
}

ClientState client_state(const Client *c)
{
    return c->state;
}

const char *client_reason(const Client *c)
{
    return c->reason[0] ? c->reason : NULL;
}

World *client_world(Client *c)
{
    return c->has_world ? &c->world : NULL;
}

int client_local_player(const Client *c)
{
    return c->accepted ? c->world.local_player : -1;
}

ClientStats client_stats(const Client *c)
{
    return c->stats;
}
