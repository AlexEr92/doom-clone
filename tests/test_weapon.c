/* weapon.c: the weapon table and who owns what in it, switching, per-tick
 * cooldown/animation decay and the world hitscan.
 *
 * Firing no longer needs a rendered frame, so the trace it uses is linked for
 * real: raycast_world.c against a map and a door built in memory, enemy.c for
 * the damage it applies and player.c for the PvP half of it. Nothing here
 * touches the renderer, so sprite.c is not linked and nothing is stubbed.
 *
 * Firing makes no sound of its own either: weapon.c posts EV_SHOT / EV_NO_AMMO
 * into an EventQueue and the client plays them. */

#include "unity.h"
#include "weapon.h"
#include "player.h"
#include "enemy.h"
#include "event.h"
#include "door.h"
#include "map.h"
#include "raycast_world.h"
#include <math.h>
#include <string.h>

#define TEST_STEP (1.0 / 60.0)

/* Cells the fixture map blocks, each on a row of its own so a shot fired
 * along that row meets exactly one of them. */
#define BLOCK_X 6
#define WALL_ROW 4
#define DOOR_ROW 6

/* Lateral offsets from the ray, either side of the enemy hit radius. */
#define INSIDE_OFFSET (ENEMY_HIT_RADIUS * 0.6f)
#define OUTSIDE_OFFSET (ENEMY_HIT_RADIUS * 1.6f)

#define SHOOTER 0
#define VICTIM 1

static Map map;
static DoorList doors;
static EnemyList enemies;
static PlayerState players[2];
/* Filled by weapon.c and enemy.c; cleared once per test. */
static EventQueue events;

/* ---- fixture ---- */

/* 20x12, walled around the edge and otherwise empty, with one wall cell and
 * one door cell dropped into rows of their own. */
static void build_map(Map *m)
{
    memset(m, 0, sizeof(*m));
    m->w = 20;
    m->h = 12;
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            int edge = (x == 0 || y == 0 || x == m->w - 1 || y == m->h - 1);
            m->cells[y][x] = edge ? 1 : 0;
        }
    }
    m->cells[WALL_ROW][BLOCK_X] = 1;
    m->cells[DOOR_ROW][BLOCK_X] = 2;
}

void setUp(void)
{
    build_map(&map);
    door_list_init(&doors);
    door_discover(&doors, &map);
    enemy_list_init(&enemies);

    player_init(&players[SHOOTER], 2, 2);
    player_init(&players[VICTIM], 17, 10);
    players[VICTIM].id = VICTIM;
    event_queue_clear(&events);
}

void tearDown(void)
{}

static void aim(PlayerState *p, float x, float y, float angle)
{
    p->x = x;
    p->y = y;
    p->angle = angle;
}

/* Every scenario fires straight down +X (angle 0) from the left of the map. */
static void fire(void)
{
    weapon_try_fire(&players[SHOOTER], &map, &doors, players, 2, &enemies, &events);
}

static int add_enemy(float x, float y)
{
    return enemy_spawn(&enemies, x, y, ENEMY_IMP);
}

static float imp_max_hp(void)
{
    return enemy_def(ENEMY_IMP)->max_hp;
}

static float pistol_damage(void)
{
    return players[SHOOTER].weapons.weapons[WEAPON_PISTOL].damage;
}

/* What picking a weapon up will do (task 10-06): own it, with a full load. */
static void give(PlayerState *p, int weapon)
{
    Weapon *w = &p->weapons.weapons[weapon];
    w->owned = 1;
    w->ammo = w->max_ammo;
}

/* Every weapon but the grenade launcher, which fires nothing before 10-07. */
static int is_hitscan(int weapon)
{
    return weapon != WEAPON_GRENADE;
}

static void test_weapon_system_init_fills_every_weapon(void)
{
    const Weapon *ws = players[SHOOTER].weapons.weapons;
    const Weapon *pistol = &ws[WEAPON_PISTOL];
    const Weapon *shotgun = &ws[WEAPON_SHOTGUN];
    const Weapon *ak47 = &ws[WEAPON_AK47];
    const Weapon *rifle = &ws[WEAPON_RIFLE];
    const Weapon *grenade = &ws[WEAPON_GRENADE];

    for (int i = 0; i < WEAPON_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(i, ws[i].type);
        TEST_ASSERT_TRUE(ws[i].max_ammo > 0);
        TEST_ASSERT_TRUE(ws[i].damage > 0.0f);
        TEST_ASSERT_TRUE(ws[i].pellets >= 1);
        TEST_ASSERT_TRUE(ws[i].fire_cd > 0.0f);
    }

    TEST_ASSERT_EQUAL_INT(50, pistol->max_ammo);
    TEST_ASSERT_EQUAL_INT(1, pistol->pellets);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pistol->spread);

    TEST_ASSERT_EQUAL_INT(20, shotgun->max_ammo);
    TEST_ASSERT_TRUE(shotgun->pellets > 1);
    TEST_ASSERT_TRUE(shotgun->spread > 0.0f);
    /* the shotgun trades rate of fire for a heavier volley */
    TEST_ASSERT_TRUE(shotgun->fire_cd > pistol->fire_cd);
    TEST_ASSERT_TRUE(shotgun->damage * shotgun->pellets > pistol->damage);

    /* the automatic: fast, light, and it wanders */
    TEST_ASSERT_TRUE(ak47->fire_cd < pistol->fire_cd);
    TEST_ASSERT_TRUE(ak47->damage < pistol->damage);
    TEST_ASSERT_TRUE(ak47->spread > 0.0f);

    /* the rifle: slow, heavy, and next to no spread */
    TEST_ASSERT_TRUE(rifle->fire_cd > pistol->fire_cd);
    TEST_ASSERT_TRUE(rifle->damage > pistol->damage);
    TEST_ASSERT_TRUE(rifle->spread < ak47->spread);

    /* the grenade launcher: the slowest of all, and the fewest rounds */
    for (int i = 0; i < WEAPON_COUNT; i++) {
        if (i != WEAPON_GRENADE) {
            TEST_ASSERT_TRUE(grenade->fire_cd > ws[i].fire_cd);
            TEST_ASSERT_TRUE(grenade->max_ammo < ws[i].max_ammo);
        }
    }
}

/* A deathmatch starts with the pistol, loaded; the rest is on the map. */
static void test_weapon_system_init_owns_the_pistol_alone(void)
{
    const Weapon *ws = players[SHOOTER].weapons.weapons;
    TEST_ASSERT_EQUAL_INT(1, ws[WEAPON_PISTOL].owned);
    TEST_ASSERT_EQUAL_INT(ws[WEAPON_PISTOL].max_ammo, ws[WEAPON_PISTOL].ammo);
    for (int i = 0; i < WEAPON_COUNT; i++) {
        if (i != WEAPON_PISTOL) {
            TEST_ASSERT_EQUAL_INT(0, ws[i].owned);
            TEST_ASSERT_EQUAL_INT(0, ws[i].ammo);
        }
    }
}

static void test_weapon_system_init_starts_ready_with_the_pistol(void)
{
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, players[SHOOTER].weapons.current);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, players[SHOOTER].weapons.fire_button);
    for (int i = 0; i < WEAPON_COUNT; i++) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, players[SHOOTER].weapons.weapons[i].cooldown);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, players[SHOOTER].weapons.weapons[i].anim);
    }
}

static void test_weapon_switch_selects_by_index(void)
{
    for (int i = 0; i < WEAPON_COUNT; i++) {
        give(&players[SHOOTER], i);
    }
    for (int i = WEAPON_COUNT - 1; i >= 0; i--) {
        weapon_switch(&players[SHOOTER], i);
        TEST_ASSERT_EQUAL_INT(i, players[SHOOTER].weapons.current);
    }
}

/* A weapon not picked up is no choice: the one in hand stays. */
static void test_weapon_switch_refuses_a_weapon_not_owned(void)
{
    for (int i = 0; i < WEAPON_COUNT; i++) {
        weapon_switch(&players[SHOOTER], i);
        TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, players[SHOOTER].weapons.current);
    }

    give(&players[SHOOTER], WEAPON_RIFLE);
    weapon_switch(&players[SHOOTER], WEAPON_RIFLE);
    TEST_ASSERT_EQUAL_INT(WEAPON_RIFLE, players[SHOOTER].weapons.current);
    weapon_switch(&players[SHOOTER], WEAPON_AK47);
    TEST_ASSERT_EQUAL_INT(WEAPON_RIFLE, players[SHOOTER].weapons.current);
}

static void test_weapon_switch_ignores_an_index_outside_the_table(void)
{
    weapon_switch(&players[SHOOTER], -1);
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, players[SHOOTER].weapons.current);

    weapon_switch(&players[SHOOTER], WEAPON_COUNT);
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, players[SHOOTER].weapons.current);
}

/* Re-selecting the weapon already in hand changes nothing. */
static void test_weapon_switch_to_the_current_weapon_is_a_no_op(void)
{
    players[SHOOTER].weapons.weapons[WEAPON_PISTOL].cooldown = 0.3f;
    weapon_switch(&players[SHOOTER], WEAPON_PISTOL);
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, players[SHOOTER].weapons.current);
    TEST_ASSERT_EQUAL_FLOAT(0.3f, players[SHOOTER].weapons.weapons[WEAPON_PISTOL].cooldown);
}

static void test_weapon_update_runs_down_the_cooldown(void)
{
    Weapon *w = &players[SHOOTER].weapons.weapons[WEAPON_PISTOL];
    w->cooldown = w->fire_cd;

    weapon_update(&players[SHOOTER], TEST_STEP);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, w->fire_cd - (float)TEST_STEP, w->cooldown);

    for (int i = 0; i < 120; i++) {
        weapon_update(&players[SHOOTER], TEST_STEP);
    }
    TEST_ASSERT_TRUE(w->cooldown <= 0.0f);
}

/* Every weapon cools down, not just the one in hand. */
static void test_weapon_update_ticks_every_weapon(void)
{
    for (int i = 0; i < WEAPON_COUNT; i++) {
        players[SHOOTER].weapons.weapons[i].cooldown = 1.0f;
    }
    weapon_switch(&players[SHOOTER], WEAPON_PISTOL);

    weapon_update(&players[SHOOTER], TEST_STEP);
    for (int i = 0; i < WEAPON_COUNT; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f - (float)TEST_STEP,
                                 players[SHOOTER].weapons.weapons[i].cooldown);
    }
}

/* The animation runs from 1 to 0 over WEAPON_SHOT_TIME and stops at zero. */
static void test_weapon_update_decays_the_animation_to_zero(void)
{
    Weapon *w = &players[SHOOTER].weapons.weapons[WEAPON_PISTOL];
    w->anim = 1.0f;

    weapon_update(&players[SHOOTER], TEST_STEP);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f - (float)TEST_STEP / WEAPON_SHOT_TIME, w->anim);

    for (int i = 0; i < 60; i++) {
        weapon_update(&players[SHOOTER], TEST_STEP);
    }
    TEST_ASSERT_EQUAL_FLOAT(0.0f, w->anim);

    /* and it does not go negative once it is there */
    weapon_update(&players[SHOOTER], TEST_STEP);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, w->anim);
}

/* A shot shows for WEAPON_SHOT_TIME whatever the weapon: a flash is a flash,
 * and the shotgun's long cooldown must not play it in slow motion. Counted in
 * steps of float, the end may land a tick off. */
static void test_shot_animation_is_the_same_for_every_weapon(void)
{
    for (int i = 0; i < WEAPON_COUNT; i++) {
        if (!is_hitscan(i)) {
            continue;
        }
        player_init(&players[SHOOTER], 2, 2);
        give(&players[SHOOTER], i);
        weapon_switch(&players[SHOOTER], i);
        aim(&players[SHOOTER], 2.5f, 2.5f, 0.0f);
        fire();
        const Weapon *w = &players[SHOOTER].weapons.weapons[i];
        TEST_ASSERT_EQUAL_FLOAT(1.0f, w->anim);

        int shown = 0;
        for (int t = 1; t <= 200 && w->anim > 0.0f; t++) {
            weapon_update(&players[SHOOTER], TEST_STEP);
            if (w->anim > 0.0f) {
                shown = t;
            }
        }
        TEST_ASSERT_INT_WITHIN(1, (int)roundf(WEAPON_SHOT_TIME / (float)TEST_STEP), shown);
    }
}

/* Ammo, cooldowns and selection are per player, not global. */
static void test_two_players_keep_separate_weapon_state(void)
{
    give(&players[VICTIM], WEAPON_SHOTGUN);
    weapon_switch(&players[VICTIM], WEAPON_SHOTGUN);
    players[VICTIM].weapons.weapons[WEAPON_PISTOL].ammo = 3;

    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, players[SHOOTER].weapons.current);
    TEST_ASSERT_EQUAL_INT(50, players[SHOOTER].weapons.weapons[WEAPON_PISTOL].ammo);
    TEST_ASSERT_EQUAL_INT(0, players[SHOOTER].weapons.weapons[WEAPON_SHOTGUN].owned);
    TEST_ASSERT_EQUAL_INT(WEAPON_SHOTGUN, players[VICTIM].weapons.current);
    TEST_ASSERT_EQUAL_INT(3, players[VICTIM].weapons.weapons[WEAPON_PISTOL].ammo);
}

/* ---- hitscan ---- */

static void test_a_shot_damages_an_enemy_in_the_open(void)
{
    aim(&players[SHOOTER], 2.5f, 2.5f, 0.0f);
    int e = add_enemy(6.5f, 2.5f);

    fire();

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp() - pistol_damage(), enemies.items[e].hp);
    TEST_ASSERT_EQUAL_INT(49, players[SHOOTER].weapons.weapons[WEAPON_PISTOL].ammo);
    TEST_ASSERT_TRUE(players[SHOOTER].weapons.weapons[WEAPON_PISTOL].cooldown > 0.0f);
}

static int count_events(GameEventKind kind)
{
    int n = 0;
    for (int i = 0; i < events.count; i++) {
        if (events.items[i].kind == kind) {
            n++;
        }
    }
    return n;
}

/* Every pellet of a blast at point-blank hurts the enemy, and the tick still
 * has one shot and one pain for the client to play, not seven pains. */
static void test_a_point_blank_blast_is_one_shot_and_one_pain(void)
{
    aim(&players[SHOOTER], 2.5f, 2.5f, 0.0f);
    give(&players[SHOOTER], WEAPON_SHOTGUN);
    weapon_switch(&players[SHOOTER], WEAPON_SHOTGUN);
    const Weapon *w = &players[SHOOTER].weapons.weapons[WEAPON_SHOTGUN];
    int e = add_enemy(3.5f, 2.5f);
    enemies.items[e].hp = 1000.0f; /* survives the blast: hurt, not killed */

    fire();

    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1000.0f - w->damage * (float)w->pellets, enemies.items[e].hp);
    TEST_ASSERT_EQUAL_INT(2, events.count);
    TEST_ASSERT_EQUAL_INT(1, count_events(EV_SHOT));
    TEST_ASSERT_EQUAL_INT(1, count_events(EV_ENEMY_HURT));
}

/* Ten players fire their shotguns in one tick, each into an enemy of its
 * own: seventy pellets hit, and the tick's queue still holds every shot. */
static void test_ten_blasts_in_one_tick_fit_the_queue(void)
{
    static PlayerState shooters[MAX_PLAYERS];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerState *p = &shooters[i];
        player_init(p, 2, 1 + i);
        p->id = (uint8_t)i;
        p->angle = 0.0f;
        give(p, WEAPON_SHOTGUN);
        weapon_switch(p, WEAPON_SHOTGUN);
        int e = add_enemy(3.5f, p->y);
        enemies.items[e].hp = 1000.0f;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        weapon_try_fire(&shooters[i], &map, &doors, shooters, MAX_PLAYERS, &enemies, &events);
    }

    TEST_ASSERT_EQUAL_INT(MAX_PLAYERS, count_events(EV_SHOT));
    TEST_ASSERT_EQUAL_INT(MAX_PLAYERS, count_events(EV_ENEMY_HURT));
    TEST_ASSERT_EQUAL_INT(2 * MAX_PLAYERS, events.count);
}

/* Every weapon that fires a ray hurts what it hits at point-blank, where
 * spread cannot carry a pellet off the target, by its damage per pellet. */
static void test_every_hitscan_weapon_damages_what_it_hits(void)
{
    for (int i = 0; i < WEAPON_COUNT; i++) {
        if (!is_hitscan(i)) {
            continue;
        }
        enemy_list_init(&enemies);
        player_init(&players[SHOOTER], 2, 2);
        give(&players[SHOOTER], i);
        weapon_switch(&players[SHOOTER], i);
        aim(&players[SHOOTER], 2.5f, 2.5f, 0.0f);
        int e = add_enemy(3.5f, 2.5f);
        enemies.items[e].hp = 1000.0f;
        const Weapon *w = &players[SHOOTER].weapons.weapons[i];

        fire();

        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1000.0f - w->damage * (float)w->pellets,
                                 enemies.items[e].hp);
    }
}

/* Each weapon spends its own rounds: a shot from one leaves the rest alone. */
static void test_each_weapon_spends_its_own_ammo(void)
{
    PlayerState *p = &players[SHOOTER];
    for (int i = 0; i < WEAPON_COUNT; i++) {
        give(p, i);
    }
    aim(p, 2.5f, 2.5f, 0.0f);
    for (int i = 0; i < WEAPON_COUNT; i++) {
        if (!is_hitscan(i)) {
            continue;
        }
        weapon_switch(p, i);
        fire();
        for (int k = 0; k < WEAPON_COUNT; k++) {
            const Weapon *w = &p->weapons.weapons[k];
            int fired = is_hitscan(k) && k <= i;
            TEST_ASSERT_EQUAL_INT(w->max_ammo - fired, w->ammo);
        }
    }
}

/* The grenade launcher is there to be selected and seen; its projectile is
 * task 10-07. Until then the trigger does nothing: no round, no cooldown, no
 * flash, no sound, no damage — not even the click of an empty weapon. */
static void test_the_grenade_launcher_fires_nothing_yet(void)
{
    PlayerState *p = &players[SHOOTER];
    give(p, WEAPON_GRENADE);
    weapon_switch(p, WEAPON_GRENADE);
    TEST_ASSERT_EQUAL_INT(WEAPON_GRENADE, p->weapons.current);
    aim(p, 2.5f, 2.5f, 0.0f);
    int e = add_enemy(3.5f, 2.5f);
    const Weapon *w = &p->weapons.weapons[WEAPON_GRENADE];

    fire();
    TEST_ASSERT_EQUAL_INT(w->max_ammo, w->ammo);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, w->cooldown);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, w->anim);
    TEST_ASSERT_EQUAL_INT(0, events.count);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp(), enemies.items[e].hp);

    p->weapons.weapons[WEAPON_GRENADE].ammo = 0;
    fire();
    TEST_ASSERT_EQUAL_INT(0, events.count);
}

static void test_a_shot_does_not_go_through_a_wall(void)
{
    aim(&players[SHOOTER], 2.5f, WALL_ROW + 0.5f, 0.0f);
    int e = add_enemy(BLOCK_X + 3.5f, WALL_ROW + 0.5f);

    fire();

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp(), enemies.items[e].hp);
}

static void test_a_shot_does_not_go_through_a_closed_door(void)
{
    aim(&players[SHOOTER], 2.5f, DOOR_ROW + 0.5f, 0.0f);
    int e = add_enemy(BLOCK_X + 3.5f, DOOR_ROW + 0.5f);

    fire();
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp(), enemies.items[e].hp);

    /* the same shot lands once that door is out of the way */
    doors.doors[door_at(&doors, BLOCK_X, DOOR_ROW)].openness = 1.0f;
    players[SHOOTER].weapons.weapons[WEAPON_PISTOL].cooldown = 0.0f;

    fire();
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp() - pistol_damage(), enemies.items[e].hp);
}

/* The ray stops at the first thing it meets, not the best-looking one. */
static void test_a_shot_stops_at_the_nearest_enemy(void)
{
    aim(&players[SHOOTER], 2.5f, 2.5f, 0.0f);
    int near_e = add_enemy(5.5f, 2.5f);
    int far_e = add_enemy(8.5f, 2.5f);

    fire();

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp() - pistol_damage(), enemies.items[near_e].hp);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp(), enemies.items[far_e].hp);
}

/* A corpse is scenery: it takes no damage and shields nothing behind it. */
static void test_a_shot_passes_through_a_dead_enemy(void)
{
    aim(&players[SHOOTER], 2.5f, 2.5f, 0.0f);
    int corpse = add_enemy(5.5f, 2.5f);
    int alive = add_enemy(8.5f, 2.5f);
    enemies.items[corpse].state = ESTATE_DEAD;

    fire();

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp(), enemies.items[corpse].hp);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp() - pistol_damage(), enemies.items[alive].hp);
}

/* What decides a hit is the world hit radius. The screen band this replaced
 * grew with proximity: one cell away it covered most of the screen, so both
 * enemies below would have been hit, and how many depended on the screen
 * resolution. */
static void test_a_shot_hits_within_the_world_radius_only(void)
{
    aim(&players[SHOOTER], 2.5f, 2.5f, 0.0f);
    int inside = add_enemy(3.5f, 2.5f + INSIDE_OFFSET);
    int outside = add_enemy(3.5f, 2.5f - OUTSIDE_OFFSET);

    fire();

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp() - pistol_damage(), enemies.items[inside].hp);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp(), enemies.items[outside].hp);
}

/* PvP: armor absorbs half of the damage, and the shooter is recorded so the
 * frag can be attributed later. */
static void test_a_shot_damages_another_player_through_armor(void)
{
    aim(&players[SHOOTER], 2.5f, 8.5f, 0.0f);
    aim(&players[VICTIM], 7.5f, 8.5f, 0.0f);
    players[VICTIM].hp = 100.0f;
    players[VICTIM].armor = 20.0f;

    fire();

    float absorbed = pistol_damage() * 0.5f;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f - absorbed, players[VICTIM].armor);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f - (pistol_damage() - absorbed), players[VICTIM].hp);
    TEST_ASSERT_EQUAL_INT(SHOOTER, players[VICTIM].last_attacker);
}

/* The ray starts inside the shooter's own hit radius; it must ignore it. */
static void test_a_player_cannot_shoot_themselves(void)
{
    aim(&players[SHOOTER], 2.5f, 8.5f, 0.0f);
    players[SHOOTER].hp = 100.0f;

    fire();

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, players[SHOOTER].hp);
    TEST_ASSERT_EQUAL_INT(-1, players[SHOOTER].last_attacker);
}

/* A dead player is not a target. */
static void test_a_shot_ignores_a_player_who_is_not_alive(void)
{
    aim(&players[SHOOTER], 2.5f, 8.5f, 0.0f);
    aim(&players[VICTIM], 7.5f, 8.5f, 0.0f);
    players[VICTIM].hp = 100.0f;
    players[VICTIM].alive = 0;

    fire();

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, players[VICTIM].hp);
}

static void test_an_empty_weapon_does_not_fire(void)
{
    aim(&players[SHOOTER], 2.5f, 2.5f, 0.0f);
    int e = add_enemy(6.5f, 2.5f);
    players[SHOOTER].weapons.weapons[WEAPON_PISTOL].ammo = 0;

    fire();

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp(), enemies.items[e].hp);
    TEST_ASSERT_EQUAL_INT(0, players[SHOOTER].weapons.weapons[WEAPON_PISTOL].ammo);
}

/* Holding the trigger does not fire faster than the cooldown allows. */
static void test_a_weapon_on_cooldown_does_not_fire(void)
{
    aim(&players[SHOOTER], 2.5f, 2.5f, 0.0f);
    int e = add_enemy(6.5f, 2.5f);

    fire();
    fire();

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, imp_max_hp() - pistol_damage(), enemies.items[e].hp);
    TEST_ASSERT_EQUAL_INT(49, players[SHOOTER].weapons.weapons[WEAPON_PISTOL].ammo);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_weapon_system_init_fills_every_weapon);
    RUN_TEST(test_weapon_system_init_owns_the_pistol_alone);
    RUN_TEST(test_weapon_system_init_starts_ready_with_the_pistol);
    RUN_TEST(test_weapon_switch_selects_by_index);
    RUN_TEST(test_weapon_switch_refuses_a_weapon_not_owned);
    RUN_TEST(test_weapon_switch_ignores_an_index_outside_the_table);
    RUN_TEST(test_weapon_switch_to_the_current_weapon_is_a_no_op);
    RUN_TEST(test_weapon_update_runs_down_the_cooldown);
    RUN_TEST(test_weapon_update_ticks_every_weapon);
    RUN_TEST(test_weapon_update_decays_the_animation_to_zero);
    RUN_TEST(test_shot_animation_is_the_same_for_every_weapon);
    RUN_TEST(test_two_players_keep_separate_weapon_state);
    RUN_TEST(test_a_shot_damages_an_enemy_in_the_open);
    RUN_TEST(test_a_point_blank_blast_is_one_shot_and_one_pain);
    RUN_TEST(test_ten_blasts_in_one_tick_fit_the_queue);
    RUN_TEST(test_every_hitscan_weapon_damages_what_it_hits);
    RUN_TEST(test_each_weapon_spends_its_own_ammo);
    RUN_TEST(test_the_grenade_launcher_fires_nothing_yet);
    RUN_TEST(test_a_shot_does_not_go_through_a_wall);
    RUN_TEST(test_a_shot_does_not_go_through_a_closed_door);
    RUN_TEST(test_a_shot_stops_at_the_nearest_enemy);
    RUN_TEST(test_a_shot_passes_through_a_dead_enemy);
    RUN_TEST(test_a_shot_hits_within_the_world_radius_only);
    RUN_TEST(test_a_shot_damages_another_player_through_armor);
    RUN_TEST(test_a_player_cannot_shoot_themselves);
    RUN_TEST(test_a_shot_ignores_a_player_who_is_not_alive);
    RUN_TEST(test_an_empty_weapon_does_not_fire);
    RUN_TEST(test_a_weapon_on_cooldown_does_not_fire);
    return UNITY_END();
}
