/* net_transport.c over real sockets on 127.0.0.1: a server and its clients
 * live in this one process and are polled in turn.
 *
 * Timeouts are tested by moving ENet's clock with enet_time_set() rather than
 * by waiting for it — the host reads time from nowhere else. That is also why
 * this file, unlike the game, includes enet.h. */

#include <enet/enet.h>
#include <string.h>

#include "config.h"
#include "net_transport.h"
#include "unity.h"

/* Clear of NET_PORT_DEFAULT, so a game running alongside does not collide. */
#define TEST_PORT 27115

/* Long enough for anything on loopback, short enough to fail fast. */
#define TEST_WAIT_MS 2000

#define TEST_MAX_HOSTS 4
#define TEST_QUEUE_LEN 16
#define TEST_MAX_DATA 4096 /* room for a packet ENet has to fragment */

/* An event copied out of net_poll(): its data is only valid until the next
 * call, and the harness polls every host before a test looks. */
typedef struct {
    NetTransportEventType type;
    NetPeerId peer;
    size_t size;
    char data[TEST_MAX_DATA];
} Seen;

typedef struct {
    NetHost *host;
    int paused; /* not polled: as far as the network can tell, the process died */
    Seen queue[TEST_QUEUE_LEN];
    int count;
} Watched;

static Watched hosts[TEST_MAX_HOSTS];
static int host_count;

void setUp(void)
{
    memset(hosts, 0, sizeof(hosts));
    host_count = 0;
}

void tearDown(void)
{
    for (int i = 0; i < host_count; i++) {
        net_destroy(hosts[i].host);
    }
}

static Watched *watch(NetHost *h)
{
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_LESS_THAN_INT(TEST_MAX_HOSTS, host_count);
    hosts[host_count].host = h;
    return &hosts[host_count++];
}

static Watched *server(void)
{
    return watch(net_host_server(TEST_PORT, NET_MAX_PEERS));
}

static Watched *client(void)
{
    return watch(net_host_client());
}

static void poll_host(Watched *w)
{
    NetTransportEvent ev;
    while (net_poll(w->host, &ev)) {
        TEST_ASSERT_LESS_THAN_INT(TEST_QUEUE_LEN, w->count);
        Seen *s = &w->queue[w->count++];
        s->type = ev.type;
        s->peer = ev.peer;
        s->size = ev.size;
        if (ev.data) {
            TEST_ASSERT_LESS_OR_EQUAL_size_t(sizeof(s->data), ev.size);
            memcpy(s->data, ev.data, ev.size);
        }
    }
}

static void poll_all(void)
{
    for (int i = 0; i < host_count; i++) {
        if (hosts[i].host && !hosts[i].paused) {
            poll_host(&hosts[i]);
        }
    }
}

/* Keep every host talking for `ms` of real time. */
static void poll_for(uint32_t ms)
{
    uint32_t start = enet_time_get();
    while (enet_time_get() - start < ms) {
        poll_all();
    }
}

/* Poll every host until `w` has something, and take it. */
static Seen next_event(Watched *w)
{
    uint32_t start = enet_time_get();
    while (w->count == 0 && enet_time_get() - start < TEST_WAIT_MS) {
        poll_all();
    }
    TEST_ASSERT_TRUE_MESSAGE(w->count > 0, "no event within TEST_WAIT_MS");
    Seen s = w->queue[0];
    w->count--;
    memmove(&w->queue[0], &w->queue[1], (size_t)w->count * sizeof(Seen));
    return s;
}

static void expect_event(Watched *w, NetTransportEventType type, NetPeerId peer)
{
    Seen s = next_event(w);
    TEST_ASSERT_EQUAL_INT(type, s.type);
    TEST_ASSERT_EQUAL_INT(peer, s.peer);
}

static void expect_data(Watched *w, NetPeerId peer, const char *text)
{
    Seen s = next_event(w);
    TEST_ASSERT_EQUAL_INT(NET_EV_DATA, s.type);
    TEST_ASSERT_EQUAL_INT(peer, s.peer);
    TEST_ASSERT_EQUAL_size_t(strlen(text) + 1, s.size);
    TEST_ASSERT_EQUAL_STRING(text, s.data);
}

static void send_text(Watched *w, NetPeerId peer, int channel, const char *text, int reliable)
{
    net_send(w->host, peer, channel, text, strlen(text) + 1, reliable);
}

/* Connect `c` to `s` and return the number `c` knows the server by; the
 * server's number for `c` is checked to be `expected_on_server`. */
static NetPeerId connect_to(Watched *s, Watched *c, NetPeerId expected_on_server)
{
    NetPeerId id = net_connect(c->host, "127.0.0.1", TEST_PORT);
    TEST_ASSERT_NOT_EQUAL_INT(NET_PEER_NONE, id);
    expect_event(s, NET_EV_CONNECT, expected_on_server);
    expect_event(c, NET_EV_CONNECT, id);
    return id;
}

static void advance_clock(uint32_t ms)
{
    enet_time_set(enet_time_get() + ms);
}

static void test_peer_limit_out_of_range_is_refused(void)
{
    TEST_ASSERT_NULL(net_host_server(TEST_PORT, 0));
    TEST_ASSERT_NULL(net_host_server(TEST_PORT, NET_MAX_PEERS + 1));
}

static void test_port_in_use_is_refused(void)
{
    server();
    TEST_ASSERT_NULL(net_host_server(TEST_PORT, 1));
}

static void test_idle_host_reports_nothing(void)
{
    Watched *s = server();
    NetTransportEvent ev;
    TEST_ASSERT_EQUAL_INT(0, net_poll(s->host, &ev));
    TEST_ASSERT_EQUAL_INT(NET_EV_NONE, ev.type);
    TEST_ASSERT_EQUAL_INT(NET_PEER_NONE, ev.peer);
    TEST_ASSERT_NULL(ev.data);
}

/* The game polls once a frame; one that waited even a millisecond for
 * traffic would cost frames. 200 polls of an idle host take microseconds
 * each, a poll that waits 1 ms takes 200 ms. */
static void test_poll_does_not_wait(void)
{
    Watched *s = server();
    NetTransportEvent ev;
    uint32_t start = enet_time_get();
    for (int i = 0; i < 200; i++) {
        net_poll(s->host, &ev);
    }
    TEST_ASSERT_LESS_THAN_UINT32(100, enet_time_get() - start);
}

static void test_both_sides_see_the_connection(void)
{
    Watched *s = server();
    Watched *c = client();
    TEST_ASSERT_EQUAL_INT(0, connect_to(s, c, 0));
}

static void test_server_echoes_on_both_channels(void)
{
    Watched *s = server();
    Watched *c = client();
    NetPeerId to_server = connect_to(s, c, 0);

    send_text(c, to_server, NET_CH_RELIABLE, "join", 1);
    expect_data(s, 0, "join");
    send_text(s, 0, NET_CH_RELIABLE, "join", 1);
    expect_data(c, to_server, "join");

    send_text(c, to_server, NET_CH_UNRELIABLE, "input", 0);
    expect_data(s, 0, "input");
    send_text(s, 0, NET_CH_UNRELIABLE, "input", 0);
    expect_data(c, to_server, "input");
}

/* A snapshot near the 1400 bytes requirements.md N9 allows no longer fits in
 * one ENet datagram and goes out in unreliable fragments; it arrives whole. */
static void test_unreliable_packet_larger_than_a_datagram_arrives_whole(void)
{
    Watched *s = server();
    Watched *c = client();
    NetPeerId to_server = connect_to(s, c, 0);

    static char big[3000];
    for (size_t i = 0; i < sizeof(big); i++) {
        big[i] = (char)(i * 7);
    }
    net_send(s->host, 0, NET_CH_UNRELIABLE, big, sizeof(big), 0);

    Seen got = next_event(c);
    TEST_ASSERT_EQUAL_INT(NET_EV_DATA, got.type);
    TEST_ASSERT_EQUAL_INT(to_server, got.peer);
    TEST_ASSERT_EQUAL_size_t(sizeof(big), got.size);
    TEST_ASSERT_EQUAL_MEMORY(big, got.data, sizeof(big));
}

/* A second client gets the next number on the server, and both get what is
 * broadcast. */
static void test_broadcast_reaches_every_client(void)
{
    Watched *s = server();
    Watched *a = client();
    Watched *b = client();
    NetPeerId a_to_server = connect_to(s, a, 0);
    NetPeerId b_to_server = connect_to(s, b, 1);

    net_broadcast(s->host, NET_CH_UNRELIABLE, "snap", 5, 0);
    expect_data(a, a_to_server, "snap");
    expect_data(b, b_to_server, "snap");
}

/* A packet for a peer still connecting is dropped, not queued for later. */
static void test_send_before_connected_is_dropped(void)
{
    Watched *s = server();
    Watched *c = client();
    NetPeerId id = net_connect(c->host, "127.0.0.1", TEST_PORT);
    send_text(c, id, NET_CH_RELIABLE, "early", 1);

    expect_event(s, NET_EV_CONNECT, 0);
    expect_event(c, NET_EV_CONNECT, id);
    poll_for(50);
    TEST_ASSERT_EQUAL_INT(0, s->count);
}

/* The side that disconnects is told nothing more; the other side is, and
 * the number goes to the next connection. */
static void test_disconnect_frees_the_number(void)
{
    Watched *s = server();
    Watched *a = client();
    NetPeerId id = connect_to(s, a, 0);

    net_disconnect(a->host, id);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, net_peer_rtt(a->host, id));
    expect_event(s, NET_EV_DISCONNECT, 0);
    poll_for(50);
    TEST_ASSERT_EQUAL_INT(0, a->count);

    Watched *b = client();
    connect_to(s, b, 0);
}

/* Packets queued before net_disconnect() still leave — how a reject reaches
 * the client it rejects. What the client sent meanwhile is not reported. */
static void test_disconnect_delivers_what_was_queued(void)
{
    Watched *s = server();
    Watched *c = client();
    NetPeerId to_server = connect_to(s, c, 0);

    send_text(c, to_server, NET_CH_RELIABLE, "join", 1);
    poll_host(c); /* on its way; the server has not read it yet */
    send_text(s, 0, NET_CH_RELIABLE, "full", 1);
    net_disconnect(s->host, 0);

    expect_data(c, to_server, "full");
    expect_event(c, NET_EV_DISCONNECT, to_server);
    TEST_ASSERT_EQUAL_INT(0, s->count);
}

/* A host destroyed right after net_disconnect() still sends what was queued,
 * and nothing behind it: ENet drops a packet that a disconnect follows into
 * the same receive. How a server that goes away tells its clients so. */
static void test_destroy_delivers_what_was_queued_for_a_peer_let_go(void)
{
    Watched *s = server();
    Watched *c = client();
    NetPeerId to_server = connect_to(s, c, 0);

    send_text(s, 0, NET_CH_RELIABLE, "bye", 1);
    net_disconnect(s->host, 0);
    net_destroy(s->host);
    s->host = NULL;
    expect_data(c, to_server, "bye");
}

/* A client that quits says so: the server does not wait for the timeout. */
static void test_destroying_a_client_disconnects_it_at_once(void)
{
    Watched *s = server();
    Watched *c = client();
    connect_to(s, c, 0);

    net_destroy(c->host);
    c->host = NULL;
    expect_event(s, NET_EV_DISCONNECT, 0);
}

/* What kill -9 looks like from the server: nothing at all, ever again. */
static void test_silent_peer_is_dropped_after_the_timeout(void)
{
    Watched *s = server();
    Watched *c = client();
    connect_to(s, c, 0);
    c->paused = 1;

    advance_clock(NET_PEER_TIMEOUT_MS - 1000);
    poll_for(20);
    TEST_ASSERT_EQUAL_INT(0, s->count);

    advance_clock(1500);
    expect_event(s, NET_EV_DISCONNECT, 0);
}

/* Silence is what times a peer out, not the age of the connection. */
static void test_answering_peer_outlives_the_timeout(void)
{
    Watched *s = server();
    Watched *c = client();
    NetPeerId to_server = connect_to(s, c, 0);

    for (int i = 0; i < 3; i++) {
        advance_clock(NET_PEER_TIMEOUT_MS - 1000);
        poll_for(50);
    }
    TEST_ASSERT_EQUAL_INT(0, s->count);
    TEST_ASSERT_EQUAL_INT(0, c->count);
    send_text(c, to_server, NET_CH_RELIABLE, "alive", 1);
    expect_data(s, 0, "alive");
}

/* A server that never answers: the attempt ends in NET_EV_DISCONNECT for the
 * number net_connect() returned. ENet's default would wait 30 s. */
static void test_connect_to_nobody_gives_up(void)
{
    Watched *c = client();
    NetPeerId id = net_connect(c->host, "127.0.0.1", TEST_PORT);
    TEST_ASSERT_NOT_EQUAL_INT(NET_PEER_NONE, id);
    poll_for(20);
    TEST_ASSERT_EQUAL_INT(0, c->count);

    advance_clock(NET_PEER_TIMEOUT_MS + 1000);
    expect_event(c, NET_EV_DISCONNECT, id);
}

static void test_client_holds_one_connection(void)
{
    Watched *c = client();
    TEST_ASSERT_NOT_EQUAL_INT(NET_PEER_NONE, net_connect(c->host, "127.0.0.1", TEST_PORT));
    TEST_ASSERT_EQUAL_INT(NET_PEER_NONE, net_connect(c->host, "127.0.0.1", TEST_PORT));
}

static void test_rtt_and_loss_of_a_connected_peer(void)
{
    Watched *s = server();
    Watched *c = client();
    connect_to(s, c, 0);

    float rtt = net_peer_rtt(s->host, 0);
    TEST_ASSERT_TRUE(rtt >= 1.0f && rtt < 250.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, net_peer_loss(s->host, 0));
}

/* The clock a caller times its own timeouts by is the one a test moves. */
static void test_net_time_is_enets_clock(void)
{
    uint32_t before = net_time();
    enet_time_set(enet_time_get() + 10000);
    uint32_t after = net_time();
    TEST_ASSERT_TRUE(after - before >= 10000 && after - before < 10000 + TEST_WAIT_MS);
    TEST_ASSERT_UINT32_WITHIN(1, enet_time_get(), after);
}

static void test_unknown_peers_are_ignored(void)
{
    Watched *s = server();
    Watched *c = client();
    NetPeerId to_server = connect_to(s, c, 0);

    send_text(c, NET_PEER_NONE, NET_CH_RELIABLE, "x", 1);
    send_text(c, 1, NET_CH_RELIABLE, "x", 1);
    send_text(c, to_server, NET_CH_COUNT, "x", 1);
    send_text(c, to_server, -1, "x", 1);
    net_broadcast(c->host, NET_CH_COUNT, "x", 2, 1);
    net_disconnect(c->host, NET_PEER_NONE);
    net_disconnect(c->host, NET_MAX_PEERS);
    poll_for(50);
    TEST_ASSERT_EQUAL_INT(0, s->count);

    TEST_ASSERT_EQUAL_FLOAT(0.0f, net_peer_rtt(s->host, NET_PEER_NONE));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, net_peer_rtt(s->host, 5));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, net_peer_loss(s->host, NET_MAX_PEERS));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, net_peer_rtt(NULL, 0));
    TEST_ASSERT_EQUAL_INT(NET_PEER_NONE, net_connect(c->host, NULL, TEST_PORT));
    TEST_ASSERT_EQUAL_INT(NET_PEER_NONE, net_connect(NULL, "127.0.0.1", TEST_PORT));

    NetTransportEvent ev;
    TEST_ASSERT_EQUAL_INT(0, net_poll(NULL, &ev));
    TEST_ASSERT_EQUAL_INT(0, net_poll(s->host, NULL));
    net_destroy(NULL);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_peer_limit_out_of_range_is_refused);
    RUN_TEST(test_port_in_use_is_refused);
    RUN_TEST(test_idle_host_reports_nothing);
    RUN_TEST(test_poll_does_not_wait);
    RUN_TEST(test_both_sides_see_the_connection);
    RUN_TEST(test_server_echoes_on_both_channels);
    RUN_TEST(test_unreliable_packet_larger_than_a_datagram_arrives_whole);
    RUN_TEST(test_broadcast_reaches_every_client);
    RUN_TEST(test_send_before_connected_is_dropped);
    RUN_TEST(test_disconnect_frees_the_number);
    RUN_TEST(test_disconnect_delivers_what_was_queued);
    RUN_TEST(test_destroy_delivers_what_was_queued_for_a_peer_let_go);
    RUN_TEST(test_destroying_a_client_disconnects_it_at_once);
    RUN_TEST(test_silent_peer_is_dropped_after_the_timeout);
    RUN_TEST(test_answering_peer_outlives_the_timeout);
    RUN_TEST(test_connect_to_nobody_gives_up);
    RUN_TEST(test_client_holds_one_connection);
    RUN_TEST(test_rtt_and_loss_of_a_connected_peer);
    RUN_TEST(test_net_time_is_enets_clock);
    RUN_TEST(test_unknown_peers_are_ignored);
    return UNITY_END();
}
