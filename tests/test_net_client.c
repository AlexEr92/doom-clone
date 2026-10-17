/* net_client.c over real sockets on 127.0.0.1, against two kinds of server in
 * this one process: the real one, net_server.c, for what a client sees of a
 * game it plays in; and a stand-in that speaks the protocol by hand, for what
 * the real one never sends — a reject, a stale or broken snapshot, silence
 * after MSG_ACCEPT.
 *
 * Which poll a datagram comes in is up to the operating system, so every wait
 * is for a condition, not a count of ticks. Timeouts are tested by moving
 * ENet's clock with enet_time_set(), as in test_net_server.c — which is also
 * why this file includes enet.h.
 *
 * MSG_ACCEPT names the map by its file name, and the client opens it under
 * assets/maps/ of the directory it runs in: tests/CMakeLists.txt makes that
 * directory beside this test, and the test writes its map there. */

#include <enet/enet.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "net_client.h"
#include "net_protocol.h"
#include "net_server.h"
#include "net_transport.h"
#include "world.h"
#include "unity.h"

/* Clear of NET_PORT_DEFAULT and of the other network tests, which ctest -j
 * may run alongside. Nothing listens on NOBODY_PORT. */
#define TEST_PORT 27117
#define FAKE_PORT 27118
#define NOBODY_PORT 27119

/* Long enough for anything on loopback, short enough to fail fast. */
#define TEST_WAIT_MS 2000

#define MAP_NAME "net_client_map.txt"
#define MAP_PATH "assets/maps/" MAP_NAME

#define MAX_CLIENTS (MAX_PLAYERS + 1)

/* The second player starts beside the first, right before a door. Armor lies
 * two steps south of the first, and an imp stands out of reach of both, so
 * that it stays put until it is shot.
 *
 *   0123456789012345
 * 0 ################
 * 1 #P.D...........#
 * 2 #..............#
 * 3 #R.............#
 * 4 #..............#
 * 5 #.........E....#
 * 6 ################ */
static const char *const map_rows[] = {
        "################", "#P.D...........#", "#..............#", "#R.............#",
        "#..............#", "#.........E....#", "################",
};

#define ANGLE_EAST 0.0f
#define ANGLE_SOUTH (MATH_PI / 2.0f)

/* A server written by hand: it takes one client and keeps what it sends. */
typedef struct {
    NetHost *host;
    NetPeerId peer; /* the client, NET_PEER_NONE until it connects */
    int joins;
    MsgJoin join;
    int inputs;
    MsgInput input[8]; /* the first ones that came */
} Fake;

static Server *server;
static Fake fake;
static Client *clients[MAX_CLIENTS];
static int client_count;

/* Events of a marked kind every poll of the clients has seen, by actor: a
 * snapshot that was applied leaves its events in World.events for one poll
 * only, so the test collects them as they pass. */
#define MARK EV_DOOR
static int marks_seen[256];

static void write_map(void)
{
    FILE *f = fopen(MAP_PATH, "w");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "no assets/maps/ beside the test: see tests/CMakeLists.txt");
    for (size_t i = 0; i < sizeof(map_rows) / sizeof(map_rows[0]); i++) {
        fprintf(f, "%s\n", map_rows[i]);
    }
    fclose(f);
}

void setUp(void)
{
    write_map();
    memset(&fake, 0, sizeof(fake));
    fake.peer = NET_PEER_NONE;
    memset(clients, 0, sizeof(clients));
    client_count = 0;
    memset(marks_seen, 0, sizeof(marks_seen));
    server = server_create(TEST_PORT, MAP_PATH);
    TEST_ASSERT_NOT_NULL(server);
}

void tearDown(void)
{
    for (int i = 0; i < client_count; i++) {
        client_destroy(clients[i]);
    }
    net_destroy(fake.host);
    server_destroy(server);
}

/* ---- the stand-in server ---- */

static void fake_poll(void)
{
    NetTransportEvent ev;
    while (net_poll(fake.host, &ev)) {
        if (ev.type == NET_EV_CONNECT) {
            fake.peer = ev.peer;
        } else if (ev.type == NET_EV_DISCONNECT) {
            fake.peer = NET_PEER_NONE;
        } else if (ev.type == NET_EV_DATA) {
            uint8_t type = ((const uint8_t *)ev.data)[0];
            if (type == MSG_JOIN) {
                TEST_ASSERT_EQUAL_size_t(sizeof(MsgJoin), ev.size);
                memcpy(&fake.join, ev.data, sizeof(fake.join));
                fake.joins++;
            } else if (type == MSG_INPUT) {
                TEST_ASSERT_EQUAL_size_t(sizeof(MsgInput), ev.size);
                if (fake.inputs < 8) {
                    memcpy(&fake.input[fake.inputs], ev.data, sizeof(MsgInput));
                }
                fake.inputs++;
            }
        }
    }
}

static void fake_start(void)
{
    fake.host = net_host_server(FAKE_PORT, 1);
    TEST_ASSERT_NOT_NULL(fake.host);
}

/* Everything the stand-in sends goes reliably, so that it comes, and comes
 * in order: the client reads a message by its type, not by its channel. */
static void fake_send(const void *data, size_t size)
{
    net_send(fake.host, fake.peer, NET_CH_RELIABLE, data, size, 1);
    fake_poll();
}

static void fake_send_accept(uint8_t your_id, const char *map)
{
    MsgAccept msg;
    memset(&msg, 0, sizeof(msg));
    msg.h.type = MSG_ACCEPT;
    msg.your_id = your_id;
    memcpy(msg.map, map, strlen(map) < NET_MAP_MAX ? strlen(map) + 1 : NET_MAP_MAX);
    fake_send(&msg, sizeof(msg));
}

static void fake_send_reject(uint8_t reason)
{
    MsgReject msg;
    memset(&msg, 0, sizeof(msg));
    msg.h.type = MSG_REJECT;
    msg.reason = reason;
    fake_send(&msg, sizeof(msg));
}

/* A snapshot of player `id` standing at (x, 1.5), with one MARK event per
 * entry of `marks`, its actor that entry. Returns its size on the wire. */
static size_t fake_send_snapshot(uint32_t tick, uint8_t id, float x, const uint8_t *marks, int n)
{
    MsgSnapshot s;
    memset(&s, 0, sizeof(s));
    s.h.type = MSG_SNAPSHOT;
    s.tick = tick;
    s.player_count = 1;
    s.players[0].id = id;
    s.players[0].alive = 1;
    s.players[0].x = x;
    s.players[0].y = 1.5f;
    s.players[0].hp = 100;
    for (int i = 0; i < n; i++) {
        s.events[i].kind = MARK;
        s.events[i].actor = marks[i];
    }
    s.event_count = (uint8_t)n;
    static uint8_t wire[sizeof(MsgSnapshot)];
    size_t size = snapshot_encode(&s, wire);
    TEST_ASSERT_NOT_EQUAL(0, size);
    fake_send(wire, size);
    return size;
}

/* ---- polling ---- */

static void poll_clients(void)
{
    for (int i = 0; i < client_count; i++) {
        client_poll(clients[i]);
        const World *w = client_world(clients[i]);
        for (int e = 0; w && e < w->events.count; e++) {
            if (w->events.items[e].kind == MARK) {
                marks_seen[w->events.items[e].actor]++;
            }
        }
    }
}

/* One tick of the server, if there is one, then everyone's network. */
static void tick(void)
{
    if (server) {
        server_tick(server, NULL, FIXED_DT);
    }
    if (fake.host) {
        fake_poll();
    }
    poll_clients();
}

/* Tick until `cond` holds, for TEST_WAIT_MS of real time at most. */
#define TICK_UNTIL(cond)                                                                           \
    do {                                                                                           \
        uint32_t start_ = enet_time_get();                                                         \
        while (!(cond) && enet_time_get() - start_ < TEST_WAIT_MS) {                               \
            tick();                                                                                \
        }                                                                                          \
        TEST_ASSERT_TRUE_MESSAGE(cond, #cond " within TEST_WAIT_MS");                              \
    } while (0)

/* Tick for `ms` of real time: long enough for what is on its way to come. */
static void tick_for(uint32_t ms)
{
    uint32_t start = enet_time_get();
    while (enet_time_get() - start < ms) {
        tick();
    }
}

static void advance_clock(uint32_t ms)
{
    enet_time_set(enet_time_get() + ms);
}

static Client *client_new(uint16_t port)
{
    TEST_ASSERT_LESS_THAN_INT(MAX_CLIENTS, client_count);
    Client *c = client_create();
    TEST_ASSERT_NOT_NULL(c);
    clients[client_count++] = c;
    TEST_ASSERT_EQUAL_INT(0, client_connect(c, "127.0.0.1", port, NULL));
    TEST_ASSERT_EQUAL_INT(CL_CONNECTING, client_state(c));
    return c;
}

static Client *client_joined(void)
{
    Client *c = client_new(TEST_PORT);
    TICK_UNTIL(client_state(c) != CL_CONNECTING);
    TEST_ASSERT_EQUAL_INT(CL_CONNECTED, client_state(c));
    return c;
}

/* A client of the stand-in, connected and asked to join. */
static Client *fake_client_joining(void)
{
    fake_start();
    Client *c = client_new(FAKE_PORT);
    TICK_UNTIL(fake.joins > 0);
    return c;
}

/* ... and let in, as player 0, with the first snapshot at `tick`. */
static Client *fake_client_joined(uint32_t first_tick)
{
    Client *c = fake_client_joining();
    fake_send_accept(0, MAP_NAME);
    fake_send_snapshot(first_tick, 0, 1.5f, NULL, 0);
    TICK_UNTIL(client_state(c) == CL_CONNECTED);
    return c;
}

static void send_input(Client *c, uint16_t buttons, float angle)
{
    PlayerInput in = {0, buttons, angle};
    client_send_input(c, &in);
}

/* Hold `buttons` facing `angle`, an input a tick, until `cond` holds. */
#define HOLD_UNTIL(c, buttons, angle, cond)                                                        \
    do {                                                                                           \
        uint32_t start_ = enet_time_get();                                                         \
        while (!(cond) && enet_time_get() - start_ < TEST_WAIT_MS) {                               \
            send_input(c, buttons, angle);                                                         \
            tick();                                                                                \
        }                                                                                          \
        TEST_ASSERT_TRUE_MESSAGE(cond, #cond " within TEST_WAIT_MS");                              \
    } while (0)

static const PlayerState *seen_player(Client *c, int id)
{
    return &client_world(c)->players[id];
}

static const PlayerState *server_player(int id)
{
    return &server_world(server)->players[id];
}

/* ---- joining a real server ---- */

static void test_join_brings_the_servers_world(void)
{
    Client *c = client_new(TEST_PORT);
    TEST_ASSERT_NULL(client_world(c));
    TEST_ASSERT_EQUAL_INT(-1, client_local_player(c));
    TEST_ASSERT_NULL(client_reason(c));

    TICK_UNTIL(client_state(c) == CL_CONNECTED);
    const World *sw = server_world(server);
    const World *w = client_world(c);
    TEST_ASSERT_NOT_NULL(w);
    TEST_ASSERT_EQUAL_INT(0, client_local_player(c));
    TEST_ASSERT_EQUAL_INT(0, w->local_player);
    TEST_ASSERT_TRUE(w->players[0].active);
    TEST_ASSERT_EQUAL_FLOAT(sw->players[0].x, w->players[0].x);
    TEST_ASSERT_EQUAL_FLOAT(sw->players[0].y, w->players[0].y);
    TEST_ASSERT_EQUAL_INT(sw->enemies.count, w->enemies.count);
    TEST_ASSERT_EQUAL_INT(sw->items.count, w->items.count);
    TEST_ASSERT_EQUAL_INT(sw->doors.count, w->doors.count);
    TEST_ASSERT_NULL(client_reason(c));
}

static void test_second_client_plays_as_the_next_player(void)
{
    client_joined();
    Client *b = client_joined();
    TEST_ASSERT_EQUAL_INT(1, client_local_player(b));
    TEST_ASSERT_EQUAL_INT(1, client_world(b)->local_player);
}

static void test_connect_while_connected_changes_nothing(void)
{
    Client *c = client_joined();
    TEST_ASSERT_EQUAL_INT(-1, client_connect(c, "127.0.0.1", TEST_PORT, NULL));
    TEST_ASSERT_EQUAL_INT(CL_CONNECTED, client_state(c));
    TEST_ASSERT_NOT_NULL(client_world(c));
}

static void test_own_input_moves_the_own_player(void)
{
    Client *c = client_joined();
    float y0 = seen_player(c, 0)->y;
    HOLD_UNTIL(c, BTN_FORWARD, ANGLE_SOUTH, seen_player(c, 0)->y > y0 + 0.5f);
}

/* The first criterion of the task: each sees the other, and the other's
 * movement, as the server has it. */
static void test_two_clients_see_each_other_move(void)
{
    Client *a = client_joined();
    Client *b = client_joined();
    TICK_UNTIL(seen_player(a, 1)->active && seen_player(b, 0)->active);
    float y0 = seen_player(b, 0)->y;

    HOLD_UNTIL(a, BTN_FORWARD, ANGLE_SOUTH, server_player(0)->y > y0 + 0.4f);
    send_input(a, 0, ANGLE_SOUTH);
    tick_for(50);

    TICK_UNTIL(seen_player(b, 0)->y == server_player(0)->y);
    TEST_ASSERT_EQUAL_FLOAT(server_player(0)->x, seen_player(b, 0)->x);
    TEST_ASSERT_EQUAL_FLOAT(ANGLE_SOUTH, seen_player(b, 0)->angle);
    TEST_ASSERT_EQUAL_FLOAT(server_player(1)->x, seen_player(a, 1)->x);
    TEST_ASSERT_EQUAL_FLOAT(server_player(1)->y, seen_player(a, 1)->y);
}

static void test_shot_enemy_dies_for_the_other_client(void)
{
    Client *a = client_joined();
    Client *b = client_joined();
    const Enemy *e = &server_world(server)->enemies.items[0];
    HOLD_UNTIL(a, BTN_FIRE, atan2f(e->y - server_player(0)->y, e->x - server_player(0)->x),
               e->state == ESTATE_DEAD);
    send_input(a, 0, 0.0f);

    TICK_UNTIL(client_world(b)->enemies.items[0].state == ESTATE_DEAD);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, client_world(b)->enemies.items[0].hp);
    TICK_UNTIL(client_world(a)->enemies.items[0].state == ESTATE_DEAD);
}

static void test_pickup_is_gone_for_both(void)
{
    Client *a = client_joined();
    Client *b = client_joined();
    TEST_ASSERT_TRUE(client_world(b)->items.items[0].active);
    HOLD_UNTIL(a, BTN_FORWARD, ANGLE_SOUTH, !server_world(server)->items.items[0].active);
    send_input(a, 0, ANGLE_SOUTH);

    TICK_UNTIL(!client_world(b)->items.items[0].active);
    TICK_UNTIL(!client_world(a)->items.items[0].active);
}

static void test_door_opened_by_one_opens_for_the_other(void)
{
    Client *a = client_joined();
    Client *b = client_joined();
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, client_world(a)->doors.doors[0].state);
    send_input(b, BTN_USE, ANGLE_EAST);
    send_input(b, 0, ANGLE_EAST);

    TICK_UNTIL(client_world(a)->doors.doors[0].openness > 0.0f);
    TICK_UNTIL(client_world(a)->doors.doors[0].state == DOOR_OPEN);
    TEST_ASSERT_EQUAL_INT(DOOR_OPEN, client_world(b)->doors.doors[0].state);
}

static int every_client_sees_everyone(void)
{
    for (int i = 0; i < client_count; i++) {
        for (int id = 0; id < MAX_PLAYERS; id++) {
            if (!seen_player(clients[i], id)->active) {
                return 0;
            }
        }
    }
    return 1;
}

/* A full match: every client sees all ten players, and each of them under the
 * id the server gave them. The id is the player's skin (config.h), so that
 * makes ten characters, each one the same on every client. Players spawn on
 * cells of their own, which is what tells them apart here. */
static void test_ten_clients_see_each_player_under_one_id(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        TEST_ASSERT_EQUAL_INT(i, client_local_player(client_joined()));
    }
    TICK_UNTIL(every_client_sees_everyone());
    for (int a = 0; a < MAX_PLAYERS; a++) {
        for (int b = a + 1; b < MAX_PLAYERS; b++) {
            TEST_ASSERT_FALSE(server_player(a)->x == server_player(b)->x &&
                              server_player(a)->y == server_player(b)->y);
        }
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        for (int id = 0; id < MAX_PLAYERS; id++) {
            TEST_ASSERT_EQUAL_INT(id, seen_player(clients[i], id)->id);
            TEST_ASSERT_EQUAL_FLOAT(server_player(id)->x, seen_player(clients[i], id)->x);
            TEST_ASSERT_EQUAL_FLOAT(server_player(id)->y, seen_player(clients[i], id)->y);
        }
    }
}

static void test_eleventh_client_is_rejected_as_full(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        client_joined();
    }
    Client *c = client_new(TEST_PORT);
    TICK_UNTIL(client_state(c) != CL_CONNECTING);
    TEST_ASSERT_EQUAL_INT(CL_REJECTED, client_state(c));
    TEST_ASSERT_NOT_NULL(strstr(client_reason(c), "full"));
    TEST_ASSERT_NULL(client_world(c));
}

/* ---- losing the server ---- */

/* The server says it is closing the game, and the client says so in turn
 * rather than that the connection is lost. */
static void test_server_that_quits_ends_the_game(void)
{
    Client *c = client_joined();
    server_destroy(server);
    server = NULL;
    TICK_UNTIL(client_state(c) == CL_DISCONNECTED);
    TEST_ASSERT_EQUAL_STRING("the server closed the game", client_reason(c));
    TEST_ASSERT_NOT_NULL(client_world(c));
}

/* What kill -9 of the server looks like: silence. The client is still in the
 * game a second before the timeout and out of it after. */
static void test_killed_server_is_noticed_within_the_timeout(void)
{
    Client *c = client_joined();
    Server *silent = server;
    server = NULL;
    /* What the server sent last is read before the silence starts, not after
     * the clock has moved, when it would count as heard from just now. */
    tick_for(20);

    advance_clock(NET_PEER_TIMEOUT_MS - 1000);
    tick_for(20);
    TEST_ASSERT_EQUAL_INT(CL_CONNECTED, client_state(c));

    advance_clock(1500);
    TICK_UNTIL(client_state(c) == CL_DISCONNECTED);
    TEST_ASSERT_NOT_NULL(client_reason(c));
    server = silent;
}

static void test_client_joins_again_after_the_server_comes_back(void)
{
    Client *c = client_joined();
    server_destroy(server);
    server = NULL;
    TICK_UNTIL(client_state(c) == CL_DISCONNECTED);

    server = server_create(TEST_PORT, MAP_PATH);
    TEST_ASSERT_NOT_NULL(server);
    TEST_ASSERT_EQUAL_INT(0, client_connect(c, "127.0.0.1", TEST_PORT, NULL));
    TEST_ASSERT_EQUAL_INT(CL_CONNECTING, client_state(c));
    TEST_ASSERT_NULL(client_world(c));
    TEST_ASSERT_EQUAL_INT(-1, client_local_player(c));
    TEST_ASSERT_NULL(client_reason(c));
    TICK_UNTIL(client_state(c) == CL_CONNECTED);
    TEST_ASSERT_TRUE(seen_player(c, client_local_player(c))->active);
}

/* ---- the handshake, against the stand-in ---- */

static void test_join_names_this_protocol_version(void)
{
    fake_client_joining();
    TEST_ASSERT_EQUAL_INT(MSG_JOIN, fake.join.h.type);
    TEST_ASSERT_EQUAL_INT(PROTO_VERSION, fake.join.proto);
    TEST_ASSERT_EQUAL_STRING("", fake.join.name);
}

/* The name a client joins under reaches MsgJoin.name, cut to fit it, and the
 * next connection carries the name it was given rather than the last one's. */
static void test_join_carries_the_players_name(void)
{
    static const struct {
        const char *name;
        const char *sent;
    } cases[] = {
            {"doomguy", "doomguy"},
            {"a_name_longer_than_fits", "a_name_longer_t"},
            {NULL, ""},
    };
    fake_start();
    Client *c = client_create();
    TEST_ASSERT_NOT_NULL(c);
    clients[client_count++] = c;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int joins = fake.joins;
        TEST_ASSERT_EQUAL_INT(0, client_connect(c, "127.0.0.1", FAKE_PORT, cases[i].name));
        TICK_UNTIL(fake.joins > joins);
        TEST_ASSERT_EQUAL_STRING(cases[i].sent, fake.join.name);
        TEST_ASSERT_EQUAL_CHAR('\0', fake.join.name[NET_NAME_MAX - 1]);
        fake_send_reject(REJECT_SERVER_FULL);
        TICK_UNTIL(client_state(c) == CL_REJECTED);
    }
}

/* The client is in the game from its first snapshot, not from MSG_ACCEPT: a
 * world with nobody in it yet would have nothing to look through. */
static void test_accept_alone_does_not_connect(void)
{
    Client *c = fake_client_joining();
    fake_send_accept(3, MAP_NAME);
    TICK_UNTIL(client_local_player(c) == 3);
    TEST_ASSERT_EQUAL_INT(CL_CONNECTING, client_state(c));
    TEST_ASSERT_NULL(client_world(c));

    fake_send_snapshot(1, 3, 2.5f, NULL, 0);
    TICK_UNTIL(client_state(c) == CL_CONNECTED);
    TEST_ASSERT_EQUAL_INT(3, client_world(c)->local_player);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, seen_player(c, 3)->x);
}

static void test_reject_says_why(void)
{
    static const struct {
        uint8_t reason;
        const char *says;
    } cases[] = {
            {REJECT_SERVER_FULL, "full"},
            {REJECT_VERSION, "version"},
            {99, "turned"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Client *c = fake_client_joining();
        fake_send_reject(cases[i].reason);
        TICK_UNTIL(client_state(c) != CL_CONNECTING);
        TEST_ASSERT_EQUAL_INT(CL_REJECTED, client_state(c));
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(client_reason(c), cases[i].says), client_reason(c));
        tearDown();
        setUp();
    }
}

/* A file name and nothing more: a path could lead out of assets/maps/. One
 * that does lead to a map is refused all the same. */
static void test_accept_naming_a_path_is_rejected(void)
{
    static const char *const maps[] = {"../" MAP_NAME, "maps/" MAP_NAME, "..\\" MAP_NAME, ""};
    FILE *f = fopen("assets/" MAP_NAME, "w");
    TEST_ASSERT_NOT_NULL(f);
    for (size_t i = 0; i < sizeof(map_rows) / sizeof(map_rows[0]); i++) {
        fprintf(f, "%s\n", map_rows[i]);
    }
    fclose(f);
    for (size_t i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
        Client *c = fake_client_joining();
        fake_send_accept(0, maps[i]);
        TICK_UNTIL(client_state(c) != CL_CONNECTING);
        TEST_ASSERT_EQUAL_INT_MESSAGE(CL_REJECTED, client_state(c), maps[i]);
        TEST_ASSERT_EQUAL_INT(-1, client_local_player(c));
        tearDown();
        setUp();
    }
}

static void test_accept_naming_a_map_not_here_is_rejected(void)
{
    Client *c = fake_client_joining();
    fake_send_accept(0, "no_such_map.txt");
    TICK_UNTIL(client_state(c) != CL_CONNECTING);
    TEST_ASSERT_EQUAL_INT(CL_REJECTED, client_state(c));
    TEST_ASSERT_NOT_NULL(strstr(client_reason(c), "no_such_map.txt"));
}

static void test_accept_out_of_range_is_rejected(void)
{
    Client *c = fake_client_joining();
    MsgAccept msg;
    memset(&msg, 0, sizeof(msg));
    msg.h.type = MSG_ACCEPT;
    msg.your_id = MAX_PLAYERS;
    strcpy(msg.map, MAP_NAME);
    fake_send(&msg, sizeof(msg));
    TICK_UNTIL(client_state(c) == CL_REJECTED);

    tearDown();
    setUp();
    c = fake_client_joining();
    memset(msg.map, 'a', sizeof(msg.map)); /* no terminating NUL */
    msg.your_id = 0;
    fake_send(&msg, sizeof(msg));
    TICK_UNTIL(client_state(c) == CL_REJECTED);
}

/* Both timeouts below leave a margin of TIMEOUT_MARGIN_MS on either side of
 * the jump for the real time the test itself takes. */
#define TIMEOUT_MARGIN_MS 500

static void test_no_first_snapshot_within_the_timeout_is_rejected(void)
{
    Client *c = fake_client_joining();
    fake_send_accept(0, MAP_NAME);
    TICK_UNTIL(client_local_player(c) == 0);

    /* Both sides keep talking after the jump, so the transport hears from the
     * other side again and leaves the timing to the client. */
    advance_clock(NET_CONNECT_TIMEOUT_MS - TIMEOUT_MARGIN_MS);
    tick_for(20);
    TEST_ASSERT_EQUAL_INT(CL_CONNECTING, client_state(c));
    advance_clock(TIMEOUT_MARGIN_MS);
    tick();
    TEST_ASSERT_EQUAL_INT(CL_REJECTED, client_state(c));
    TEST_ASSERT_NOT_NULL(strstr(client_reason(c), "did not answer"));
}

static void test_nobody_at_the_address_is_rejected_after_the_timeout(void)
{
    Client *c = client_new(NOBODY_PORT);
    tick_for(20);
    advance_clock(NET_CONNECT_TIMEOUT_MS - TIMEOUT_MARGIN_MS);
    tick();
    TEST_ASSERT_EQUAL_INT(CL_CONNECTING, client_state(c));
    advance_clock(TIMEOUT_MARGIN_MS);
    tick();
    TEST_ASSERT_EQUAL_INT(CL_REJECTED, client_state(c));
    TEST_ASSERT_NOT_NULL(strstr(client_reason(c), "did not answer"));
}

static void test_address_that_does_not_resolve_is_rejected_at_once(void)
{
    Client *c = client_create();
    TEST_ASSERT_NOT_NULL(c);
    clients[client_count++] = c;
    TEST_ASSERT_EQUAL_INT(-1, client_connect(c, "", TEST_PORT, NULL));
    TEST_ASSERT_EQUAL_INT(CL_REJECTED, client_state(c));
    TEST_ASSERT_NOT_NULL(client_reason(c));
}

/* A second MSG_ACCEPT would load the map again under the game, and a reject
 * after the accept answers a question nobody asked: the game goes on. */
static void test_handshake_after_the_join_is_ignored(void)
{
    Client *c = fake_client_joined(1);
    fake_send_accept(5, MAP_NAME);
    fake_send_reject(REJECT_SERVER_FULL);
    const uint8_t newer = 2;
    fake_send_snapshot(2, 0, 4.5f, &newer, 1);
    TICK_UNTIL(marks_seen[newer] > 0);
    TEST_ASSERT_EQUAL_INT(CL_CONNECTED, client_state(c));
    TEST_ASSERT_EQUAL_INT(0, client_local_player(c));
    TEST_ASSERT_NULL(client_reason(c));
    TEST_ASSERT_EQUAL_FLOAT(4.5f, seen_player(c, 0)->x);
}

static void test_server_closing_the_game_disconnects(void)
{
    Client *c = fake_client_joined(1);
    MsgHeader msg = {MSG_DISCONNECT, {0}};
    fake_send(&msg, sizeof(msg));
    TICK_UNTIL(client_state(c) == CL_DISCONNECTED);
    TEST_ASSERT_NOT_NULL(strstr(client_reason(c), "closed"));
}

static void test_server_that_hangs_up_during_the_join_rejects(void)
{
    Client *c = fake_client_joining();
    net_disconnect(fake.host, fake.peer);
    fake.peer = NET_PEER_NONE;
    TICK_UNTIL(client_state(c) != CL_CONNECTING);
    TEST_ASSERT_EQUAL_INT(CL_REJECTED, client_state(c));
    TEST_ASSERT_NOT_NULL(strstr(client_reason(c), "closed the connection"));
}

static void test_short_and_unknown_messages_are_ignored(void)
{
    Client *c = fake_client_joined(1);
    const uint8_t short_one[2] = {MSG_SNAPSHOT, 0};
    fake_send(short_one, sizeof(short_one));
    const MsgHeader unknown = {99, {0}}, client_only = {MSG_INPUT, {0}};
    fake_send(&unknown, sizeof(unknown));
    fake_send(&client_only, sizeof(client_only));
    const uint8_t newer = 2;
    fake_send_snapshot(2, 0, 1.5f, &newer, 1);
    TICK_UNTIL(marks_seen[newer] > 0);
    TEST_ASSERT_EQUAL_INT(CL_CONNECTED, client_state(c));
}

static void test_client_that_never_connected_has_nothing(void)
{
    Client *c = client_create();
    TEST_ASSERT_NOT_NULL(c);
    clients[client_count++] = c;
    client_poll(c);
    send_input(c, BTN_FIRE, 0.0f);
    TEST_ASSERT_EQUAL_INT(CL_DISCONNECTED, client_state(c));
    TEST_ASSERT_NULL(client_world(c));
    TEST_ASSERT_NULL(client_reason(c));
    TEST_ASSERT_EQUAL_INT(-1, client_local_player(c));
    client_destroy(NULL);
}

/* ---- input ---- */

/* The server takes an input only if it is newer than the last, so the client
 * numbers them itself: whatever tick the caller hands it — here the same one
 * three times — they go out as 0, 1, 2. None goes before the client is in. */
static void test_inputs_are_numbered_by_the_client(void)
{
    Client *c = fake_client_joining();
    send_input(c, BTN_FIRE, 1.0f);
    fake_send_accept(0, MAP_NAME);
    fake_send_snapshot(1, 0, 1.5f, NULL, 0);
    TICK_UNTIL(client_state(c) == CL_CONNECTED);

    for (int i = 0; i < 3; i++) {
        PlayerInput in = {1234, (uint16_t)(BTN_FORWARD << i), 0.25f * (float)i};
        client_send_input(c, &in);
    }
    TICK_UNTIL(fake.inputs >= 3);
    tick_for(20);
    TEST_ASSERT_EQUAL_INT(3, fake.inputs);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT(MSG_INPUT, fake.input[i].h.type);
        TEST_ASSERT_EQUAL_UINT32(i, fake.input[i].tick);
        TEST_ASSERT_EQUAL_UINT16(BTN_FORWARD << i, fake.input[i].buttons);
        TEST_ASSERT_EQUAL_FLOAT(0.25f * (float)i, fake.input[i].angle);
    }
}

/* ---- snapshots ---- */

/* A snapshot that is not newer than the world is dropped whole, events and
 * all; the next newer one is applied. Sent in order on one reliable channel,
 * so the stale ones come before the newer one and are read first. */
static void test_stale_snapshot_is_ignored(void)
{
    Client *c = fake_client_joined(10);
    const uint8_t stale = 9, again = 10, newer = 11;
    fake_send_snapshot(9, 0, 6.0f, &stale, 1);
    fake_send_snapshot(10, 0, 7.0f, &again, 1);
    fake_send_snapshot(11, 0, 8.0f, &newer, 1);
    TICK_UNTIL(marks_seen[newer] > 0);
    TEST_ASSERT_EQUAL_INT(0, marks_seen[stale]);
    TEST_ASSERT_EQUAL_INT(0, marks_seen[again]);
    TEST_ASSERT_EQUAL_UINT32(11, client_world(c)->tick);
    TEST_ASSERT_EQUAL_FLOAT(8.0f, seen_player(c, 0)->x);
    /* Counted as they come, dropped or not: the first one and these three. */
    TEST_ASSERT_EQUAL_UINT32(4, client_stats(c).snapshots);
}

static void test_snapshot_that_does_not_decode_is_dropped(void)
{
    Client *c = fake_client_joined(10);
    MsgSnapshot s;
    memset(&s, 0, sizeof(s));
    s.h.type = MSG_SNAPSHOT;
    s.tick = 20;
    s.player_count = 1;
    s.players[0].id = MAX_PLAYERS; /* out of range */
    s.players[0].x = 9.0f;
    s.event_count = 1;
    s.events[0].kind = MARK;
    s.events[0].actor = 20;
    static uint8_t wire[sizeof(MsgSnapshot)];
    size_t size = snapshot_encode(&s, wire);
    fake_send(wire, size);
    const uint8_t newer = 11;
    fake_send_snapshot(11, 0, 8.0f, &newer, 1);

    TICK_UNTIL(marks_seen[newer] > 0);
    TEST_ASSERT_EQUAL_INT(0, marks_seen[20]);
    TEST_ASSERT_EQUAL_UINT32(11, client_world(c)->tick);
    TEST_ASSERT_EQUAL_UINT32(3, client_stats(c).snapshots);
}

/* Two snapshots read in one poll leave the events of both, the earlier
 * first, as many as MAX_EVENTS holds; the next poll starts empty. */
static void test_events_of_one_poll_add_up(void)
{
    Client *c = fake_client_joined(1);
    uint8_t first[20], second[20];
    for (int i = 0; i < 20; i++) {
        first[i] = (uint8_t)i;
        second[i] = (uint8_t)(100 + i);
    }
    fake_send_snapshot(2, 0, 1.5f, first, 20);
    fake_send_snapshot(3, 0, 1.5f, second, 20);
    /* Both are on their way before the client reads anything. */
    uint32_t start = enet_time_get();
    while (enet_time_get() - start < 20) {
        fake_poll();
    }
    client_poll(c);

    const EventQueue *q = &client_world(c)->events;
    TEST_ASSERT_EQUAL_UINT32(3, client_world(c)->tick);
    TEST_ASSERT_EQUAL_INT(MAX_EVENTS, q->count);
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_EQUAL_INT(i, q->items[i].actor);
    }
    for (int i = 20; i < MAX_EVENTS; i++) {
        TEST_ASSERT_EQUAL_INT(100 + i - 20, q->items[i].actor);
    }

    client_poll(c);
    TEST_ASSERT_EQUAL_INT(0, client_world(c)->events.count);
}

/* ---- traffic ---- */

/* Payload both ways since client_connect(), as it was sent: an input the
 * client ignores before it is in the game is not counted, and a new
 * connection starts from nothing. */
static void test_stats_count_the_payload(void)
{
    Client *c = fake_client_joining();
    send_input(c, BTN_FIRE, 0.0f);
    fake_send_accept(0, MAP_NAME);
    size_t first = fake_send_snapshot(1, 0, 1.5f, NULL, 0);
    const uint8_t marks[2] = {1, 2};
    size_t second = fake_send_snapshot(2, 0, 1.5f, marks, 2);
    TICK_UNTIL(client_world(c) && client_world(c)->tick == 2);
    send_input(c, 0, 0.0f);
    send_input(c, BTN_FORWARD, 0.0f);

    ClientStats st = client_stats(c);
    TEST_ASSERT_EQUAL_UINT32(2, st.snapshots);
    TEST_ASSERT_EQUAL_UINT64(sizeof(MsgAccept) + first + second, st.bytes_in);
    TEST_ASSERT_EQUAL_UINT64(sizeof(MsgJoin) + 2 * sizeof(MsgInput), st.bytes_out);

    const MsgHeader bye = {MSG_DISCONNECT, {0}};
    fake_send(&bye, sizeof(bye));
    TICK_UNTIL(client_state(c) == CL_DISCONNECTED);
    TEST_ASSERT_EQUAL_UINT64(sizeof(MsgAccept) + first + second + sizeof(bye),
                             client_stats(c).bytes_in);
    TEST_ASSERT_EQUAL_INT(0, client_connect(c, "127.0.0.1", FAKE_PORT, NULL));
    st = client_stats(c);
    TEST_ASSERT_EQUAL_UINT32(0, st.snapshots);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_in);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_join_brings_the_servers_world);
    RUN_TEST(test_second_client_plays_as_the_next_player);
    RUN_TEST(test_connect_while_connected_changes_nothing);
    RUN_TEST(test_own_input_moves_the_own_player);
    RUN_TEST(test_two_clients_see_each_other_move);
    RUN_TEST(test_shot_enemy_dies_for_the_other_client);
    RUN_TEST(test_pickup_is_gone_for_both);
    RUN_TEST(test_door_opened_by_one_opens_for_the_other);
    RUN_TEST(test_ten_clients_see_each_player_under_one_id);
    RUN_TEST(test_eleventh_client_is_rejected_as_full);
    RUN_TEST(test_server_that_quits_ends_the_game);
    RUN_TEST(test_killed_server_is_noticed_within_the_timeout);
    RUN_TEST(test_client_joins_again_after_the_server_comes_back);
    RUN_TEST(test_join_names_this_protocol_version);
    RUN_TEST(test_join_carries_the_players_name);
    RUN_TEST(test_accept_alone_does_not_connect);
    RUN_TEST(test_reject_says_why);
    RUN_TEST(test_accept_naming_a_path_is_rejected);
    RUN_TEST(test_accept_naming_a_map_not_here_is_rejected);
    RUN_TEST(test_accept_out_of_range_is_rejected);
    RUN_TEST(test_no_first_snapshot_within_the_timeout_is_rejected);
    RUN_TEST(test_nobody_at_the_address_is_rejected_after_the_timeout);
    RUN_TEST(test_address_that_does_not_resolve_is_rejected_at_once);
    RUN_TEST(test_handshake_after_the_join_is_ignored);
    RUN_TEST(test_server_closing_the_game_disconnects);
    RUN_TEST(test_server_that_hangs_up_during_the_join_rejects);
    RUN_TEST(test_short_and_unknown_messages_are_ignored);
    RUN_TEST(test_client_that_never_connected_has_nothing);
    RUN_TEST(test_inputs_are_numbered_by_the_client);
    RUN_TEST(test_stale_snapshot_is_ignored);
    RUN_TEST(test_snapshot_that_does_not_decode_is_dropped);
    RUN_TEST(test_events_of_one_poll_add_up);
    RUN_TEST(test_stats_count_the_payload);
    return UNITY_END();
}
