/* weapon.c: the weapon table, switching and per-tick cooldown/animation decay.
 *
 * The screen-space hitscan is out of scope — it is what task 05-04 replaces
 * with a world raycast, and it only works once a frame has been rendered. It
 * still has to link, though: weapon.c references the renderer's zBuffer,
 * enemy_damage() and audio_play_volume(), so all three are stubbed here rather
 * than dragging raycast.c, enemy.c and audio.c into this test. */

#include "unity.h"
#include "weapon.h"
#include "player.h"
#include "enemy.h"
#include "sprite.h"
#include "audio.h"
#include "raycast.h"
#include "utils.h"
#include <string.h>

#define TEST_STEP (1.0 / 60.0)

static PlayerState player;

/* ---- stubs ---- */

float zBuffer[SCREEN_W];

void enemy_damage(EnemyList *el, SpriteList *sl, int idx, float dmg, Audio *au, float px, float py)
{
    (void)el;
    (void)sl;
    (void)idx;
    (void)dmg;
    (void)au;
    (void)px;
    (void)py;
}

void audio_play_volume(Audio *a, SoundId id, float vol)
{
    (void)a;
    (void)id;
    (void)vol;
}

/* ---- fixture ---- */

void setUp(void)
{
    memset(&player, 0, sizeof(player));
    weapon_system_init(&player.weapons);
}

void tearDown(void)
{}

static void test_weapon_system_init_fills_both_weapons(void)
{
    const Weapon *pistol = &player.weapons.weapons[WEAPON_PISTOL];
    const Weapon *shotgun = &player.weapons.weapons[WEAPON_SHOTGUN];

    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, pistol->type);
    TEST_ASSERT_EQUAL_INT(50, pistol->ammo);
    TEST_ASSERT_EQUAL_INT(50, pistol->max_ammo);
    TEST_ASSERT_EQUAL_INT(1, pistol->pellets);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pistol->spread);

    TEST_ASSERT_EQUAL_INT(WEAPON_SHOTGUN, shotgun->type);
    TEST_ASSERT_EQUAL_INT(20, shotgun->ammo);
    TEST_ASSERT_EQUAL_INT(20, shotgun->max_ammo);
    TEST_ASSERT_TRUE(shotgun->pellets > 1);
    TEST_ASSERT_TRUE(shotgun->spread > 0.0f);

    /* the shotgun trades rate of fire for a heavier volley */
    TEST_ASSERT_TRUE(shotgun->fire_cd > pistol->fire_cd);
    TEST_ASSERT_TRUE(shotgun->damage * shotgun->pellets > pistol->damage);
}

static void test_weapon_system_init_starts_ready_with_the_pistol(void)
{
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, player.weapons.current);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, player.weapons.fire_button);
    for (int i = 0; i < WEAPON_COUNT; i++) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, player.weapons.weapons[i].cooldown);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, player.weapons.weapons[i].anim);
    }
}

static void test_weapon_switch_selects_by_index(void)
{
    weapon_switch(&player, WEAPON_SHOTGUN);
    TEST_ASSERT_EQUAL_INT(WEAPON_SHOTGUN, player.weapons.current);

    weapon_switch(&player, WEAPON_PISTOL);
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, player.weapons.current);
}

static void test_weapon_switch_ignores_an_index_outside_the_table(void)
{
    weapon_switch(&player, -1);
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, player.weapons.current);

    weapon_switch(&player, WEAPON_COUNT);
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, player.weapons.current);
}

/* Re-selecting the weapon already in hand changes nothing. */
static void test_weapon_switch_to_the_current_weapon_is_a_no_op(void)
{
    player.weapons.weapons[WEAPON_PISTOL].cooldown = 0.3f;
    weapon_switch(&player, WEAPON_PISTOL);
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, player.weapons.current);
    TEST_ASSERT_EQUAL_FLOAT(0.3f, player.weapons.weapons[WEAPON_PISTOL].cooldown);
}

static void test_weapon_update_runs_down_the_cooldown(void)
{
    Weapon *w = &player.weapons.weapons[WEAPON_PISTOL];
    w->cooldown = w->fire_cd;

    weapon_update(&player, TEST_STEP);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, w->fire_cd - (float)TEST_STEP, w->cooldown);

    for (int i = 0; i < 120; i++) {
        weapon_update(&player, TEST_STEP);
    }
    TEST_ASSERT_TRUE(w->cooldown <= 0.0f);
}

/* Both weapons cool down, not just the one in hand. */
static void test_weapon_update_ticks_every_weapon(void)
{
    for (int i = 0; i < WEAPON_COUNT; i++) {
        player.weapons.weapons[i].cooldown = 1.0f;
    }
    weapon_switch(&player, WEAPON_PISTOL);

    weapon_update(&player, TEST_STEP);
    for (int i = 0; i < WEAPON_COUNT; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f - (float)TEST_STEP,
                                 player.weapons.weapons[i].cooldown);
    }
}

/* The animation runs four times faster than real time and stops at zero. */
static void test_weapon_update_decays_the_animation_to_zero(void)
{
    Weapon *w = &player.weapons.weapons[WEAPON_PISTOL];
    w->anim = 1.0f;

    weapon_update(&player, TEST_STEP);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f - 4.0f * (float)TEST_STEP, w->anim);

    for (int i = 0; i < 60; i++) {
        weapon_update(&player, TEST_STEP);
    }
    TEST_ASSERT_EQUAL_FLOAT(0.0f, w->anim);

    /* and it does not go negative once it is there */
    weapon_update(&player, TEST_STEP);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, w->anim);
}

/* Ammo, cooldowns and selection are per player, not global. */
static void test_two_players_keep_separate_weapon_state(void)
{
    PlayerState other;
    memset(&other, 0, sizeof(other));
    weapon_system_init(&other.weapons);

    weapon_switch(&other, WEAPON_SHOTGUN);
    other.weapons.weapons[WEAPON_PISTOL].ammo = 3;

    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, player.weapons.current);
    TEST_ASSERT_EQUAL_INT(50, player.weapons.weapons[WEAPON_PISTOL].ammo);
    TEST_ASSERT_EQUAL_INT(WEAPON_SHOTGUN, other.weapons.current);
    TEST_ASSERT_EQUAL_INT(3, other.weapons.weapons[WEAPON_PISTOL].ammo);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_weapon_system_init_fills_both_weapons);
    RUN_TEST(test_weapon_system_init_starts_ready_with_the_pistol);
    RUN_TEST(test_weapon_switch_selects_by_index);
    RUN_TEST(test_weapon_switch_ignores_an_index_outside_the_table);
    RUN_TEST(test_weapon_switch_to_the_current_weapon_is_a_no_op);
    RUN_TEST(test_weapon_update_runs_down_the_cooldown);
    RUN_TEST(test_weapon_update_ticks_every_weapon);
    RUN_TEST(test_weapon_update_decays_the_animation_to_zero);
    RUN_TEST(test_two_players_keep_separate_weapon_state);
    return UNITY_END();
}
