/* item.c: pickups and the hp/armor/ammo limits.
 *
 * Item positions live only in the sprite — Item.x/y are never filled — so the
 * sprite list is what places a pickup on the map here.
 *
 * item.c makes no sound of its own — it appends to an EventQueue — so a
 * pickup is checked as the EV_PICKUP it posts. */

#include "unity.h"
#include "item.h"
#include "sprite.h"
#include "player.h"
#include "event.h"
#include "weapon.h"
#include <string.h>

/* item_update picks anything up within this radius. */
#define TEST_PICKUP_RADIUS 0.45f

static ItemList items;
static SpriteList sprites;
static PlayerState player;
static EventQueue events;

static int add_sprite(float x, float y, int type)
{
    Sprite *sp = &sprites.items[sprites.count];
    sp->x = x;
    sp->y = y;
    sp->type = type;
    sp->active = 1;
    sp->scale = 1.0f;
    sp->vmove = 0;
    return sprites.count++;
}

/* item.c only ever reads ammo and max_ammo out of a weapon, so the weapons are
 * filled in here instead of linking weapon.c — which would need stubs for the
 * screen-space hitscan's zBuffer and for enemy_damage(). What the defaults
 * actually are is test_weapon.c's business. */
static void give_weapons(void)
{
    memset(&player.weapons, 0, sizeof(player.weapons));
    player.weapons.weapons[WEAPON_PISTOL].type = WEAPON_PISTOL;
    player.weapons.weapons[WEAPON_PISTOL].ammo = 50;
    player.weapons.weapons[WEAPON_PISTOL].max_ammo = 50;
    player.weapons.weapons[WEAPON_SHOTGUN].type = WEAPON_SHOTGUN;
    player.weapons.weapons[WEAPON_SHOTGUN].ammo = 20;
    player.weapons.weapons[WEAPON_SHOTGUN].max_ammo = 20;
    player.weapons.current = WEAPON_PISTOL;
}

/* Place a pickup at (x, y) and return its item index. */
static int place(float x, float y, int sprite_type, ItemType type, float amount, int weapon)
{
    int sid = add_sprite(x, y, sprite_type);
    return item_add(&items, sid, type, amount, weapon);
}

void setUp(void)
{
    item_list_init(&items);
    memset(&sprites, 0, sizeof(sprites));
    memset(&player, 0, sizeof(player));
    event_queue_clear(&events);

    player.x = 5.0f;
    player.y = 5.0f;
    player.hp = 50.0f;
    player.armor = 0.0f;
    player.alive = 1;
    give_weapons();
}

void tearDown(void)
{}

static void test_item_add_starts_active_and_bound_to_its_sprite(void)
{
    int idx = place(5.0f, 5.0f, SPRITE_MEDKIT, ITEM_MEDKIT, 25.0f, 0);
    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_EQUAL_INT(1, items.count);
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(0, items.items[0].sprite_id);
    TEST_ASSERT_EQUAL_INT(ITEM_MEDKIT, items.items[0].type);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, items.items[0].amount);
}

static void test_medkit_heals_and_is_consumed(void)
{
    place(5.0f, 5.0f, SPRITE_MEDKIT, ITEM_MEDKIT, 25.0f, 0);

    TEST_ASSERT_EQUAL_INT(1, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_FLOAT(75.0f, player.hp);
    TEST_ASSERT_EQUAL_INT(0, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(0, sprites.items[0].active);

    /* nothing left to pick up */
    TEST_ASSERT_EQUAL_INT(0, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_FLOAT(75.0f, player.hp);
}

static void test_medkit_caps_hp_at_100(void)
{
    player.hp = 90.0f;
    place(5.0f, 5.0f, SPRITE_MEDKIT, ITEM_MEDKIT, 25.0f, 0);

    TEST_ASSERT_EQUAL_INT(1, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, player.hp);
}

/* A full player leaves the medkit lying there for later. */
static void test_medkit_is_left_alone_at_full_hp(void)
{
    player.hp = 100.0f;
    place(5.0f, 5.0f, SPRITE_MEDKIT, ITEM_MEDKIT, 25.0f, 0);

    TEST_ASSERT_EQUAL_INT(0, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, player.hp);
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);
    TEST_ASSERT_EQUAL_INT(1, sprites.items[0].active);
}

static void test_armor_adds_and_caps_at_100(void)
{
    player.armor = 80.0f;
    place(5.0f, 5.0f, SPRITE_ARMOR, ITEM_ARMOR, 50.0f, 0);

    TEST_ASSERT_EQUAL_INT(1, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, player.armor);
}

static void test_armor_is_left_alone_when_full(void)
{
    player.armor = 100.0f;
    place(5.0f, 5.0f, SPRITE_ARMOR, ITEM_ARMOR, 50.0f, 0);

    TEST_ASSERT_EQUAL_INT(0, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);
}

/* Ammo goes into the weapon it names, inside that same player. */
static void test_ammo_goes_to_the_named_weapon(void)
{
    player.weapons.weapons[WEAPON_SHOTGUN].ammo = 5;
    place(5.0f, 5.0f, SPRITE_AMMO, ITEM_AMMO, 8.0f, WEAPON_SHOTGUN);

    TEST_ASSERT_EQUAL_INT(1, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_INT(13, player.weapons.weapons[WEAPON_SHOTGUN].ammo);
    TEST_ASSERT_EQUAL_INT(50, player.weapons.weapons[WEAPON_PISTOL].ammo);
}

static void test_ammo_caps_at_max_ammo(void)
{
    Weapon *w = &player.weapons.weapons[WEAPON_PISTOL];
    w->ammo = w->max_ammo - 2;
    place(5.0f, 5.0f, SPRITE_AMMO, ITEM_AMMO, 30.0f, WEAPON_PISTOL);

    TEST_ASSERT_EQUAL_INT(1, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_INT(w->max_ammo, w->ammo);
}

static void test_full_ammo_is_left_alone(void)
{
    place(5.0f, 5.0f, SPRITE_AMMO, ITEM_AMMO, 10.0f, WEAPON_PISTOL);

    TEST_ASSERT_EQUAL_INT(0, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_INT(1, items.items[0].active);
}

static void test_ammo_for_an_unknown_weapon_is_not_picked_up(void)
{
    place(5.0f, 5.0f, SPRITE_AMMO, ITEM_AMMO, 10.0f, WEAPON_COUNT);
    TEST_ASSERT_EQUAL_INT(0, item_update(&items, &sprites, &player, &events));

    place(5.0f, 5.0f, SPRITE_AMMO, ITEM_AMMO, 10.0f, -1);
    TEST_ASSERT_EQUAL_INT(0, item_update(&items, &sprites, &player, &events));
}

static void test_pickup_needs_the_player_within_the_radius(void)
{
    place(5.0f + TEST_PICKUP_RADIUS + 0.05f, 5.0f, SPRITE_MEDKIT, ITEM_MEDKIT, 25.0f, 0);

    TEST_ASSERT_EQUAL_INT(0, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_FLOAT(50.0f, player.hp);

    /* one step closer and it is in reach */
    sprites.items[0].x = 5.0f + TEST_PICKUP_RADIUS - 0.05f;
    TEST_ASSERT_EQUAL_INT(1, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_FLOAT(75.0f, player.hp);
}

/* The item follows its sprite: Item.x/y are never filled in. */
static void test_the_position_comes_from_the_sprite(void)
{
    place(12.0f, 12.0f, SPRITE_MEDKIT, ITEM_MEDKIT, 25.0f, 0);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, items.items[0].x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, items.items[0].y);

    TEST_ASSERT_EQUAL_INT(0, item_update(&items, &sprites, &player, &events));

    sprites.items[0].x = player.x;
    sprites.items[0].y = player.y;
    TEST_ASSERT_EQUAL_INT(1, item_update(&items, &sprites, &player, &events));
}

/* A sprite switched off elsewhere takes its item with it. */
static void test_an_inactive_sprite_deactivates_the_item(void)
{
    place(12.0f, 12.0f, SPRITE_MEDKIT, ITEM_MEDKIT, 25.0f, 0);
    sprites.items[0].active = 0;

    TEST_ASSERT_EQUAL_INT(0, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_INT(0, items.items[0].active);
}

static void test_several_pickups_in_reach_are_all_taken(void)
{
    place(5.0f, 5.0f, SPRITE_MEDKIT, ITEM_MEDKIT, 20.0f, 0);
    place(5.1f, 5.0f, SPRITE_ARMOR, ITEM_ARMOR, 30.0f, 0);
    place(4.9f, 5.0f, SPRITE_AMMO, ITEM_AMMO, 5.0f, WEAPON_SHOTGUN);
    player.weapons.weapons[WEAPON_SHOTGUN].ammo = 0;

    TEST_ASSERT_EQUAL_INT(1, item_update(&items, &sprites, &player, &events));
    TEST_ASSERT_EQUAL_FLOAT(70.0f, player.hp);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, player.armor);
    TEST_ASSERT_EQUAL_INT(5, player.weapons.weapons[WEAPON_SHOTGUN].ammo);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_item_add_starts_active_and_bound_to_its_sprite);
    RUN_TEST(test_medkit_heals_and_is_consumed);
    RUN_TEST(test_medkit_caps_hp_at_100);
    RUN_TEST(test_medkit_is_left_alone_at_full_hp);
    RUN_TEST(test_armor_adds_and_caps_at_100);
    RUN_TEST(test_armor_is_left_alone_when_full);
    RUN_TEST(test_ammo_goes_to_the_named_weapon);
    RUN_TEST(test_ammo_caps_at_max_ammo);
    RUN_TEST(test_full_ammo_is_left_alone);
    RUN_TEST(test_ammo_for_an_unknown_weapon_is_not_picked_up);
    RUN_TEST(test_pickup_needs_the_player_within_the_radius);
    RUN_TEST(test_the_position_comes_from_the_sprite);
    RUN_TEST(test_an_inactive_sprite_deactivates_the_item);
    RUN_TEST(test_several_pickups_in_reach_are_all_taken);
    return UNITY_END();
}
