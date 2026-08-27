/* item.c: pickups, the hp/armor/ammo limits and who gets an item when several
 * players reach it at once.
 *
 * A pickup is placed by coordinates and picked up out of the players array,
 * so the sprite list takes no part in any of this — item.c does not know it
 * exists.
 *
 * item.c makes no sound of its own — it appends to an EventQueue — so a
 * pickup is checked as the EV_PICKUP it posts. */

#include "unity.h"
#include "item.h"
#include "player.h"
#include "event.h"
#include "weapon.h"
#include <string.h>

/* item_update picks anything up within this radius. */
#define TEST_PICKUP_RADIUS 0.45f

/* Enough to tell "the lowest id" from "the first one that fits". */
#define TEST_PLAYERS 3

static ItemList items;
static PlayerState players[TEST_PLAYERS];
static EventQueue events;

/* The player every single-player test uses; the rest of the array is empty
 * until a test puts someone in it. */
#define P0 (&players[0])

/* item.c only ever reads ammo and max_ammo out of a weapon, so the weapons are
 * filled in here instead of linking weapon.c — which would need stubs for the
 * world raycast and for enemy_damage(). What the defaults actually are is
 * test_weapon.c's business. */
static void give_weapons(PlayerState *p)
{
    memset(&p->weapons, 0, sizeof(p->weapons));
    p->weapons.weapons[WEAPON_PISTOL].type = WEAPON_PISTOL;
    p->weapons.weapons[WEAPON_PISTOL].ammo = 50;
    p->weapons.weapons[WEAPON_PISTOL].max_ammo = 50;
    p->weapons.weapons[WEAPON_SHOTGUN].type = WEAPON_SHOTGUN;
    p->weapons.weapons[WEAPON_SHOTGUN].ammo = 20;
    p->weapons.weapons[WEAPON_SHOTGUN].max_ammo = 20;
    p->weapons.current = WEAPON_PISTOL;
}

/* Put player `id` into the world at (x, y), hurt enough to want a medkit. */
static PlayerState *spawn(int id, float x, float y)
{
    PlayerState *p = &players[id];
    memset(p, 0, sizeof(*p));
    p->id = (uint8_t)id;
    p->active = 1;
    p->alive = 1;
    p->x = x;
    p->y = y;
    p->hp = 50.0f;
    p->armor = 0.0f;
    give_weapons(p);
    return p;
}

/* Place a pickup at (x, y) and return its item index. */
static int place(float x, float y, ItemType type, float amount, int weapon)
{
    return item_add(&items, x, y, type, amount, weapon);
}

static void update(void)
{
    event_queue_clear(&events);
    item_update(&items, players, TEST_PLAYERS, &events);
}

/* item_update() reports what it did through the queue and nowhere else. */
static int pickup_count(void)
{
    int n = 0;
    for (int i = 0; i < events.count; i++) {
        if (events.items[i].kind == EV_PICKUP) {
            n++;
        }
    }
    return n;
}

static const GameEvent *first_pickup(void)
{
    for (int i = 0; i < events.count; i++) {
        if (events.items[i].kind == EV_PICKUP) {
            return &events.items[i];
        }
    }
    return NULL;
}

void setUp(void)
{
    item_list_init(&items);
    memset(players, 0, sizeof(players));
    event_queue_clear(&events);
    spawn(0, 5.0f, 5.0f);
}

void tearDown(void)
{}

static void test_item_add_places_the_pickup_where_it_is_told(void)
{
    int idx = place(7.5f, 3.25f, ITEM_MEDKIT, 25.0f, 0);
    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_EQUAL_INT(1, items.count);
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(ITEM_MEDKIT, items.items[0].type);
    TEST_ASSERT_EQUAL_FLOAT(7.5f, items.items[0].x);
    TEST_ASSERT_EQUAL_FLOAT(3.25f, items.items[0].y);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, items.items[0].amount);
}

static void test_medkit_heals_and_is_consumed(void)
{
    place(5.0f, 5.0f, ITEM_MEDKIT, 25.0f, 0);

    update();
    TEST_ASSERT_EQUAL_FLOAT(75.0f, P0->hp);
    TEST_ASSERT_EQUAL_INT(0, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(1, pickup_count());

    /* nothing left to pick up */
    update();
    TEST_ASSERT_EQUAL_FLOAT(75.0f, P0->hp);
    TEST_ASSERT_EQUAL_INT(0, pickup_count());
}

/* EV_PICKUP names the player who took it and where it lay: the client scales
 * the sound by the distance from its own player to that point. */
static void test_the_pickup_event_carries_the_player_and_the_item(void)
{
    spawn(1, 8.0f, 2.0f);
    place(8.25f, 2.0f, ITEM_MEDKIT, 25.0f, 0);

    update();
    const GameEvent *e = first_pickup();
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_INT(1, e->actor);
    TEST_ASSERT_EQUAL_FLOAT(8.25f, e->x);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, e->y);
}

static void test_medkit_caps_hp_at_100(void)
{
    P0->hp = 90.0f;
    place(5.0f, 5.0f, ITEM_MEDKIT, 25.0f, 0);

    update();
    TEST_ASSERT_EQUAL_FLOAT(100.0f, P0->hp);
}

/* A full player leaves the medkit lying there for later. */
static void test_medkit_is_left_alone_at_full_hp(void)
{
    P0->hp = 100.0f;
    place(5.0f, 5.0f, ITEM_MEDKIT, 25.0f, 0);

    update();
    TEST_ASSERT_EQUAL_FLOAT(100.0f, P0->hp);
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(0, pickup_count());
}

static void test_armor_adds_and_caps_at_100(void)
{
    P0->armor = 80.0f;
    place(5.0f, 5.0f, ITEM_ARMOR, 50.0f, 0);

    update();
    TEST_ASSERT_EQUAL_FLOAT(100.0f, P0->armor);
    TEST_ASSERT_EQUAL_INT(1, pickup_count());
}

static void test_armor_is_left_alone_when_full(void)
{
    P0->armor = 100.0f;
    place(5.0f, 5.0f, ITEM_ARMOR, 50.0f, 0);

    update();
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(0, pickup_count());
}

/* Ammo goes into the weapon it names, inside that same player. */
static void test_ammo_goes_to_the_named_weapon(void)
{
    P0->weapons.weapons[WEAPON_SHOTGUN].ammo = 5;
    place(5.0f, 5.0f, ITEM_AMMO, 8.0f, WEAPON_SHOTGUN);

    update();
    TEST_ASSERT_EQUAL_INT(13, P0->weapons.weapons[WEAPON_SHOTGUN].ammo);
    TEST_ASSERT_EQUAL_INT(50, P0->weapons.weapons[WEAPON_PISTOL].ammo);
}

static void test_ammo_caps_at_max_ammo(void)
{
    Weapon *w = &P0->weapons.weapons[WEAPON_PISTOL];
    w->ammo = w->max_ammo - 2;
    place(5.0f, 5.0f, ITEM_AMMO, 30.0f, WEAPON_PISTOL);

    update();
    TEST_ASSERT_EQUAL_INT(w->max_ammo, w->ammo);
}

static void test_full_ammo_is_left_alone(void)
{
    place(5.0f, 5.0f, ITEM_AMMO, 10.0f, WEAPON_PISTOL);

    update();
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(0, pickup_count());
}

static void test_ammo_for_an_unknown_weapon_is_not_picked_up(void)
{
    place(5.0f, 5.0f, ITEM_AMMO, 10.0f, WEAPON_COUNT);
    place(5.0f, 5.0f, ITEM_AMMO, 10.0f, -1);

    update();
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(1, items.items[1].active);
    TEST_ASSERT_EQUAL_INT(0, pickup_count());
}

static void test_pickup_needs_the_player_within_the_radius(void)
{
    place(5.0f + TEST_PICKUP_RADIUS + 0.05f, 5.0f, ITEM_MEDKIT, 25.0f, 0);

    update();
    TEST_ASSERT_EQUAL_FLOAT(50.0f, P0->hp);
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);

    /* one step closer and it is in reach */
    P0->x = 5.0f + 0.1f;
    update();
    TEST_ASSERT_EQUAL_FLOAT(75.0f, P0->hp);
}

static void test_several_pickups_in_reach_are_all_taken(void)
{
    place(5.0f, 5.0f, ITEM_MEDKIT, 20.0f, 0);
    place(5.1f, 5.0f, ITEM_ARMOR, 30.0f, 0);
    place(4.9f, 5.0f, ITEM_AMMO, 5.0f, WEAPON_SHOTGUN);
    P0->weapons.weapons[WEAPON_SHOTGUN].ammo = 0;

    update();
    TEST_ASSERT_EQUAL_FLOAT(70.0f, P0->hp);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, P0->armor);
    TEST_ASSERT_EQUAL_INT(5, P0->weapons.weapons[WEAPON_SHOTGUN].ammo);
    TEST_ASSERT_EQUAL_INT(3, pickup_count());
}

/* --- Several players --- */

/* Two players standing on one medkit: exactly one of them gets it, and which
 * one may not depend on anything but the id — the server and every client
 * predicting the tick have to come out the same. */
static void test_an_item_reached_at_once_goes_to_the_lowest_id(void)
{
    spawn(1, 5.0f, 5.0f);
    spawn(2, 5.0f, 5.0f);
    place(5.0f, 5.0f, ITEM_MEDKIT, 25.0f, 0);

    update();
    TEST_ASSERT_EQUAL_INT(0, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(1, pickup_count());
    TEST_ASSERT_EQUAL_INT(0, first_pickup()->actor);
    TEST_ASSERT_EQUAL_FLOAT(75.0f, players[0].hp);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, players[1].hp);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, players[2].hp);
}

/* Being first in line is not the same as taking it: a player at full health
 * walks over the medkit and the one behind them picks it up. */
static void test_a_player_who_cannot_use_it_leaves_it_to_the_next(void)
{
    P0->hp = 100.0f;
    spawn(1, 5.0f, 5.0f);
    place(5.0f, 5.0f, ITEM_MEDKIT, 25.0f, 0);

    update();
    TEST_ASSERT_EQUAL_INT(0, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(1, first_pickup()->actor);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, players[0].hp);
    TEST_ASSERT_EQUAL_FLOAT(75.0f, players[1].hp);
}

/* Every item is offered to every player, so two of them standing apart each
 * take the one they are standing on. */
static void test_each_player_takes_the_item_they_stand_on(void)
{
    spawn(2, 12.0f, 9.0f);
    place(5.0f, 5.0f, ITEM_MEDKIT, 25.0f, 0);
    place(12.0f, 9.0f, ITEM_ARMOR, 30.0f, 0);

    update();
    TEST_ASSERT_EQUAL_INT(2, pickup_count());
    TEST_ASSERT_EQUAL_FLOAT(75.0f, players[0].hp);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, players[0].armor);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, players[2].hp);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, players[2].armor);
}

/* A free slot and a corpse are both standing on the item as far as the array
 * is concerned, and neither may take it. */
static void test_a_player_who_is_not_in_the_world_or_not_alive_takes_nothing(void)
{
    spawn(1, 5.0f, 5.0f);
    players[1].active = 0;
    spawn(2, 5.0f, 5.0f);
    players[2].alive = 0;
    P0->hp = 100.0f; /* the only live player has no use for it */
    place(5.0f, 5.0f, ITEM_MEDKIT, 25.0f, 0);

    update();
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(0, pickup_count());
    TEST_ASSERT_EQUAL_FLOAT(50.0f, players[1].hp);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, players[2].hp);
}

/* Dead by hp rather than by flag: nothing has set alive to 0 yet on the tick
 * the shot landed. */
static void test_a_player_at_zero_hp_takes_nothing(void)
{
    P0->hp = 0.0f;
    place(5.0f, 5.0f, ITEM_MEDKIT, 25.0f, 0);

    update();
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, P0->hp);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_item_add_places_the_pickup_where_it_is_told);
    RUN_TEST(test_medkit_heals_and_is_consumed);
    RUN_TEST(test_the_pickup_event_carries_the_player_and_the_item);
    RUN_TEST(test_medkit_caps_hp_at_100);
    RUN_TEST(test_medkit_is_left_alone_at_full_hp);
    RUN_TEST(test_armor_adds_and_caps_at_100);
    RUN_TEST(test_armor_is_left_alone_when_full);
    RUN_TEST(test_ammo_goes_to_the_named_weapon);
    RUN_TEST(test_ammo_caps_at_max_ammo);
    RUN_TEST(test_full_ammo_is_left_alone);
    RUN_TEST(test_ammo_for_an_unknown_weapon_is_not_picked_up);
    RUN_TEST(test_pickup_needs_the_player_within_the_radius);
    RUN_TEST(test_several_pickups_in_reach_are_all_taken);
    RUN_TEST(test_an_item_reached_at_once_goes_to_the_lowest_id);
    RUN_TEST(test_a_player_who_cannot_use_it_leaves_it_to_the_next);
    RUN_TEST(test_each_player_takes_the_item_they_stand_on);
    RUN_TEST(test_a_player_who_is_not_in_the_world_or_not_alive_takes_nothing);
    RUN_TEST(test_a_player_at_zero_hp_takes_nothing);
    return UNITY_END();
}
