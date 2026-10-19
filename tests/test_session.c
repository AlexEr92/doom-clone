/* session.c: the three kinds of GameSession, driven through the table alone,
 * as the frame loop drives them. Solo on a world of its own; host and
 * dedicated on a server on 127.0.0.1; a client against a host session in this
 * same process, and against a stand-in that speaks the protocol by hand where
 * what goes over the network has to be read: the name a client joins under,
 * the snapshots a server sends a second. */

#include "unity.h"
#include "session.h"
#include "options.h"
#include "net_protocol.h"
#include "net_transport.h"
#include "world.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

#define TEST_PORT 27120

/* Long enough for anything on loopback, short enough to fail fast. */
#define TEST_WAIT_MS 2000

/* Under assets/maps/ of the test's working directory, where a client looks
 * for the map MSG_ACCEPT names; tests/CMakeLists.txt makes the directory. */
#define MAP_PATH "assets/maps/session_map.txt"

/*   0123456789
 * 0 ##########
 * 1 #P.......#
 * 2 #........#
 * 3 ########## */
static const char *const map_rows[] = {"##########", "#P.......#", "#........#", "##########"};

static const PlayerInput run_east = {0, BTN_FORWARD, 0.0f};

/* Sessions a test opened; tearDown() closes what the test has not. */
static GameSession host, client;
static int host_open, client_open;

/* A stand-in at the other end of the network, server or client. */
static NetHost *fake;
static NetPeerId fake_peer;
static MsgJoin fake_join;
static int fake_joins, fake_accepted, fake_snapshots;

void setUp(void)
{
    FILE *f = fopen(MAP_PATH, "w");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "no assets/maps/ beside the test: see tests/CMakeLists.txt");
    for (size_t i = 0; i < sizeof(map_rows) / sizeof(map_rows[0]); i++) {
        fprintf(f, "%s\n", map_rows[i]);
    }
    fclose(f);
    host_open = client_open = 0;
    fake = NULL;
    fake_peer = NET_PEER_NONE;
    memset(&fake_join, 0, sizeof(fake_join));
    fake_joins = fake_accepted = fake_snapshots = 0;
}

void tearDown(void)
{
    if (client_open) {
        client.close(client.ctx);
    }
    if (host_open) {
        host.close(host.ctx);
    }
    net_destroy(fake);
}

static Options options(LaunchMode mode)
{
    /* No map name: session_open() is given the path of the test's own map. */
    Options o = {mode, mode == LAUNCH_CLIENT ? "127.0.0.1" : NULL, TEST_PORT, "", SNAPSHOT_RATE,
                 NULL};
    return o;
}

static void open_host(LaunchMode mode, int snapshot_rate)
{
    Options o = options(mode);
    o.snapshot_rate = snapshot_rate;
    TEST_ASSERT_EQUAL_INT(0, session_open(&host, &o, MAP_PATH));
    host_open = 1;
}

static void open_client(const char *name)
{
    Options o = options(LAUNCH_CLIENT);
    o.name = name;
    TEST_ASSERT_EQUAL_INT(0, session_open(&client, &o, MAP_PATH));
    client_open = 1;
}

static void close_host(void)
{
    host.close(host.ctx);
    host_open = 0;
}

static void fake_poll(void)
{
    NetTransportEvent ev;
    while (fake && net_poll(fake, &ev)) {
        if (ev.type == NET_EV_CONNECT) {
            fake_peer = ev.peer;
        } else if (ev.type == NET_EV_DATA) {
            uint8_t type = ((const uint8_t *)ev.data)[0];
            if (type == MSG_JOIN && ev.size == sizeof(MsgJoin)) {
                memcpy(&fake_join, ev.data, sizeof(fake_join));
                fake_joins++;
            } else if (type == MSG_ACCEPT) {
                fake_accepted = 1;
            } else if (type == MSG_SNAPSHOT) {
                fake_snapshots++;
            }
        }
    }
}

/* One fixed step of the frame loop: the host ticks with `local`, then the
 * client and the stand-in take what came. Returns what the client's poll
 * found, SESS_SAME without a client. */
static SessionChange step(const PlayerInput *local)
{
    if (host_open) {
        host.tick(host.ctx, local, FIXED_DT);
        TEST_ASSERT_EQUAL_INT(SESS_SAME, host.poll(host.ctx));
    }
    fake_poll();
    return client_open ? client.poll(client.ctx) : SESS_SAME;
}

/* Step until the client's poll reports `want`, for TEST_WAIT_MS of real time
 * at most. */
static void step_until_client(SessionChange want)
{
    uint32_t start = net_time();
    SessionChange got = SESS_SAME;
    while (got != want && net_time() - start < TEST_WAIT_MS) {
        got = step(NULL);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(want, got, "the client's poll within TEST_WAIT_MS");
}

#define STEP_UNTIL(cond)                                                                           \
    do {                                                                                           \
        uint32_t start_ = net_time();                                                              \
        while (!(cond) && net_time() - start_ < TEST_WAIT_MS) {                                    \
            step(NULL);                                                                            \
        }                                                                                          \
        TEST_ASSERT_TRUE_MESSAGE(cond, #cond " within TEST_WAIT_MS");                              \
    } while (0)

/* ---- solo ---- */

static void test_solo_runs_a_world_of_its_own(void)
{
    GameSession s;
    Options o = options(LAUNCH_SOLO);
    TEST_ASSERT_EQUAL_INT(0, session_open(&s, &o, MAP_PATH));
    TEST_ASSERT_FALSE(s.shared);
    TEST_ASSERT_FALSE(s.remote);
    World *w = s.world(s.ctx);
    TEST_ASSERT_NOT_NULL(w);
    /* Everyone in it moves every tick: the frame draws the world itself. */
    TEST_ASSERT_EQUAL_PTR(w, s.view(s.ctx));
    TEST_ASSERT_EQUAL_INT(0, s.local_player(s.ctx));
    TEST_ASSERT_TRUE(w->players[0].active);

    float x = w->players[0].x;
    s.tick(s.ctx, &run_east, FIXED_DT);
    TEST_ASSERT_EQUAL_UINT32(1, w->tick);
    TEST_ASSERT_TRUE(w->players[0].x > x);
    TEST_ASSERT_EQUAL_INT(SESS_SAME, s.poll(s.ctx));

    /* Starting over is a fresh world, the player back where they spawned. */
    TEST_ASSERT_EQUAL_INT(0, s.restart(s.ctx));
    TEST_ASSERT_EQUAL_PTR(w, s.world(s.ctx));
    TEST_ASSERT_EQUAL_UINT32(0, w->tick);
    TEST_ASSERT_EQUAL_FLOAT(x, w->players[0].x);
    TEST_ASSERT_EQUAL_INT(0, s.local_player(s.ctx));
    s.close(s.ctx);
}

static void test_solo_without_its_map_does_not_open(void)
{
    GameSession s;
    Options o = options(LAUNCH_SOLO);
    TEST_ASSERT_EQUAL_INT(-1, session_open(&s, &o, "no_such_map.txt"));
}

/* ---- host ---- */

static void test_host_plays_on_its_own_server_and_shares_it(void)
{
    open_host(LAUNCH_HOST, SNAPSHOT_RATE);
    TEST_ASSERT_TRUE(host.shared);
    TEST_ASSERT_FALSE(host.remote);
    World *w = host.world(host.ctx);
    TEST_ASSERT_EQUAL_PTR(w, host.view(host.ctx));
    int me = host.local_player(host.ctx);
    TEST_ASSERT_EQUAL_INT(0, me);
    TEST_ASSERT_EQUAL_INT(me, w->local_player);
    TEST_ASSERT_TRUE(w->players[me].active);

    float x = w->players[me].x;
    step(&run_east);
    TEST_ASSERT_TRUE(w->players[me].x > x);

    /* With the player in a menu the world still goes on, and they stand. */
    x = w->players[me].x;
    uint32_t tick = w->tick;
    step(NULL);
    TEST_ASSERT_EQUAL_UINT32(tick + 1, w->tick);
    TEST_ASSERT_EQUAL_FLOAT(x, w->players[me].x);

    /* The world is everyone's: one player starting over leaves it as it is. */
    TEST_ASSERT_EQUAL_INT(0, host.restart(host.ctx));
    TEST_ASSERT_EQUAL_UINT32(tick + 1, w->tick);
    TEST_ASSERT_EQUAL_FLOAT(x, w->players[me].x);
}

static void test_dedicated_host_has_nobody_at_this_machine(void)
{
    open_host(LAUNCH_DEDICATED, SNAPSHOT_RATE);
    TEST_ASSERT_EQUAL_INT(-1, host.local_player(host.ctx));
    World *w = host.world(host.ctx);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        TEST_ASSERT_FALSE(w->players[i].active);
    }
    step(NULL);
    TEST_ASSERT_EQUAL_UINT32(1, w->tick);
}

static void test_host_on_a_taken_port_does_not_open(void)
{
    open_host(LAUNCH_DEDICATED, SNAPSHOT_RATE);
    GameSession s;
    Options o = options(LAUNCH_HOST);
    TEST_ASSERT_EQUAL_INT(-1, session_open(&s, &o, MAP_PATH));
}

/* What --snaprate sets is what the server sends: a second of ticks holds that
 * many snapshots, counted by a client written by hand. */
static void test_host_sends_the_snapshot_rate_it_was_opened_with(void)
{
    const int rates[] = {SNAPSHOT_RATE_MIN, TICK_RATE};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        open_host(LAUNCH_DEDICATED, rates[i]);
        fake = net_host_client();
        TEST_ASSERT_NOT_NULL(fake);
        NetPeerId server = net_connect(fake, "127.0.0.1", TEST_PORT);
        STEP_UNTIL(fake_peer == server);
        MsgJoin join;
        memset(&join, 0, sizeof(join));
        join.h.type = MSG_JOIN;
        join.proto = PROTO_VERSION;
        net_send(fake, server, NET_CH_RELIABLE, &join, sizeof(join), 1);
        STEP_UNTIL(fake_accepted);

        fake_snapshots = 0;
        for (int t = 0; t < TICK_RATE; t++) {
            step(NULL);
        }
        /* The last one may still be on its way. */
        uint32_t start = net_time();
        while (fake_snapshots < rates[i] && net_time() - start < 100) {
            fake_poll();
        }
        TEST_ASSERT_EQUAL_INT(rates[i], fake_snapshots);
        tearDown();
        setUp();
    }
}

/* ---- client ---- */

static void test_client_joins_plays_and_loses_the_game(void)
{
    open_host(LAUNCH_DEDICATED, SNAPSHOT_RATE);
    open_client("");
    TEST_ASSERT_TRUE(client.shared);
    TEST_ASSERT_TRUE(client.remote);
    TEST_ASSERT_NULL(client.world(client.ctx));
    TEST_ASSERT_NULL(client.view(client.ctx));
    step_until_client(SESS_JOINED);

    World *seen = client.world(client.ctx);
    TEST_ASSERT_NOT_NULL(seen);
    /* The others are drawn a moment back, in a world of the view's own. */
    World *view = client.view(client.ctx);
    TEST_ASSERT_NOT_NULL(view);
    TEST_ASSERT_TRUE(view != seen);
    int me = client.local_player(client.ctx);
    TEST_ASSERT_EQUAL_INT(0, me);
    TEST_ASSERT_TRUE(seen->players[me].active);

    /* A tick sends the input and runs nothing here: the server moves the
     * player, and the client sees it in a snapshot. */
    const PlayerState *there = &host.world(host.ctx)->players[me];
    float x = there->x;
    uint32_t tick = seen->tick;
    client.tick(client.ctx, &run_east, FIXED_DT);
    TEST_ASSERT_EQUAL_UINT32(tick, seen->tick);
    STEP_UNTIL(there->x > x);
    STEP_UNTIL(seen->players[me].x > x);

    /* The server goes, and says why; the world seen last stays. */
    close_host();
    step_until_client(SESS_LOST);
    TEST_ASSERT_NOT_NULL(client.world(client.ctx));
}

static void test_client_joins_again_when_restarted_after_losing_the_game(void)
{
    open_host(LAUNCH_DEDICATED, SNAPSHOT_RATE);
    open_client("");
    step_until_client(SESS_JOINED);
    close_host();
    step_until_client(SESS_LOST);

    open_host(LAUNCH_DEDICATED, SNAPSHOT_RATE);
    TEST_ASSERT_EQUAL_INT(0, client.restart(client.ctx));
    step_until_client(SESS_JOINED);
    TEST_ASSERT_EQUAL_INT(0, client.local_player(client.ctx));

    /* In the game already, restarting changes nothing. */
    TEST_ASSERT_EQUAL_INT(0, client.restart(client.ctx));
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(SESS_SAME, step(NULL));
    }
}

static void test_client_joins_under_its_name(void)
{
    fake = net_host_server(TEST_PORT, 1);
    TEST_ASSERT_NOT_NULL(fake);
    open_client("doomguy");
    STEP_UNTIL(fake_joins > 0);
    TEST_ASSERT_EQUAL_STRING("doomguy", fake_join.name);
}

static void test_client_with_an_address_that_does_not_resolve_does_not_open(void)
{
    GameSession s;
    Options o = options(LAUNCH_CLIENT);
    o.connect = "";
    TEST_ASSERT_EQUAL_INT(-1, session_open(&s, &o, MAP_PATH));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_solo_runs_a_world_of_its_own);
    RUN_TEST(test_solo_without_its_map_does_not_open);
    RUN_TEST(test_host_plays_on_its_own_server_and_shares_it);
    RUN_TEST(test_dedicated_host_has_nobody_at_this_machine);
    RUN_TEST(test_host_on_a_taken_port_does_not_open);
    RUN_TEST(test_host_sends_the_snapshot_rate_it_was_opened_with);
    RUN_TEST(test_client_joins_plays_and_loses_the_game);
    RUN_TEST(test_client_joins_again_when_restarted_after_losing_the_game);
    RUN_TEST(test_client_joins_under_its_name);
    RUN_TEST(test_client_with_an_address_that_does_not_resolve_does_not_open);
    return UNITY_END();
}
