/* net_protocol.c: a world turned into a snapshot, the snapshot into bytes and
 * back, and the result put into another world; and which events a snapshot
 * carries to the player who receives it.
 *
 * The other world is what a client holds — the same map loaded fresh, with
 * nobody in it — so a round trip through snapshot_write(), snapshot_encode(),
 * snapshot_decode() and snapshot_apply() checks exactly what the client will
 * see. No sockets: what goes over them is the bytes, and those are compared
 * here.
 *
 * The map is written out by the test, as in replay_test.c. */

#include "unity.h"
#include "net_protocol.h"
#include "world.h"
#include "config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAP_PATH "net_protocol_map.txt"

/* Two enemy types, all three pickups and a door, which is all a snapshot
 * carries besides the players.
 *
 *   0123456789
 * 0 ##########
 * 1 #P..#....#
 * 2 #M..D.E..#
 * 3 #A..#.S..#
 * 4 #R..#....#
 * 5 ########## */
static const char *const map_rows[] = {
        "##########", "#P..#....#", "#M..D.E..#", "#A..#.S..#", "#R..#....#", "##########",
};

/* Static: a World is tens of kilobytes, too much for the stack of a test. */
static World server, client;
static MsgSnapshot snap, decoded;
static uint8_t wire[sizeof(MsgSnapshot)];

static void write_map(void)
{
    FILE *f = fopen(MAP_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    for (size_t i = 0; i < sizeof(map_rows) / sizeof(map_rows[0]); i++) {
        fprintf(f, "%s\n", map_rows[i]);
    }
    fclose(f);
}

void setUp(void)
{
    write_map();
    TEST_ASSERT_EQUAL_INT(0, world_load(&server, MAP_PATH));
    TEST_ASSERT_EQUAL_INT(0, world_load(&client, MAP_PATH));
    memset(&snap, 0, sizeof(snap));
    memset(&decoded, 0, sizeof(decoded));
    memset(wire, 0, sizeof(wire));
}

void tearDown(void)
{
    remove(MAP_PATH);
}

/* Server to client the way the network will do it: through the bytes. */
static size_t send_snapshot(uint32_t last_input_tick)
{
    snapshot_write(&snap, &server, last_input_tick);
    size_t size = snapshot_encode(&snap, wire);
    TEST_ASSERT_TRUE(size > 0);
    TEST_ASSERT_EQUAL_INT(0, snapshot_decode(&decoded, wire, size));
    snapshot_apply(&client, &decoded);
    return size;
}

static void add_players(World *w, int n)
{
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_INT(i, world_add_player(w, NULL));
    }
}

/* ---- size ---- */

/* N9: with every list full the snapshot is still one packet. The assertions in
 * net_protocol.h hold this at compile time; this is the number they pin. */
static void test_a_full_snapshot_fits_in_one_packet(void)
{
    TEST_ASSERT_EQUAL_size_t(16 + 10 * 24 + 32 * 12 + 8 + 64 + 32 * 12, sizeof(MsgSnapshot));
    TEST_ASSERT_TRUE(sizeof(MsgSnapshot) <= SNAPSHOT_MAX_BYTES);

    snap.h.type = MSG_SNAPSHOT;
    snap.player_count = MAX_PLAYERS;
    snap.enemy_count = MAX_ENEMIES;
    snap.event_count = MAX_EVENTS;
    TEST_ASSERT_EQUAL_size_t(sizeof(MsgSnapshot), snapshot_encode(&snap, wire));
}

/* Deathmatch has no enemies, so the enemy list goes out empty instead of as
 * 32 blank slots: 16 + 10×24 + 8 + 64 + 5×12 bytes. */
static void test_a_deathmatch_snapshot_is_about_390_bytes(void)
{
    add_players(&server, MAX_PLAYERS);
    server.enemies.count = 0;
    for (int i = 0; i < 5; i++) {
        event_push(&server.events, EV_SHOT, i, 1.5f, 1.5f);
    }

    TEST_ASSERT_EQUAL_size_t(388, send_snapshot(0));
}

/* Every list is cut to its count, and nothing else changes size. */
static void test_each_list_is_cut_to_its_count(void)
{
    snap.h.type = MSG_SNAPSHOT;
    TEST_ASSERT_EQUAL_size_t(16 + 8 + 64, snapshot_encode(&snap, wire));

    snap.player_count = 3;
    snap.enemy_count = 2;
    snap.event_count = 1;
    TEST_ASSERT_EQUAL_size_t(16 + 3 * 24 + 2 * 12 + 8 + 64 + 1 * 12, snapshot_encode(&snap, wire));
}

/* ---- the round trip ---- */

static void test_players_arrive_where_the_server_has_them(void)
{
    add_players(&server, 3);
    PlayerState *p = &server.players[2];
    p->x = 2.25f;
    p->y = 3.75f;
    p->angle = -1.25f;
    p->hp = 64.0f;
    p->armor = 30.0f;
    /* the last weapon there is, picked up: the whole range goes on the wire */
    p->weapons.weapons[WEAPON_GRENADE].owned = 1;
    weapon_switch(p, WEAPON_GRENADE);
    p->weapons.weapons[WEAPON_GRENADE].ammo = 7;
    server.players[1].alive = 0;
    server.players[1].hp = 0.0f;

    send_snapshot(0);

    TEST_ASSERT_EQUAL_INT(3, client.player_count);
    for (int i = 0; i < 3; i++) {
        const PlayerState *s = &server.players[i];
        const PlayerState *c = &client.players[i];
        TEST_ASSERT_TRUE(c->active);
        TEST_ASSERT_EQUAL_UINT8(i, c->id);
        TEST_ASSERT_EQUAL_FLOAT(s->x, c->x);
        TEST_ASSERT_EQUAL_FLOAT(s->y, c->y);
        TEST_ASSERT_EQUAL_FLOAT(s->angle, c->angle);
        TEST_ASSERT_EQUAL_FLOAT(s->hp, c->hp);
        TEST_ASSERT_EQUAL_FLOAT(s->armor, c->armor);
        TEST_ASSERT_EQUAL_INT(s->alive, c->alive);
        TEST_ASSERT_EQUAL_INT(s->weapons.current, c->weapons.current);
    }
    TEST_ASSERT_EQUAL_INT(WEAPON_GRENADE, client.players[2].weapons.current);
    TEST_ASSERT_EQUAL_INT(7, client.players[2].weapons.weapons[WEAPON_GRENADE].ammo);
}

/* A slot freed on the server is left out of the snapshot, and the ids of the
 * players after it keep meaning what they meant. */
static void test_a_free_slot_stays_free_and_ids_hold(void)
{
    add_players(&server, 3);
    world_remove_player(&server, 1);

    send_snapshot(0);

    TEST_ASSERT_EQUAL_UINT8(2, snap.player_count);
    TEST_ASSERT_TRUE(client.players[0].active);
    TEST_ASSERT_FALSE(client.players[1].active);
    TEST_ASSERT_TRUE(client.players[2].active);
    TEST_ASSERT_EQUAL_FLOAT(server.players[2].x, client.players[2].x);
}

/* Someone the client still has but the snapshot does not list has left. Which
 * player is this client's own is not the snapshot's to change. */
static void test_a_player_the_snapshot_leaves_out_has_left(void)
{
    add_players(&server, 3);
    send_snapshot(0);
    client.local_player = 1;
    world_remove_player(&server, 1);

    send_snapshot(0);

    TEST_ASSERT_FALSE(client.players[1].active);
    TEST_ASSERT_FALSE(client.players[1].alive);
    TEST_ASSERT_EQUAL_INT(1, client.local_player);
}

/* A player who appears in an empty slot has what a snapshot does not carry —
 * the weapons that are not in hand, their limits — from player_init(). */
static void test_a_new_player_starts_from_player_init(void)
{
    add_players(&server, 1);

    send_snapshot(0);

    const WeaponSystem *ws = &client.players[0].weapons;
    TEST_ASSERT_EQUAL_INT(server.players[0].weapons.weapons[WEAPON_PISTOL].max_ammo,
                          ws->weapons[WEAPON_PISTOL].max_ammo);
    TEST_ASSERT_EQUAL_INT(server.players[0].weapons.weapons[WEAPON_SHOTGUN].ammo,
                          ws->weapons[WEAPON_SHOTGUN].ammo);
    TEST_ASSERT_EQUAL_INT8(-1, client.players[0].last_attacker);
}

/* hp travels as whole points, rounded up: the client reads hp <= 0 as dead,
 * and someone with half a point left is not. */
static void test_hp_rounds_up_so_the_living_never_arrive_at_zero(void)
{
    add_players(&server, 3);
    server.players[0].hp = 0.25f;
    server.players[1].hp = 37.5f;
    server.players[2].hp = 0.0f;
    server.players[2].armor = 99.1f;
    server.enemies.items[0].hp = 0.5f;

    send_snapshot(0);

    TEST_ASSERT_EQUAL_FLOAT(1.0f, client.players[0].hp);
    TEST_ASSERT_EQUAL_FLOAT(38.0f, client.players[1].hp);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, client.players[2].hp);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, client.players[2].armor);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, client.enemies.items[0].hp);
}

/* A byte holds no more than 255 points; more than that arrives as 255 rather
 * than wrapped round to a few. */
static void test_points_past_a_byte_arrive_as_255(void)
{
    add_players(&server, 1);
    server.players[0].hp = 300.0f;

    send_snapshot(0);

    TEST_ASSERT_EQUAL_FLOAT(255.0f, client.players[0].hp);
}

/* Nothing the simulation should never produce turns into garbage on the way:
 * negative and NaN points are 0, openness outside 0..1 is pinned to it, and
 * ammo is pinned to what a uint16_t holds. */
static void test_values_out_of_range_are_pinned(void)
{
    add_players(&server, 3);
    server.players[0].hp = -5.0f;
    server.players[0].armor = NAN;
    server.players[0].weapons.weapons[WEAPON_PISTOL].ammo = -3;
    server.players[1].weapons.weapons[WEAPON_PISTOL].ammo = 70000;
    server.doors.doors[0].openness = 1.5f;

    send_snapshot(0);

    TEST_ASSERT_EQUAL_UINT8(0, snap.players[0].hp);
    TEST_ASSERT_EQUAL_UINT8(0, snap.players[0].armor);
    TEST_ASSERT_EQUAL_UINT16(0, snap.players[0].ammo);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, snap.players[1].ammo);
    TEST_ASSERT_EQUAL_UINT8(255, snap.doors[0]);

    server.doors.doors[0].openness = -0.5f;
    send_snapshot(0);
    TEST_ASSERT_EQUAL_UINT8(0, snap.doors[0]);
}

/* The fields the simulation has nothing for yet go out as zero, not as
 * whatever was in memory. */
static void test_fields_with_nothing_behind_them_are_zero(void)
{
    add_players(&server, 1);
    event_push(&server.events, EV_SHOT, 0, 1.5f, 1.5f);

    send_snapshot(0);

    TEST_ASSERT_EQUAL_UINT8(0, decoded.players[0].flags);
    TEST_ASSERT_EQUAL_UINT16(0, decoded.players[0].frags);
    TEST_ASSERT_EQUAL_UINT16(0, decoded.players[0].deaths);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.events[0].arg);
}

/* snapshot_apply() trusts its struct no further than it has to: an entry
 * whose id or weapon would index past an array is passed over, not written. */
static void test_apply_passes_over_a_player_it_cannot_index(void)
{
    snap.h.type = MSG_SNAPSHOT;
    snap.player_count = 2;
    snap.players[0].id = MAX_PLAYERS;
    snap.players[1].id = 0;
    snap.players[1].weapon = WEAPON_COUNT;

    snapshot_apply(&client, &snap);

    TEST_ASSERT_EQUAL_INT(0, client.player_count);
    TEST_ASSERT_FALSE(client.players[0].active);

    /* ...and a sound entry next to a bad one still gets through. */
    snap.player_count = 3;
    snap.players[2].id = 1;
    snap.players[2].x = 2.5f;
    snapshot_apply(&client, &snap);

    TEST_ASSERT_EQUAL_INT(2, client.player_count);
    TEST_ASSERT_TRUE(client.players[1].active);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, client.players[1].x);
}

/* The client holds as many enemies and events as the snapshot says, and never
 * more than its arrays do, whatever a struct handed to it claims. */
static void test_apply_takes_its_counts_from_the_snapshot(void)
{
    server.enemies.count = 1;
    send_snapshot(0);
    TEST_ASSERT_EQUAL_INT(1, client.enemies.count);

    snap.enemy_count = MAX_ENEMIES + 1;
    snap.event_count = MAX_EVENTS + 1;
    snapshot_apply(&client, &snap);
    TEST_ASSERT_EQUAL_INT(MAX_ENEMIES, client.enemies.count);
    TEST_ASSERT_EQUAL_INT(MAX_EVENTS, client.events.count);
}

static void test_enemies_arrive_where_the_server_has_them(void)
{
    TEST_ASSERT_EQUAL_INT(2, server.enemies.count);
    Enemy *imp = &server.enemies.items[0];
    Enemy *serg = &server.enemies.items[1];
    imp->x = 6.75f;
    imp->y = 1.25f;
    imp->state = ESTATE_CHASE;
    imp->hp = 12.0f;
    serg->state = ESTATE_DEAD;
    serg->hp = 0.0f;

    send_snapshot(0);

    TEST_ASSERT_EQUAL_INT(2, client.enemies.count);
    for (int i = 0; i < 2; i++) {
        const Enemy *s = &server.enemies.items[i];
        const Enemy *c = &client.enemies.items[i];
        TEST_ASSERT_EQUAL_INT(s->type, c->type);
        TEST_ASSERT_EQUAL_INT(s->state, c->state);
        TEST_ASSERT_EQUAL_FLOAT(s->hp, c->hp);
        TEST_ASSERT_EQUAL_FLOAT(s->x, c->x);
        TEST_ASSERT_EQUAL_FLOAT(s->y, c->y);
    }
}

/* A client that has fewer enemies than the server gets the rest spawned, so
 * that what the snapshot does not carry starts out as enemy_spawn() leaves it. */
static void test_enemies_the_client_lacks_are_spawned(void)
{
    client.enemies.count = 0;

    send_snapshot(0);

    TEST_ASSERT_EQUAL_INT(2, client.enemies.count);
    TEST_ASSERT_EQUAL_INT(ENEMY_SERG, client.enemies.items[1].type);
    TEST_ASSERT_EQUAL_INT(-1, client.enemies.items[1].target);
    TEST_ASSERT_EQUAL_FLOAT(server.enemies.items[1].x, client.enemies.items[1].x);
}

static void test_the_items_taken_are_gone_on_the_client(void)
{
    TEST_ASSERT_EQUAL_INT(3, server.items.count);
    server.items.items[1].active = 0;

    send_snapshot(0);

    TEST_ASSERT_EQUAL_UINT64(0x5, snap.items_alive);
    TEST_ASSERT_TRUE(client.items.items[0].active);
    TEST_ASSERT_FALSE(client.items.items[1].active);
    TEST_ASSERT_TRUE(client.items.items[2].active);
}

/* Openness goes as a byte, so it comes back to within 1/255; closed and open
 * come back exactly, and the state with them. */
static void test_doors_arrive_as_open_as_on_the_server(void)
{
    TEST_ASSERT_EQUAL_INT(1, server.doors.count);
    Door *d = &server.doors.doors[0];

    d->state = DOOR_OPENING;
    d->openness = 0.4f;
    send_snapshot(0);
    TEST_ASSERT_FLOAT_WITHIN(1.0f / 255.0f, 0.4f, client.doors.doors[0].openness);
    TEST_ASSERT_EQUAL_INT(DOOR_OPENING, client.doors.doors[0].state);

    d->state = DOOR_OPEN;
    d->openness = 1.0f;
    send_snapshot(0);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, client.doors.doors[0].openness);
    TEST_ASSERT_EQUAL_INT(DOOR_OPEN, client.doors.doors[0].state);

    d->state = DOOR_CLOSING;
    d->openness = 0.6f;
    send_snapshot(0);
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSING, client.doors.doors[0].state);

    d->state = DOOR_CLOSED;
    d->openness = 0.0f;
    send_snapshot(0);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, client.doors.doors[0].openness);
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, client.doors.doors[0].state);
}

/* A door caught at the same openness by two snapshots in a row keeps going the
 * way it was going. */
static void test_a_door_that_has_not_moved_keeps_its_direction(void)
{
    Door *d = &server.doors.doors[0];
    d->state = DOOR_CLOSING;
    d->openness = 0.6f;
    client.doors.doors[0].state = DOOR_CLOSING;
    client.doors.doors[0].openness = 1.0f;
    send_snapshot(0);

    send_snapshot(0);

    TEST_ASSERT_EQUAL_INT(DOOR_CLOSING, client.doors.doors[0].state);
}

static void test_events_replace_the_client_queue(void)
{
    event_push(&client.events, EV_NO_AMMO, 0, 0.0f, 0.0f);
    event_push(&server.events, EV_SHOT, 3, 1.5f, 2.5f);
    event_push(&server.events, EV_ENEMY_DEATH, 1, 6.5f, 3.5f);

    send_snapshot(0);

    TEST_ASSERT_EQUAL_INT(2, client.events.count);
    for (int i = 0; i < 2; i++) {
        const GameEvent *s = &server.events.items[i];
        const GameEvent *c = &client.events.items[i];
        TEST_ASSERT_EQUAL_UINT8(s->kind, c->kind);
        TEST_ASSERT_EQUAL_UINT8(s->actor, c->actor);
        TEST_ASSERT_EQUAL_FLOAT(s->x, c->x);
        TEST_ASSERT_EQUAL_FLOAT(s->y, c->y);
        TEST_ASSERT_EQUAL_UINT8(0, snap.events[i].arg);
    }
}

/* A server that collects the events of several ticks hands in more than a
 * snapshot holds: the first MAX_EVENTS go, the rest stay behind. */
static void test_set_events_keeps_the_first_that_fit(void)
{
    GameEvent many[MAX_EVENTS + 5];
    for (int i = 0; i < MAX_EVENTS + 5; i++) {
        many[i].kind = EV_DOOR;
        many[i].actor = (uint8_t)i;
        many[i].x = (float)i;
        many[i].y = 0.5f;
    }
    event_push(&server.events, EV_SHOT, 0, 1.5f, 1.5f);
    snapshot_write(&snap, &server, 0);

    snapshot_set_events(&snap, many, MAX_EVENTS + 5);
    TEST_ASSERT_EQUAL_UINT8(MAX_EVENTS, snap.event_count);
    TEST_ASSERT_EQUAL_UINT8(EV_DOOR, snap.events[0].kind);
    TEST_ASSERT_EQUAL_UINT8(MAX_EVENTS - 1, snap.events[MAX_EVENTS - 1].actor);
    TEST_ASSERT_EQUAL_FLOAT(MAX_EVENTS - 1, snap.events[MAX_EVENTS - 1].x);

    snapshot_set_events(&snap, many, 1);
    TEST_ASSERT_EQUAL_UINT8(1, snap.event_count);
    TEST_ASSERT_EQUAL_UINT8(0, snap.events[1].kind); /* what the longer list left is cleared */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, snap.events[1].x);
}

/* ---- the events a receiver gets ---- */

#define LISTENER 2

static PlayerState listener_at(float x, float y)
{
    PlayerState p;
    memset(&p, 0, sizeof(p));
    p.id = LISTENER;
    p.x = x;
    p.y = y;
    return p;
}

static GameEvent event_at(GameEventKind kind, int actor, float x, float y)
{
    GameEvent e = {(uint8_t)kind, (uint8_t)actor, x, y};
    return e;
}

static void assert_picked(int i, GameEventKind kind, int actor)
{
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kind, snap.events[i].kind, "kind");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(actor, snap.events[i].actor, "actor");
}

/* The receiver's own events go first, as they happened, however far off they
 * were posted — pain is posted where the hit came from. The rest follow
 * nearest first, and two as near as each other in the order they happened. */
static void test_picked_events_are_the_receivers_then_the_nearest(void)
{
    PlayerState me = listener_at(0.0f, 0.0f);
    const GameEvent in[] = {
            event_at(EV_DOOR, 0, 5.0f, 0.0f),                /* 5 away */
            event_at(EV_SHOT, LISTENER, 0.0f, 0.0f),         /* own */
            event_at(EV_ENEMY_HURT, 1, 0.0f, 1.0f),          /* 1 away */
            event_at(EV_PLAYER_HURT, LISTENER, 11.5f, 0.0f), /* own, hit from afar */
            event_at(EV_SHOT, 4, 3.0f, 0.0f),                /* 3 away */
            event_at(EV_ENEMY_DEATH, 7, -1.0f, 0.0f),        /* 1 away, later */
    };

    snapshot_pick_events(&snap, in, 6, &me);

    TEST_ASSERT_EQUAL_UINT8(6, snap.event_count);
    assert_picked(0, EV_SHOT, LISTENER);
    assert_picked(1, EV_PLAYER_HURT, LISTENER);
    assert_picked(2, EV_ENEMY_HURT, 1);
    assert_picked(3, EV_ENEMY_DEATH, 7);
    assert_picked(4, EV_SHOT, 4);
    assert_picked(5, EV_DOOR, 0);
    TEST_ASSERT_EQUAL_FLOAT(11.5f, snap.events[1].x);
}

/* What the receiver would not hear is not sent: another player's pickup and
 * empty click wherever they are, and anything past the range of its kind. */
static void test_what_the_receiver_cannot_hear_is_not_picked(void)
{
    PlayerState me = listener_at(4.0f, 4.0f);
    const GameEvent in[] = {
            event_at(EV_PICKUP, 4, 4.0f, 4.0f),
            event_at(EV_NO_AMMO, 4, 4.0f, 4.0f),
            event_at(EV_SHOT, 4, 4.0f + event_range(EV_SHOT) + 0.5f, 4.0f),
            event_at(EV_DOOR, 3, 4.0f, 4.0f + event_range(EV_DOOR)),
            event_at(EV_ENEMY_HURT, 1, 5.0f, 4.0f),
    };

    snapshot_pick_events(&snap, in, 5, &me);

    TEST_ASSERT_EQUAL_UINT8(1, snap.event_count);
    assert_picked(0, EV_ENEMY_HURT, 1);
    TEST_ASSERT_EQUAL_UINT8(0, snap.events[1].kind); /* the rest of the list is cleared */
}

/* The three ticks between two snapshots post more than one carries: here each
 * posts a full queue, and the n-th door of each lies about as far as the n-th
 * of the others. What goes is the nearest MAX_EVENTS of all three ticks —
 * not the first tick's, which is all the list would hold in order. */
static void test_the_nearest_of_every_tick_since_the_last_snapshot_are_picked(void)
{
    PlayerState me = listener_at(0.0f, 0.0f);
    GameEvent in[3 * MAX_EVENTS];
    for (int t = 0; t < 3; t++) {
        for (int i = 0; i < MAX_EVENTS; i++) {
            float dist = (float)i * 0.4f + (float)t * 0.1f;
            in[t * MAX_EVENTS + i] = event_at(EV_DOOR, t * MAX_EVENTS + i, dist, 0.0f);
        }
    }

    snapshot_pick_events(&snap, in, 3 * MAX_EVENTS, &me);

    /* nearest first: door i of ticks 0, 1 and 2, then door i + 1 */
    TEST_ASSERT_EQUAL_UINT8(MAX_EVENTS, snap.event_count);
    for (int k = 0; k < MAX_EVENTS; k++) {
        assert_picked(k, EV_DOOR, (k % 3) * MAX_EVENTS + k / 3);
    }
}

static void test_the_ticks_travel_with_the_snapshot(void)
{
    server.tick = 1234;

    send_snapshot(1230);

    TEST_ASSERT_EQUAL_UINT8(MSG_SNAPSHOT, decoded.h.type);
    TEST_ASSERT_EQUAL_UINT32(1234, decoded.tick);
    TEST_ASSERT_EQUAL_UINT32(1230, decoded.last_input_tick);
    TEST_ASSERT_EQUAL_UINT32(1234, client.tick);
}

/* The characters alone: the players and the enemies arrive as snapshot_apply()
 * would put them, and the tick, the items, the doors and the events of the
 * client stay as they were. */
static void test_apply_characters_puts_the_players_and_enemies_alone(void)
{
    add_players(&server, 2);
    server.players[1].x = 3.25f;
    server.players[1].hp = 40.0f;
    server.enemies.items[0].x = 7.25f;
    server.enemies.items[0].state = ESTATE_CHASE;
    server.tick = 99;
    server.items.items[0].active = 0;
    server.doors.doors[0].openness = 1.0f;
    event_push(&server.events, EV_SHOT, 0, 1.5f, 1.5f);
    snapshot_write(&snap, &server, 0);
    event_push(&client.events, EV_DOOR, 0, 4.5f, 2.5f);

    snapshot_apply_characters(&client, &snap);
    TEST_ASSERT_TRUE(client.players[1].active);
    TEST_ASSERT_EQUAL_FLOAT(3.25f, client.players[1].x);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, client.players[1].hp);
    TEST_ASSERT_EQUAL_FLOAT(7.25f, client.enemies.items[0].x);
    TEST_ASSERT_EQUAL_INT(ESTATE_CHASE, client.enemies.items[0].state);
    TEST_ASSERT_EQUAL_UINT32(0, client.tick);
    TEST_ASSERT_TRUE(client.items.items[0].active);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, client.doors.doors[0].openness);
    TEST_ASSERT_EQUAL_INT(1, client.events.count);
    TEST_ASSERT_EQUAL_INT(EV_DOOR, client.events.items[0].kind);
}

/* What decode gives back is what encode was given — padding and unused slots
 * included, which snapshot_write() leaves zero. */
static void test_decode_gives_back_what_encode_was_given(void)
{
    add_players(&server, 4);
    server.players[3].x = 3.5f;
    event_push(&server.events, EV_PICKUP, 2, 1.5f, 2.5f);

    send_snapshot(77);

    TEST_ASSERT_EQUAL_MEMORY(&snap, &decoded, sizeof(snap));
}

/* ---- what decode turns away ---- */

static const MsgSnapshot zero_snapshot;

/* Decode the first `size` bytes of wire[] and expect them refused, with
 * nothing of them left behind in `decoded`. */
static void assert_rejected(size_t size)
{
    memset(&decoded, 0xAB, sizeof(decoded));
    TEST_ASSERT_EQUAL_INT(-1, snapshot_decode(&decoded, wire, size));
    TEST_ASSERT_EQUAL_MEMORY(&zero_snapshot, &decoded, sizeof(decoded));
}

/* A fresh server world with two players, two enemies and one event, encoded
 * into wire[]: something for each test below to break. */
static size_t encode_server(void)
{
    TEST_ASSERT_EQUAL_INT(0, world_load(&server, MAP_PATH));
    add_players(&server, 2);
    event_push(&server.events, EV_SHOT, 0, 1.5f, 1.5f);
    snapshot_write(&snap, &server, 0);
    return snapshot_encode(&snap, wire);
}

static void test_decode_rejects_bytes_too_short_for_the_head(void)
{
    encode_server();
    assert_rejected(15);
    TEST_ASSERT_EQUAL_INT(-1, snapshot_decode(&decoded, NULL, 0));
}

static void test_decode_rejects_another_message_type(void)
{
    size_t size = encode_server();
    wire[offsetof(MsgSnapshot, h.type)] = MSG_INPUT;
    assert_rejected(size);
}

/* A packet a byte short or a byte long of what its counts add up to is not
 * the snapshot it says it is. */
static void test_decode_rejects_a_size_that_does_not_match_the_counts(void)
{
    size_t size = encode_server();
    assert_rejected(size - 1);
    assert_rejected(size + 1);
}

static void test_decode_rejects_a_count_past_its_limit(void)
{
    size_t size = encode_server();
    wire[offsetof(MsgSnapshot, player_count)] = MAX_PLAYERS + 1;
    assert_rejected(size);

    size = encode_server();
    wire[offsetof(MsgSnapshot, enemy_count)] = MAX_ENEMIES + 1;
    assert_rejected(size);

    size = encode_server();
    wire[offsetof(MsgSnapshot, event_count)] = MAX_EVENTS + 1;
    assert_rejected(size);
}

/* Bytes that index an array or select an enum on the client. */
static void test_decode_rejects_an_index_out_of_range(void)
{
    const size_t players = offsetof(MsgSnapshot, players);
    const size_t enemies = players + 2 * sizeof(NetPlayer);
    const size_t events = enemies + 2 * sizeof(NetEnemy) + sizeof(uint64_t) + MAX_DOORS;
    size_t size;

    size = encode_server();
    wire[players + offsetof(NetPlayer, id)] = MAX_PLAYERS;
    assert_rejected(size);

    size = encode_server();
    wire[players + offsetof(NetPlayer, weapon)] = WEAPON_COUNT;
    assert_rejected(size);

    size = encode_server();
    wire[enemies + offsetof(NetEnemy, type)] = ENEMY_COUNT;
    assert_rejected(size);

    size = encode_server();
    wire[enemies + offsetof(NetEnemy, state)] = ESTATE_DEAD + 1;
    assert_rejected(size);

    size = encode_server();
    wire[events + offsetof(NetEvent, kind)] = EV_COUNT;
    assert_rejected(size);
}

static void test_encode_refuses_a_count_past_its_limit(void)
{
    snap.h.type = MSG_SNAPSHOT;
    snap.player_count = MAX_PLAYERS + 1;
    TEST_ASSERT_EQUAL_size_t(0, snapshot_encode(&snap, wire));
}

/* ---- byte order ---- */

/* Nothing is converted on the way, which is only right on a little-endian
 * machine — the one kind the game targets. */
static void test_the_platform_is_little_endian(void)
{
    const uint16_t one = 1;
    uint8_t bytes[2];
    memcpy(bytes, &one, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT8(1, bytes[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_full_snapshot_fits_in_one_packet);
    RUN_TEST(test_a_deathmatch_snapshot_is_about_390_bytes);
    RUN_TEST(test_each_list_is_cut_to_its_count);
    RUN_TEST(test_players_arrive_where_the_server_has_them);
    RUN_TEST(test_a_free_slot_stays_free_and_ids_hold);
    RUN_TEST(test_a_player_the_snapshot_leaves_out_has_left);
    RUN_TEST(test_a_new_player_starts_from_player_init);
    RUN_TEST(test_hp_rounds_up_so_the_living_never_arrive_at_zero);
    RUN_TEST(test_points_past_a_byte_arrive_as_255);
    RUN_TEST(test_values_out_of_range_are_pinned);
    RUN_TEST(test_fields_with_nothing_behind_them_are_zero);
    RUN_TEST(test_apply_passes_over_a_player_it_cannot_index);
    RUN_TEST(test_apply_takes_its_counts_from_the_snapshot);
    RUN_TEST(test_enemies_arrive_where_the_server_has_them);
    RUN_TEST(test_enemies_the_client_lacks_are_spawned);
    RUN_TEST(test_the_items_taken_are_gone_on_the_client);
    RUN_TEST(test_doors_arrive_as_open_as_on_the_server);
    RUN_TEST(test_a_door_that_has_not_moved_keeps_its_direction);
    RUN_TEST(test_events_replace_the_client_queue);
    RUN_TEST(test_set_events_keeps_the_first_that_fit);
    RUN_TEST(test_picked_events_are_the_receivers_then_the_nearest);
    RUN_TEST(test_what_the_receiver_cannot_hear_is_not_picked);
    RUN_TEST(test_the_nearest_of_every_tick_since_the_last_snapshot_are_picked);
    RUN_TEST(test_the_ticks_travel_with_the_snapshot);
    RUN_TEST(test_apply_characters_puts_the_players_and_enemies_alone);
    RUN_TEST(test_decode_gives_back_what_encode_was_given);
    RUN_TEST(test_decode_rejects_bytes_too_short_for_the_head);
    RUN_TEST(test_decode_rejects_another_message_type);
    RUN_TEST(test_decode_rejects_a_size_that_does_not_match_the_counts);
    RUN_TEST(test_decode_rejects_a_count_past_its_limit);
    RUN_TEST(test_decode_rejects_an_index_out_of_range);
    RUN_TEST(test_encode_refuses_a_count_past_its_limit);
    RUN_TEST(test_the_platform_is_little_endian);
    return UNITY_END();
}
