/* net_server.c over real sockets on 127.0.0.1: the server and the clients it
 * serves live in this one process and are polled in turn. A client here is a
 * bot that speaks the protocol by hand — a bare transport host that sends
 * MsgJoin and MsgInput and keeps what comes back.
 *
 * Which tick a datagram reaches the server in is up to the operating system,
 * so no test depends on it: snapshots are told apart by the World.tick they
 * carry, and the input tests check what holds whichever tick an input lands
 * in. Timeouts are tested by moving ENet's clock with enet_time_set(), as in
 * test_net_transport.c — which is also why this file includes enet.h.
 *
 * The map is written out by the test, as in replay_test.c. */

#include <enet/enet.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "net_protocol.h"
#include "net_server.h"
#include "net_transport.h"
#include "world.h"
#include "unity.h"

/* Clear of NET_PORT_DEFAULT and of test_net_transport.c, which ctest -j may
 * run alongside. */
#define TEST_PORT 27116

/* Long enough for anything on loopback, short enough to fail fast. */
#define TEST_WAIT_MS 2000

#define MAP_NAME "net_server_map.txt"

#define MAX_BOTS (MAX_PLAYERS + 1)
#define RECENT 16 /* snapshots a bot keeps, the newest ones */

/* IPv4 and UDP headers and ENet's own around one unreliable packet: what a
 * snapshot costs on the wire beyond its bytes. */
#define DATAGRAM_OVERHEAD (20 + 8 + 12)

/* A door right in front of the start cell, and room for ten players and
 * MAX_ENEMIES enemies besides.
 *
 *   0123456789012345
 * 0 ################
 * 1 #PD............#
 * 2 #..............#
 * 3 #..............#
 * 4 #..............#
 * 5 #..............#
 * 6 ################ */
static const char *const map_rows[] = {
        "################", "#PD............#", "#..............#", "#..............#",
        "#..............#", "#..............#", "################",
};

typedef struct {
    NetHost *host;
    NetPeerId server; /* the number this bot knows the server by */
    int connected, gone, paused;
    int accepted, rejected;
    int closed; /* MSG_DISCONNECT came: the server closed the game */
    MsgAccept accept;
    MsgReject reject;
    int snapshots;         /* how many came */
    size_t snapshot_bytes; /* their size on the wire, summed */
    MsgSnapshot recent[RECENT];
} Bot;

static Server *server;
static Bot bots[MAX_BOTS];
static int bot_count;

static void write_map(void)
{
    FILE *f = fopen(MAP_NAME, "w");
    TEST_ASSERT_NOT_NULL(f);
    for (size_t i = 0; i < sizeof(map_rows) / sizeof(map_rows[0]); i++) {
        fprintf(f, "%s\n", map_rows[i]);
    }
    fclose(f);
}

void setUp(void)
{
    write_map();
    memset(bots, 0, sizeof(bots));
    bot_count = 0;
    server = server_create(TEST_PORT, "./" MAP_NAME);
    TEST_ASSERT_NOT_NULL(server);
}

void tearDown(void)
{
    for (int i = 0; i < bot_count; i++) {
        net_destroy(bots[i].host);
    }
    server_destroy(server);
}

static void poll_bot(Bot *b)
{
    NetTransportEvent ev;
    while (net_poll(b->host, &ev)) {
        if (ev.type == NET_EV_CONNECT) {
            b->connected = 1;
        } else if (ev.type == NET_EV_DISCONNECT) {
            b->gone = 1;
        } else if (ev.type == NET_EV_DATA) {
            uint8_t type = ((const uint8_t *)ev.data)[0];
            if (type == MSG_ACCEPT) {
                TEST_ASSERT_EQUAL_size_t(sizeof(MsgAccept), ev.size);
                memcpy(&b->accept, ev.data, sizeof(b->accept));
                b->accepted = 1;
            } else if (type == MSG_REJECT) {
                TEST_ASSERT_EQUAL_size_t(sizeof(MsgReject), ev.size);
                memcpy(&b->reject, ev.data, sizeof(b->reject));
                b->rejected = 1;
            } else if (type == MSG_DISCONNECT) {
                TEST_ASSERT_EQUAL_size_t(sizeof(MsgHeader), ev.size);
                b->closed = 1;
            } else {
                MsgSnapshot *snap = &b->recent[b->snapshots % RECENT];
                TEST_ASSERT_EQUAL_INT(0, snapshot_decode(snap, ev.data, ev.size));
                b->snapshots++;
                b->snapshot_bytes += ev.size;
            }
        }
    }
}

static void poll_bots(void)
{
    for (int i = 0; i < bot_count; i++) {
        if (bots[i].host && !bots[i].paused) {
            poll_bot(&bots[i]);
        }
    }
}

/* One tick of the server, then whatever it sent. */
static void tick(const PlayerInput *local)
{
    server_tick(server, local, FIXED_DT);
    poll_bots();
}

/* Tick until `cond` holds, for TEST_WAIT_MS of real time at most. */
#define TICK_UNTIL(cond)                                                                           \
    do {                                                                                           \
        uint32_t start_ = enet_time_get();                                                         \
        while (!(cond) && enet_time_get() - start_ < TEST_WAIT_MS) {                               \
            tick(NULL);                                                                            \
        }                                                                                          \
        TEST_ASSERT_TRUE_MESSAGE(cond, #cond " within TEST_WAIT_MS");                              \
    } while (0)

/* Wait for what the server has already sent, without ticking it again: a test
 * that counts snapshots must not have the wait add one. */
#define POLL_UNTIL(cond)                                                                           \
    do {                                                                                           \
        uint32_t start_ = enet_time_get();                                                         \
        while (!(cond) && enet_time_get() - start_ < TEST_WAIT_MS) {                               \
            poll_bots();                                                                           \
        }                                                                                          \
        TEST_ASSERT_TRUE_MESSAGE(cond, #cond " within TEST_WAIT_MS");                              \
    } while (0)

static void advance_clock(uint32_t ms)
{
    enet_time_set(enet_time_get() + ms);
}

static Bot *bot_connect(void)
{
    TEST_ASSERT_LESS_THAN_INT(MAX_BOTS, bot_count);
    Bot *b = &bots[bot_count++];
    b->host = net_host_client();
    TEST_ASSERT_NOT_NULL(b->host);
    b->server = net_connect(b->host, "127.0.0.1", TEST_PORT);
    TEST_ASSERT_NOT_EQUAL_INT(NET_PEER_NONE, b->server);
    TICK_UNTIL(b->connected);
    return b;
}

static void bot_send_join(Bot *b, uint16_t proto)
{
    MsgJoin msg;
    memset(&msg, 0, sizeof(msg));
    msg.h.type = MSG_JOIN;
    msg.proto = proto;
    strcpy(msg.name, "bot");
    net_send(b->host, b->server, NET_CH_RELIABLE, &msg, sizeof(msg), 1);
}

/* Connect, ask to join, and wait for the answer, whichever it is. */
static Bot *bot_join(void)
{
    Bot *b = bot_connect();
    bot_send_join(b, PROTO_VERSION);
    TICK_UNTIL(b->accepted || b->rejected);
    return b;
}

static Bot *bot_joined(void)
{
    Bot *b = bot_join();
    TEST_ASSERT_TRUE(b->accepted);
    return b;
}

/* Queue an input and send it at once, as a client does every tick. */
static void bot_send_input(Bot *b, uint32_t tick_no, uint16_t buttons, float angle)
{
    MsgInput msg;
    memset(&msg, 0, sizeof(msg));
    msg.h.type = MSG_INPUT;
    msg.tick = tick_no;
    msg.buttons = buttons;
    msg.angle = angle;
    net_send(b->host, b->server, NET_CH_UNRELIABLE, &msg, sizeof(msg), 0);
    poll_bot(b);
}

static const MsgSnapshot *bot_snapshot_at(const Bot *b, uint32_t world_tick)
{
    for (int i = 0; i < RECENT && i < b->snapshots; i++) {
        if (b->recent[i].tick == world_tick) {
            return &b->recent[i];
        }
    }
    return NULL;
}

static int bot_has_snapshot_at(const Bot *b, uint32_t world_tick)
{
    return bot_snapshot_at(b, world_tick) != NULL;
}

static int count_events(const MsgSnapshot *snap, GameEventKind kind, int actor)
{
    int n = 0;
    for (int i = 0; i < snap->event_count; i++) {
        if (snap->events[i].kind == kind && snap->events[i].actor == actor) {
            n++;
        }
    }
    return n;
}

/* Events of `kind` by `actor` in the snapshots `b` got since the *seen-th,
 * which then moves past them. */
static int events_since(const Bot *b, int *seen, GameEventKind kind, int actor)
{
    int n = 0;
    for (; *seen < b->snapshots; (*seen)++) {
        n += count_events(&b->recent[*seen % RECENT], kind, actor);
    }
    return n;
}

static double now_seconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ---- creating ---- */

static void test_create_refuses_what_it_cannot_serve(void)
{
    TEST_ASSERT_NULL(server_create(TEST_PORT + 1, "no_such_map.txt"));
    TEST_ASSERT_NULL(server_create(TEST_PORT + 1, "dir/a_map_name_too_long_for_msg_accept.txt"));
    TEST_ASSERT_NULL(server_create(TEST_PORT + 1, "maps/"));
    TEST_ASSERT_NULL(server_create(TEST_PORT, MAP_NAME)); /* the port is setUp()'s */
    server_destroy(NULL);
}

static void test_dedicated_server_has_no_local_player(void)
{
    TEST_ASSERT_EQUAL_INT(-1, server_local_player(server));
    TEST_ASSERT_EQUAL_INT(-1, server_world(server)->local_player);
    TEST_ASSERT_TRUE(server_world(server)->loaded);
}

/* ---- joining ---- */

static void test_join_is_accepted_with_id_seed_and_map(void)
{
    Bot *b = bot_joined();
    TEST_ASSERT_EQUAL_INT(MSG_ACCEPT, b->accept.h.type);
    TEST_ASSERT_EQUAL_INT(0, b->accept.your_id);
    TEST_ASSERT_EQUAL_STRING(MAP_NAME, b->accept.map);
    TEST_ASSERT_TRUE(server_world(server)->players[0].active);

    Bot *c = bot_joined();
    TEST_ASSERT_EQUAL_INT(1, c->accept.your_id);
    TEST_ASSERT_EQUAL_UINT32(b->accept.seed, c->accept.seed);
}

/* Shotgun spread is the one rand() of the simulation, and the server rolls it
 * from the seed it hands out. Nothing has fired yet, so the next number is the
 * first of that seed. */
static void test_rand_is_seeded_with_the_accepted_seed(void)
{
    Bot *b = bot_joined();
    int next = rand();
    srand(b->accept.seed);
    TEST_ASSERT_EQUAL_INT(rand(), next);
}

/* A client that asks again keeps the player it has, and gets no second one. */
static void test_second_join_changes_nothing(void)
{
    Bot *b = bot_joined();
    b->accepted = 0;
    bot_send_join(b, PROTO_VERSION);
    for (int i = 0; i < 10; i++) {
        tick(NULL);
    }
    TEST_ASSERT_FALSE(b->accepted || b->rejected);
    TEST_ASSERT_TRUE(server_world(server)->players[0].active);
    TEST_ASSERT_FALSE(server_world(server)->players[1].active);
}

/* Two players never start on one cell: each spawns at the cell of its id. */
static void test_joined_players_spawn_apart(void)
{
    bot_joined();
    bot_joined();
    const World *w = server_world(server);
    TEST_ASSERT_FALSE(w->players[0].x == w->players[1].x && w->players[0].y == w->players[1].y);
}

static void test_eleventh_client_is_rejected_as_full(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Bot *b = bot_joined();
        TEST_ASSERT_EQUAL_INT(i, b->accept.your_id);
    }
    Bot *extra = bot_join();
    TEST_ASSERT_TRUE(extra->rejected);
    TEST_ASSERT_EQUAL_INT(MSG_REJECT, extra->reject.h.type);
    TEST_ASSERT_EQUAL_INT(REJECT_SERVER_FULL, extra->reject.reason);
    TICK_UNTIL(extra->gone);

    const World *w = server_world(server);
    int active = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        active += w->players[i].active;
    }
    TEST_ASSERT_EQUAL_INT(MAX_PLAYERS, active);
}

/* The local player of a listen server takes a slot like anyone else. */
static void test_listen_server_counts_the_local_player(void)
{
    int local = server_add_local_player(server);
    TEST_ASSERT_EQUAL_INT(0, local);
    TEST_ASSERT_EQUAL_INT(local, server_add_local_player(server));
    TEST_ASSERT_EQUAL_INT(local, server_local_player(server));
    TEST_ASSERT_EQUAL_INT(local, server_world(server)->local_player);

    for (int i = 1; i < MAX_PLAYERS; i++) {
        TEST_ASSERT_EQUAL_INT(i, bot_joined()->accept.your_id);
    }
    Bot *extra = bot_join();
    TEST_ASSERT_TRUE(extra->rejected);
    TEST_ASSERT_EQUAL_INT(REJECT_SERVER_FULL, extra->reject.reason);
}

static void test_other_protocol_version_is_rejected(void)
{
    Bot *b = bot_connect();
    bot_send_join(b, PROTO_VERSION + 1);
    TICK_UNTIL(b->rejected);
    TEST_ASSERT_EQUAL_INT(REJECT_VERSION, b->reject.reason);
    TICK_UNTIL(b->gone);
    TEST_ASSERT_FALSE(b->accepted);
    TEST_ASSERT_FALSE(server_world(server)->players[0].active);
}

/* A client of another version may send a MsgJoin of another length; the
 * version alone is enough to turn it away clearly. */
static void test_other_version_with_a_shorter_join_is_rejected(void)
{
    Bot *b = bot_connect();
    uint8_t join[offsetof(MsgJoin, proto) + sizeof(uint16_t)] = {MSG_JOIN};
    uint16_t proto = PROTO_VERSION + 1;
    memcpy(join + offsetof(MsgJoin, proto), &proto, sizeof(proto));
    net_send(b->host, b->server, NET_CH_RELIABLE, join, sizeof(join), 1);
    TICK_UNTIL(b->rejected);
    TEST_ASSERT_EQUAL_INT(REJECT_VERSION, b->reject.reason);
}

/* Nothing malformed gets a player in or moves one. */
static void test_malformed_packets_are_ignored(void)
{
    Bot *b = bot_connect();
    uint8_t byte = MSG_JOIN;
    net_send(b->host, b->server, NET_CH_RELIABLE, &byte, 1, 1);
    MsgJoin join;
    memset(&join, 0, sizeof(join));
    join.h.type = MSG_JOIN;
    join.proto = PROTO_VERSION;
    net_send(b->host, b->server, NET_CH_RELIABLE, &join, sizeof(join) - 1, 1);
    MsgHeader bare = {MSG_JOIN, {0}}; /* no version in it */
    net_send(b->host, b->server, NET_CH_RELIABLE, &bare, sizeof(bare), 1);
    MsgHeader unknown = {MSG_SNAPSHOT, {0}}; /* not something a client sends */
    net_send(b->host, b->server, NET_CH_RELIABLE, &unknown, sizeof(unknown), 1);
    bot_send_input(b, 1, BTN_FORWARD, 0.0f); /* before joining */
    for (int i = 0; i < 10; i++) {
        tick(NULL);
    }
    TEST_ASSERT_FALSE(b->accepted || b->rejected);
    TEST_ASSERT_FALSE(server_world(server)->players[0].active);

    bot_send_join(b, PROTO_VERSION);
    TICK_UNTIL(b->accepted);
    const PlayerState *p = &server_world(server)->players[b->accept.your_id];
    float x = p->x, y = p->y, angle = p->angle;
    bot_send_input(b, 2, BTN_FORWARD, NAN);
    bot_send_input(b, 3, BTN_FORWARD, INFINITY);
    bot_send_input(b, 4, BTN_FORWARD, 1e30f);
    for (int i = 0; i < 10; i++) {
        tick(NULL);
    }
    TEST_ASSERT_EQUAL_FLOAT(x, p->x);
    TEST_ASSERT_EQUAL_FLOAT(y, p->y);
    TEST_ASSERT_EQUAL_FLOAT(angle, p->angle);
}

/* ---- leaving ---- */

static void test_client_that_quits_leaves_the_world(void)
{
    Bot *b = bot_joined();
    const PlayerState *p = &server_world(server)->players[b->accept.your_id];
    net_destroy(b->host);
    b->host = NULL;
    TICK_UNTIL(!p->active);
}

/* A server that stops says so to every player, who then knows the game was
 * closed rather than the connection lost. A client that has not joined has no
 * game to be told about and is only let go. */
static void test_destroyed_server_tells_every_player_it_closed_the_game(void)
{
    Bot *a = bot_joined();
    Bot *b = bot_joined();
    Bot *idle = bot_connect();
    server_destroy(server);
    server = NULL;
    POLL_UNTIL(a->closed && b->closed && idle->gone);
    TEST_ASSERT_FALSE(idle->closed);
}

/* What kill -9 of a client looks like: silence. The player stays until the
 * timeout, then goes, and the next client to join gets the slot. */
static void test_killed_client_leaves_within_the_timeout_and_frees_its_slot(void)
{
    Bot *b = bot_joined();
    int id = b->accept.your_id;
    const PlayerState *p = &server_world(server)->players[id];
    /* The bot acknowledged MSG_ACCEPT on its way out of bot_joined(); the
     * server reads that before the silence starts, not after the clock has
     * moved, when it would count as heard from just now. */
    b->paused = 1;
    for (int i = 0; i < 5; i++) {
        tick(NULL);
    }

    advance_clock(NET_PEER_TIMEOUT_MS - 1000);
    for (int i = 0; i < 20; i++) {
        tick(NULL);
    }
    TEST_ASSERT_TRUE(p->active);

    advance_clock(1500);
    TICK_UNTIL(!p->active);

    TEST_ASSERT_EQUAL_INT(id, bot_joined()->accept.your_id);
}

/* ---- input ---- */

/* An input that comes after a newer one is stale and is dropped: the facing
 * stays the newer one's, and the snapshot reports the newer one as used. */
static void test_older_input_is_ignored(void)
{
    Bot *b = bot_joined();
    const PlayerState *p = &server_world(server)->players[b->accept.your_id];
    bot_send_input(b, 100, 0, 1.0f);
    bot_send_input(b, 99, 0, -1.0f);
    for (int i = 0; i < 5; i++) {
        tick(NULL);
    }
    TEST_ASSERT_EQUAL_FLOAT(1.0f, p->angle);

    int seen = b->snapshots;
    TICK_UNTIL(b->snapshots > seen);
    TEST_ASSERT_EQUAL_UINT32(100, b->recent[(b->snapshots - 1) % RECENT].last_input_tick);
}

/* A lost input is covered by the one before it: a running player keeps the
 * same pace through the gap instead of stopping and catching up. */
static void test_lost_input_does_not_stall_the_player(void)
{
    Bot *b = bot_joined();
    const PlayerState *p = &server_world(server)->players[b->accept.your_id];
    float step = MOVE_SPEED * (float)FIXED_DT;
    float down = MATH_PI / 2.0f;

    float y = p->y;
    for (uint32_t t = 1; t <= 12; t++) {
        if (t != 6) {
            bot_send_input(b, t, BTN_FORWARD, down);
        }
        tick(NULL);
        if (t >= 3) {
            TEST_ASSERT_FLOAT_WITHIN(1e-4f, step, p->y - y);
        }
        y = p->y;
    }
}

/* Silent for NET_INPUT_TIMEOUT, the player stops where it got to and keeps
 * facing the same way. */
static void test_silent_client_stands_still_after_the_timeout(void)
{
    Bot *b = bot_joined();
    const PlayerState *p = &server_world(server)->players[b->accept.your_id];
    float down = MATH_PI / 2.0f;
    float start = p->y;

    bot_send_input(b, 1, BTN_FORWARD, down);
    int timeout_ticks = (int)(NET_INPUT_TIMEOUT * TICK_RATE);
    for (int i = 0; i < timeout_ticks + 5; i++) {
        tick(NULL);
    }
    float stopped = p->y;
    for (int i = 0; i < 20; i++) {
        tick(NULL);
    }
    TEST_ASSERT_EQUAL_FLOAT(stopped, p->y);
    float step = MOVE_SPEED * (float)FIXED_DT;
    TEST_ASSERT_TRUE(stopped - start >= step * (float)(timeout_ticks - 1));
    TEST_ASSERT_EQUAL_FLOAT(down, p->angle);
}

/* E pressed in an input that a newer one overtakes before a tick reads it: the
 * press still reaches that tick, and the door opens. */
static void test_press_is_not_lost_under_a_newer_input(void)
{
    Bot *b = bot_joined();
    const Door *d = &server_world(server)->doors.doors[0];
    bot_send_input(b, 1, BTN_USE, 0.0f);
    bot_send_input(b, 2, 0, 0.0f);
    for (int i = 0; i < 5; i++) {
        tick(NULL);
    }
    TEST_ASSERT_EQUAL_INT(DOOR_OPENING, d->state);
}

/* E pressed once and nothing after: the ticks that repeat the input carry no
 * second press, which would close the door again. */
static void test_press_is_not_repeated_while_input_is_late(void)
{
    Bot *b = bot_joined();
    const Door *d = &server_world(server)->doors.doors[0];
    bot_send_input(b, 1, BTN_USE, 0.0f);
    /* Every tick, not just the last: a press repeated on each tick toggles
     * the door back and forth and lands on opening every other tick. */
    TICK_UNTIL(d->state != DOOR_CLOSED);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(DOOR_OPENING, d->state);
        tick(NULL);
    }
    TEST_ASSERT_EQUAL_INT(DOOR_OPENING, d->state);
}

/* ---- snapshots ---- */

/* The events of a tick go to each client as its own player hears them. Both
 * players here pull the trigger on an empty pistol: the local player every
 * tick, the bot once. The bot hears its own click, and never the local
 * player's — each of the snapshots it gets has its own pick of one backlog. */
static void test_each_client_hears_its_own_empty_click_alone(void)
{
    int local = server_add_local_player(server);
    Bot *b = bot_joined();
    World *w = server_world(server);
    int id = b->accept.your_id;
    w->players[local].weapons.weapons[WEAPON_PISTOL].ammo = 0;
    w->players[id].weapons.weapons[WEAPON_PISTOL].ammo = 0;

    bot_send_input(b, 1, BTN_FIRE, 0.0f);
    int own = 0;
    int seen = b->snapshots;
    uint32_t start = enet_time_get();
    while (own == 0 && enet_time_get() - start < TEST_WAIT_MS) {
        PlayerInput in = {w->tick, BTN_FIRE, w->players[local].angle};
        int from = seen;
        tick(&in);
        own += events_since(b, &from, EV_NO_AMMO, id);
        TEST_ASSERT_EQUAL_INT(0, events_since(b, &seen, EV_NO_AMMO, local));
    }
    TEST_ASSERT_TRUE_MESSAGE(own > 0, "the bot's own click within TEST_WAIT_MS");
}

/* An enemy's death is heard by every player near it, not only by whoever
 * killed it: the local player shoots one, and both bots get the death. */
static void test_an_enemy_death_reaches_every_client_near_it(void)
{
    int local = server_add_local_player(server);
    Bot *a = bot_joined();
    Bot *b = bot_joined();
    World *w = server_world(server);
    const PlayerState *me = &w->players[local];
    int e = enemy_spawn(&w->enemies, me->x, me->y + 2.0f, ENEMY_IMP);
    w->enemies.items[e].hp = 1.0f;
    int seen_a = a->snapshots, seen_b = b->snapshots;

    PlayerInput in = {w->tick, BTN_FIRE, MATH_PI / 2.0f}; /* facing it, down the map */
    tick(&in);
    TEST_ASSERT_EQUAL_INT(ESTATE_DEAD, w->enemies.items[e].state);

    int heard_a = 0, heard_b = 0;
    uint32_t start = enet_time_get();
    while (!(heard_a && heard_b) && enet_time_get() - start < TEST_WAIT_MS) {
        tick(NULL);
        heard_a += events_since(a, &seen_a, EV_ENEMY_DEATH, e);
        heard_b += events_since(b, &seen_b, EV_ENEMY_DEATH, e);
    }
    TEST_ASSERT_EQUAL_INT(1, heard_a);
    TEST_ASSERT_EQUAL_INT(1, heard_b);
}

/* At SNAPSHOT_RATE a snapshot follows every third tick. The local player
 * fires in tick `nth` (1 or 2) of three: the shot happens between snapshots,
 * and world_step() has cleared it from World.events by the time the snapshot
 * goes out — yet that snapshot carries it, and the next does not again. */
static void check_shot_between_snapshots(uint32_t nth)
{
    int local = server_add_local_player(server);
    Bot *b = bot_joined();
    World *w = server_world(server);
    const uint32_t every = TICK_RATE / SNAPSHOT_RATE;

    uint32_t snap = (w->tick / every + 3) * every; /* the snapshot after the shot */
    uint32_t shot = snap - every + nth;            /* the tick count the shot leaves */
    while (w->tick < snap + every) {
        PlayerInput in = {w->tick, 0, w->players[local].angle};
        if (w->tick + 1 == shot) {
            in.buttons = BTN_FIRE;
        }
        tick(&in);
    }
    POLL_UNTIL(bot_has_snapshot_at(b, snap + every));

    const MsgSnapshot *before = bot_snapshot_at(b, snap - every);
    const MsgSnapshot *with = bot_snapshot_at(b, snap);
    const MsgSnapshot *after = bot_snapshot_at(b, snap + every);
    TEST_ASSERT_NOT_NULL(before);
    TEST_ASSERT_NOT_NULL(with);
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_EQUAL_INT(0, count_events(before, EV_SHOT, local));
    TEST_ASSERT_EQUAL_INT(1, count_events(with, EV_SHOT, local));
    TEST_ASSERT_EQUAL_INT(0, count_events(after, EV_SHOT, local));
}

static void test_shot_in_the_second_tick_reaches_the_snapshot(void)
{
    check_shot_between_snapshots(2);
}

static void test_shot_in_the_first_tick_reaches_the_snapshot(void)
{
    check_shot_between_snapshots(1);
}

/* The snapshots a bot gets over `ticks` ticks, after the ones already there. */
static int snapshots_over(Bot *b, int ticks)
{
    TICK_UNTIL(b->snapshots > 0);
    int seen = b->snapshots;
    uint32_t last = b->recent[(seen - 1) % RECENT].tick;
    for (int i = 0; i < ticks; i++) {
        tick(NULL);
    }
    POLL_UNTIL(bot_has_snapshot_at(b, last + (uint32_t)ticks));
    return b->snapshots - seen;
}

static void test_snapshot_rate_is_held_between_min_and_tick_rate(void)
{
    Bot *b = bot_joined();
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_RATE, snapshots_over(b, TICK_RATE));
    server_set_snapshot_rate(server, 1);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_RATE_MIN, snapshots_over(b, TICK_RATE));
    server_set_snapshot_rate(server, 1000);
    TEST_ASSERT_EQUAL_INT(TICK_RATE, snapshots_over(b, TICK_RATE));
}

/* ---- load ---- */

/* Ten clients, each sending an input every tick, for one second: how long a
 * tick takes and how many bytes go out. The tick has to leave the core three
 * quarters idle (requirements.md, P5) and the uplink within N7. */
static void run_ten_clients(int enemies, const char *mode, size_t max_bytes_per_second)
{
    World *w = server_world(server);
    w->enemies.count = 0;
    for (int i = 0; i < enemies; i++) {
        int col = i % 14, row = i / 14; /* rows 3-5, clear of the players in row 1 */
        enemy_spawn(&w->enemies, 1.5f + (float)col, 3.5f + (float)row, ENEMY_IMP);
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        bot_joined();
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        bots[i].snapshots = 0;
        bots[i].snapshot_bytes = 0;
    }

    double busy = 0.0, worst = 0.0;
    for (int t = 0; t < TICK_RATE; t++) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            bot_send_input(&bots[i], (uint32_t)t + 1, 0, 0.0f);
        }
        double start = now_seconds();
        server_tick(server, NULL, FIXED_DT);
        double took = now_seconds() - start;
        busy += took;
        worst = took > worst ? took : worst;
        poll_bots();
    }
    /* The last snapshot of the second went out after the last tick whose
     * number the snapshot interval divides. */
    uint32_t last = w->tick - w->tick % (TICK_RATE / SNAPSHOT_RATE);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        POLL_UNTIL(bot_has_snapshot_at(&bots[i], last));
    }

    size_t bytes = 0;
    int packets = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        bytes += bots[i].snapshot_bytes;
        packets += bots[i].snapshots;
    }
    size_t on_wire = bytes + (size_t)packets * DATAGRAM_OVERHEAD;
    printf("%s, 10 clients: tick %.0f us mean, %.0f us worst; %d snapshots, %zu B/s "
           "(%zu B/s with headers)\n",
           mode, busy / TICK_RATE * 1e6, worst * 1e6, packets, bytes, on_wire);

    TEST_ASSERT_TRUE_MESSAGE(busy / TICK_RATE < FIXED_DT / 4.0, "a tick takes P5's quarter");
    TEST_ASSERT_LESS_OR_EQUAL_size_t(max_bytes_per_second, on_wire);
}

static void test_ten_clients_deathmatch(void)
{
    run_ten_clients(0, "DM", 80 * 1024);
}

static void test_ten_clients_coop(void)
{
    run_ten_clients(MAX_ENEMIES, "co-op", 200 * 1024);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_create_refuses_what_it_cannot_serve);
    RUN_TEST(test_dedicated_server_has_no_local_player);
    RUN_TEST(test_join_is_accepted_with_id_seed_and_map);
    RUN_TEST(test_rand_is_seeded_with_the_accepted_seed);
    RUN_TEST(test_second_join_changes_nothing);
    RUN_TEST(test_joined_players_spawn_apart);
    RUN_TEST(test_eleventh_client_is_rejected_as_full);
    RUN_TEST(test_listen_server_counts_the_local_player);
    RUN_TEST(test_other_protocol_version_is_rejected);
    RUN_TEST(test_other_version_with_a_shorter_join_is_rejected);
    RUN_TEST(test_malformed_packets_are_ignored);
    RUN_TEST(test_client_that_quits_leaves_the_world);
    RUN_TEST(test_destroyed_server_tells_every_player_it_closed_the_game);
    RUN_TEST(test_killed_client_leaves_within_the_timeout_and_frees_its_slot);
    RUN_TEST(test_older_input_is_ignored);
    RUN_TEST(test_lost_input_does_not_stall_the_player);
    RUN_TEST(test_silent_client_stands_still_after_the_timeout);
    RUN_TEST(test_press_is_not_lost_under_a_newer_input);
    RUN_TEST(test_press_is_not_repeated_while_input_is_late);
    RUN_TEST(test_each_client_hears_its_own_empty_click_alone);
    RUN_TEST(test_an_enemy_death_reaches_every_client_near_it);
    RUN_TEST(test_shot_in_the_second_tick_reaches_the_snapshot);
    RUN_TEST(test_shot_in_the_first_tick_reaches_the_snapshot);
    RUN_TEST(test_snapshot_rate_is_held_between_min_and_tick_rate);
    RUN_TEST(test_ten_clients_deathmatch);
    RUN_TEST(test_ten_clients_coop);
    return UNITY_END();
}
