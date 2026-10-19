#include "net_server.h"
#include "net_protocol.h"
#include "net_transport.h"
#include "world.h"
#include "config.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Events the ticks between two snapshots can produce at the lowest snapshot
 * rate. world_step() clears World.events on entry, so the server keeps its own
 * copy of every tick's from one snapshot to the next. */
#define EVENT_BACKLOG (((TICK_RATE + SNAPSHOT_RATE_MIN - 1) / SNAPSHOT_RATE_MIN) * MAX_EVENTS)

/* Buttons that ask for something to happen once. The client hands each press
 * to exactly one tick (input_sample()), and the server keeps it that way: a
 * press is never lost under a newer input that came in the same tick, nor
 * repeated when no input comes. */
#define BTN_ONCE                                                                                   \
    (BTN_FIRE | BTN_USE | BTN_WEAPON1 | BTN_WEAPON2 | BTN_WEAPON3 | BTN_WEAPON4 | BTN_WEAPON5)

/* Buttons that are held, and so still held while an input is late. The trigger
 * is both: a click fires once, a held trigger keeps firing. */
#define BTN_HELD (BTN_FORWARD | BTN_BACK | BTN_LEFT | BTN_RIGHT | BTN_FIRE)

/* The facing a client may send. Its own is kept in [-PI, PI] (input.h);
 * anything far out of that is not a facing, and wrap_angle() would never get
 * an infinity or a 1e30 back into range. */
#define ANGLE_MAX (2.0f * MATH_PI)

/* A peer of the server, by its NetPeerId. */
typedef struct {
    int used;   /* a peer is connected under this number */
    int joined; /* ... and has a player, MSG_ACCEPT went out */
    int fresh;  /* an input came since the last tick */
    int has_input;
    uint8_t player_id;
    PlayerInput last_input;   /* the newest the client has sent */
    uint16_t pending;         /* BTN_ONCE presses of every input since the last tick */
    uint32_t last_input_tick; /* the newest input a tick has used, for the snapshot */
    float time_since_input;   /* seconds since last_input came */
} ServerClient;

struct Server {
    World world;
    NetHost *net;
    ServerClient clients[NET_MAX_PEERS];
    int local_player;
    int snapshot_rate;
    int snapshot_clock; /* gains snapshot_rate a tick; a snapshot is due at TICK_RATE */
    uint32_t seed;
    char map[NET_MAP_MAX];
    GameEvent backlog[EVENT_BACKLOG];
    int backlog_count;
    MsgSnapshot snapshot;
    uint8_t wire[sizeof(MsgSnapshot)];
};

/* The file name of `path`, which is what MSG_ACCEPT names the map by. */
static const char *file_name(const char *path)
{
    const char *name = path;
    for (const char *c = path; *c; c++) {
        if (*c == '/' || *c == '\\') {
            name = c + 1;
        }
    }
    return name;
}

Server *server_create(uint16_t port, const char *map_path)
{
    const char *map = file_name(map_path);
    if (map[0] == '\0' || strlen(map) >= NET_MAP_MAX) {
        return NULL;
    }
    Server *s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    if (world_load(&s->world, map_path) != 0) {
        free(s);
        return NULL;
    }
    /* Room for one peer more than there are players, so that a client who
     * finds the server full is let in long enough to be told so. */
    s->net = net_host_server(port, NET_MAX_PEERS);
    if (!s->net) {
        free(s);
        return NULL;
    }
    strcpy(s->map, map);
    s->local_player = -1;
    s->snapshot_rate = SNAPSHOT_RATE;
    s->seed = (uint32_t)time(NULL);
    srand(s->seed);
    return s;
}

/* A client in the game is told why it ended — the server closed it rather than
 * lost the connection; one still joining is only let go, by net_destroy(). */
void server_destroy(Server *s)
{
    if (!s) {
        return;
    }
    MsgHeader bye;
    memset(&bye, 0, sizeof(bye));
    bye.type = MSG_DISCONNECT;
    for (NetPeerId peer = 0; peer < NET_MAX_PEERS; peer++) {
        if (s->clients[peer].joined) {
            net_send(s->net, peer, NET_CH_RELIABLE, &bye, sizeof(bye), 1);
            net_disconnect(s->net, peer);
        }
    }
    net_destroy(s->net);
    free(s);
}

/* The one way into the game, for a client over the network and the local
 * player of a listen-server alike: the lowest free slot, at the spawn point
 * world_pick_spawn() finds safest for whoever is in the world now. */
static int server_add_player(Server *s)
{
    return world_add_player(&s->world, NULL);
}

int server_add_local_player(Server *s)
{
    if (s->local_player < 0) {
        s->local_player = server_add_player(s);
        s->world.local_player = s->local_player;
    }
    return s->local_player;
}

int server_local_player(const Server *s)
{
    return s->local_player;
}

World *server_world(Server *s)
{
    return &s->world;
}

void server_set_snapshot_rate(Server *s, int hz)
{
    s->snapshot_rate = hz < SNAPSHOT_RATE_MIN ? SNAPSHOT_RATE_MIN : hz > TICK_RATE ? TICK_RATE : hz;
}

/* Send `reason` and let the peer go. The transport delivers the reject before
 * the disconnect and reports nothing more for this number. */
static void server_reject(Server *s, NetPeerId peer, RejectReason reason)
{
    MsgReject msg;
    memset(&msg, 0, sizeof(msg));
    msg.h.type = MSG_REJECT;
    msg.reason = (uint8_t)reason;
    net_send(s->net, peer, NET_CH_RELIABLE, &msg, sizeof(msg), 1);
    net_disconnect(s->net, peer);
    memset(&s->clients[peer], 0, sizeof(s->clients[peer]));
}

static void server_on_join(Server *s, NetPeerId peer, const uint8_t *data, size_t size)
{
    ServerClient *c = &s->clients[peer];
    if (c->joined) {
        return;
    }
    /* The version first, on its own: a client of another version may lay out
     * the rest of MsgJoin differently, and is still owed a clear answer. */
    uint16_t proto;
    if (size < offsetof(MsgJoin, proto) + sizeof(proto)) {
        return;
    }
    memcpy(&proto, data + offsetof(MsgJoin, proto), sizeof(proto));
    if (proto != PROTO_VERSION) {
        server_reject(s, peer, REJECT_VERSION);
        return;
    }
    if (size != sizeof(MsgJoin)) {
        return;
    }

    int id = server_add_player(s);
    if (id < 0) {
        server_reject(s, peer, REJECT_SERVER_FULL);
        return;
    }
    c->joined = 1;
    c->player_id = (uint8_t)id;

    MsgAccept msg;
    memset(&msg, 0, sizeof(msg));
    msg.h.type = MSG_ACCEPT;
    msg.your_id = (uint8_t)id;
    msg.seed = s->seed;
    memcpy(msg.map, s->map, sizeof(msg.map));
    net_send(s->net, peer, NET_CH_RELIABLE, &msg, sizeof(msg), 1);
}

/* Inputs travel unreliably, so one may be lost, come twice or come after a
 * newer one; only a newer one is taken. Its presses are added to the ones
 * the next tick has not used yet, rather than replacing them. */
static void server_on_input(Server *s, NetPeerId peer, const uint8_t *data, size_t size)
{
    ServerClient *c = &s->clients[peer];
    MsgInput msg;
    if (!c->joined || size != sizeof(msg)) {
        return;
    }
    memcpy(&msg, data, sizeof(msg));
    if (!(fabsf(msg.angle) <= ANGLE_MAX)) {
        return;
    }
    if (c->has_input && msg.tick <= c->last_input.tick) {
        return;
    }
    c->last_input.tick = msg.tick;
    c->last_input.buttons = msg.buttons;
    c->last_input.angle = msg.angle;
    c->pending |= msg.buttons & BTN_ONCE;
    c->has_input = 1;
    c->fresh = 1;
    c->time_since_input = 0.0f;
}

static void server_on_data(Server *s, NetPeerId peer, const void *data, size_t size)
{
    MsgHeader h;
    if (!s->clients[peer].used || size < sizeof(h)) {
        return;
    }
    memcpy(&h, data, sizeof(h));
    switch (h.type) {
        case MSG_JOIN: server_on_join(s, peer, data, size); break;
        case MSG_INPUT: server_on_input(s, peer, data, size); break;
        default: break;
    }
}

static void server_on_disconnect(Server *s, NetPeerId peer)
{
    ServerClient *c = &s->clients[peer];
    if (c->joined) {
        world_remove_player(&s->world, c->player_id);
    }
    memset(c, 0, sizeof(*c));
}

/* The host holds NET_MAX_PEERS peers, numbered from 0, so every number
 * net_poll() reports is an index into clients[]. */
static void server_poll(Server *s)
{
    NetTransportEvent ev;
    while (net_poll(s->net, &ev)) {
        if (ev.type == NET_EV_CONNECT) {
            /* Nothing to do until its MSG_JOIN. */
            memset(&s->clients[ev.peer], 0, sizeof(s->clients[ev.peer]));
            s->clients[ev.peer].used = 1;
        } else if (ev.type == NET_EV_DISCONNECT) {
            server_on_disconnect(s, ev.peer);
        } else if (ev.type == NET_EV_DATA) {
            server_on_data(s, ev.peer, ev.data, ev.size);
        }
    }
}

/* What client `c` does in the coming tick. A new input brings its own
 * buttons and every press since the last tick. With none, the last one goes
 * on — a lost packet must not stop a running player for a tick — but only
 * what is held: a press already had its tick. Silent for NET_INPUT_TIMEOUT,
 * the client stands, still facing where it last faced. */
static PlayerInput client_input(const ServerClient *c, const PlayerState *p, uint32_t tick)
{
    PlayerInput in = {tick, 0, p->angle};
    if (!c->has_input) {
        return in;
    }
    in.angle = c->last_input.angle;
    if (c->fresh) {
        in.buttons = c->last_input.buttons | c->pending;
    } else if (c->time_since_input <= NET_INPUT_TIMEOUT) {
        in.buttons = c->last_input.buttons & BTN_HELD;
    }
    return in;
}

/* Every client gets a snapshot of its own: last_input_tick is the receiver's,
 * and so are the events — picked from the backlog of every tick since the
 * last snapshot for what its player hears, theirs first and nearest next. */
static void server_send_snapshots(Server *s)
{
    snapshot_write(&s->snapshot, &s->world, 0);
    for (NetPeerId peer = 0; peer < NET_MAX_PEERS; peer++) {
        const ServerClient *c = &s->clients[peer];
        if (!c->joined) {
            continue;
        }
        snapshot_pick_events(&s->snapshot, s->backlog, s->backlog_count,
                             &s->world.players[c->player_id]);
        s->snapshot.last_input_tick = c->last_input_tick;
        size_t size = snapshot_encode(&s->snapshot, s->wire);
        if (size > 0) {
            net_send(s->net, peer, NET_CH_UNRELIABLE, s->wire, size, 0);
        }
    }
    s->backlog_count = 0;
}

void server_tick(Server *s, const PlayerInput *local, double dt)
{
    World *w = &s->world;
    server_poll(s);

    /* Indexed by player id, as world_step() reads it; a slot nobody drives
     * stands still. */
    PlayerInput inputs[MAX_PLAYERS];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerInput idle = {w->tick, 0, w->players[i].angle};
        inputs[i] = idle;
    }
    for (NetPeerId peer = 0; peer < NET_MAX_PEERS; peer++) {
        const ServerClient *c = &s->clients[peer];
        if (c->joined) {
            inputs[c->player_id] = client_input(c, &w->players[c->player_id], w->tick);
        }
    }
    if (local && s->local_player >= 0) {
        inputs[s->local_player] = *local;
    }

    world_step(w, inputs, MAX_PLAYERS, dt);

    for (NetPeerId peer = 0; peer < NET_MAX_PEERS; peer++) {
        ServerClient *c = &s->clients[peer];
        if (!c->has_input) {
            continue;
        }
        if (c->fresh) {
            c->last_input_tick = c->last_input.tick;
        }
        c->fresh = 0;
        c->pending = 0;
        c->time_since_input += (float)dt;
    }

    /* Dropped past the end, as event_push() drops them: a lost sound must
     * not change anything else. */
    for (int i = 0; i < w->events.count && s->backlog_count < EVENT_BACKLOG; i++) {
        s->backlog[s->backlog_count++] = w->events.items[i];
    }

    /* Counted in whole ticks rather than seconds: 1/60 s summed in floating
     * point drifts against 1/20 s — six ticks already come to less than two
     * intervals — and now and then a snapshot would slip a tick. */
    s->snapshot_clock += s->snapshot_rate;
    if (s->snapshot_clock >= TICK_RATE) {
        s->snapshot_clock -= TICK_RATE;
        server_send_snapshots(s);
    }

    /* Out now rather than with the next tick's poll, a tick later. */
    server_poll(s);
}
