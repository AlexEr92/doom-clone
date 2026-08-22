/* enemy.c: the AI state machine, searching the last seen position, damage and
 * armor absorption.
 *
 * The map is built in memory rather than loaded from assets/maps/level1.txt,
 * so the test depends on neither the working directory nor the level. The step
 * is 1/60 s, as in the game.
 *
 * sprite.c is not linked: only sprite_add() is referenced, and it is stubbed
 * below. Linking sprite.c would drag in raycast.c as well, purely to resolve
 * zBuffer.
 *
 * enemy.c makes no sound of its own — it appends to an EventQueue, so the
 * sounds are checked as the events that would produce them. */

#include "unity.h"
#include "enemy.h"
#include "sprite.h"
#include "event.h"
#include "map.h"
#include "player.h"
#include <math.h>
#include <string.h>

#define TEST_STEP (1.0 / 60.0)

/* Mirrors of the constants private to enemy.c. */
#define TEST_SEARCH_TIME 3.0f
#define TEST_SEARCH_REACH 0.3f

#define ENEMY_START_X 2.5f
#define ENEMY_START_Y 5.5f

static Map map;
static EnemyList enemies;
static SpriteList sprites;
static PlayerState player;
/* Filled by the module under test; never cleared inside a test, so a whole
 * scenario's events can be counted at the end of it. */
static EventQueue events;

/* ---- stubs ---- */

/* sprite_init is part of sprite.c, which is deliberately not linked. */
void sprite_init(SpriteList *sl)
{
    memset(sl, 0, sizeof(*sl));
}

int sprite_add(SpriteList *sl, float x, float y, int type)
{
    if (sl->count >= MAX_SPRITES) {
        return -1;
    }
    Sprite *sp = &sl->items[sl->count];
    sp->x = x;
    sp->y = y;
    sp->type = type;
    sp->active = 1;
    sp->scale = 1.0f;
    sp->vmove = 0;
    return sl->count++;
}

/* ---- fixture ---- */

/* 20x12, walled around the edge, with row 3 walled all the way across. The
 * strip at rows 1-2 is therefore out of sight from row 5, which is what the
 * lost-line-of-sight scenarios need. */
#define HIDDEN_X 2.5f
#define HIDDEN_Y 1.5f

static void build_map(Map *m)
{
    memset(m, 0, sizeof(*m));
    m->w = 20;
    m->h = 12;
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            int border = (x == 0 || y == 0 || x == m->w - 1 || y == m->h - 1);
            m->cells[y][x] = border ? 1 : 0;
        }
    }
    for (int x = 0; x < m->w; x++) {
        m->cells[3][x] = 1;
    }
}

static void step(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        enemy_update_all(&enemies, &sprites, &map, NULL, &player, &events, TEST_STEP);
    }
}

/* Run until the enemy reaches `want`, or fail after `limit` ticks. */
static void step_until_state(EnemyState want, int limit)
{
    for (int i = 0; i < limit; i++) {
        if (enemies.items[0].state == want) {
            return;
        }
        step(1);
    }
    TEST_ASSERT_EQUAL_INT(want, enemies.items[0].state);
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

/* The first event of `kind`, or NULL. */
static const GameEvent *find_event(GameEventKind kind)
{
    for (int i = 0; i < events.count; i++) {
        if (events.items[i].kind == kind) {
            return &events.items[i];
        }
    }
    return NULL;
}

static float dist_to(float x, float y)
{
    float dx = enemies.items[0].x - x;
    float dy = enemies.items[0].y - y;
    return sqrtf(dx * dx + dy * dy);
}

void setUp(void)
{
    build_map(&map);
    enemy_list_init(&enemies);
    sprite_init(&sprites);
    memset(&player, 0, sizeof(player));
    player.hp = 100.0f;
    player.alive = 1;
    event_queue_clear(&events);
    TEST_ASSERT_EQUAL_INT(0,
                          enemy_spawn(&enemies, &sprites, ENEMY_START_X, ENEMY_START_Y, ENEMY_IMP));
}

void tearDown(void)
{}

/* ---- spawning and definitions ---- */

static void test_enemy_spawn_creates_a_sprite_and_defaults(void)
{
    const EnemyDef *d = enemy_def(ENEMY_IMP);
    const Enemy *e = &enemies.items[0];

    TEST_ASSERT_EQUAL_INT(1, enemies.count);
    TEST_ASSERT_EQUAL_INT(ENEMY_IMP, e->type);
    TEST_ASSERT_EQUAL_INT(ESTATE_IDLE, e->state);
    TEST_ASSERT_EQUAL_FLOAT(d->max_hp, e->hp);
    TEST_ASSERT_EQUAL_FLOAT(ENEMY_START_X, e->x);
    TEST_ASSERT_EQUAL_FLOAT(ENEMY_START_Y, e->y);
    /* the search target starts where the enemy does */
    TEST_ASSERT_EQUAL_FLOAT(ENEMY_START_X, e->last_seen_x);
    TEST_ASSERT_EQUAL_FLOAT(ENEMY_START_Y, e->last_seen_y);

    TEST_ASSERT_EQUAL_INT(0, e->sprite_id);
    TEST_ASSERT_EQUAL_INT(1, sprites.count);
    TEST_ASSERT_EQUAL_INT(d->sprite_type, sprites.items[0].type);
    TEST_ASSERT_EQUAL_FLOAT(ENEMY_START_X, sprites.items[0].x);
}

static void test_enemy_def_clamps_an_unknown_type(void)
{
    TEST_ASSERT_EQUAL_PTR(enemy_def(ENEMY_IMP), enemy_def(-1));
    TEST_ASSERT_EQUAL_PTR(enemy_def(ENEMY_IMP), enemy_def(ENEMY_COUNT));
    /* the two archetypes really are different */
    TEST_ASSERT_TRUE(enemy_def(ENEMY_SERG)->max_hp > enemy_def(ENEMY_IMP)->max_hp);
}

static void test_enemy_spawn_falls_back_to_imp_for_an_unknown_type(void)
{
    TEST_ASSERT_EQUAL_INT(1, enemy_spawn(&enemies, &sprites, 6.5f, 5.5f, 99));
    TEST_ASSERT_EQUAL_INT(ENEMY_IMP, enemies.items[1].type);
}

/* ---- detection ---- */

static void test_enemy_ignores_a_player_out_of_detect_range(void)
{
    player.x = 15.5f;
    player.y = 5.5f;
    TEST_ASSERT_TRUE(dist_to(player.x, player.y) > enemy_def(ENEMY_IMP)->detect_range);

    step(120);
    TEST_ASSERT_EQUAL_INT(ESTATE_IDLE, enemies.items[0].state);
}

/* bug-001: within range but behind a wall is still not visible. */
static void test_enemy_does_not_see_through_a_wall(void)
{
    player.x = HIDDEN_X;
    player.y = HIDDEN_Y;
    TEST_ASSERT_TRUE(dist_to(player.x, player.y) < enemy_def(ENEMY_IMP)->detect_range);

    step(120);
    TEST_ASSERT_EQUAL_INT(ESTATE_IDLE, enemies.items[0].state);
    TEST_ASSERT_EQUAL_FLOAT(ENEMY_START_X, enemies.items[0].x);
    TEST_ASSERT_EQUAL_FLOAT(ENEMY_START_Y, enemies.items[0].y);
}

/* Spotting the player raises the alert first; the chase only starts after it
 * runs out, and losing sight in the meantime drops back to idle. */
static void test_alert_precedes_the_chase(void)
{
    player.x = 5.5f;
    player.y = 5.5f;

    step(1);
    TEST_ASSERT_EQUAL_INT(ESTATE_ALERT, enemies.items[0].state);
    TEST_ASSERT_EQUAL_FLOAT(ENEMY_START_X, enemies.items[0].x);

    /* out of sight again before the alert elapses */
    player.x = HIDDEN_X;
    player.y = HIDDEN_Y;
    step(1);
    TEST_ASSERT_EQUAL_INT(ESTATE_IDLE, enemies.items[0].state);
}

static void test_enemy_sprite_follows_the_enemy(void)
{
    player.x = 8.5f;
    player.y = 5.5f;
    step(120);

    TEST_ASSERT_TRUE(enemies.items[0].x > ENEMY_START_X);
    TEST_ASSERT_EQUAL_FLOAT(enemies.items[0].x, sprites.items[0].x);
    TEST_ASSERT_EQUAL_FLOAT(enemies.items[0].y, sprites.items[0].y);
}

/* ---- attacking ---- */

static void test_enemy_in_line_of_sight_attacks_and_armor_absorbs_half(void)
{
    const EnemyDef *d = enemy_def(ENEMY_IMP);
    player.x = 5.5f;
    player.y = 5.5f;
    player.armor = 100.0f;

    for (int i = 0; i < 600 && player.hp >= 100.0f; i++) {
        step(1);
    }

    TEST_ASSERT_EQUAL_INT(ESTATE_ATTACK, enemies.items[0].state);
    TEST_ASSERT_TRUE(dist_to(player.x, player.y) <= d->attack_range);
    /* half of the damage is taken off the armor, the rest off hp */
    TEST_ASSERT_EQUAL_FLOAT(100.0f - d->damage * 0.5f, player.armor);
    TEST_ASSERT_EQUAL_FLOAT(100.0f - d->damage * 0.5f, player.hp);

    /* the hit is announced once, as coming from the enemy: that position is
     * what a client measures its own distance to */
    TEST_ASSERT_EQUAL_INT(1, count_events(EV_PLAYER_HURT));
    const GameEvent *ev = find_event(EV_PLAYER_HURT);
    TEST_ASSERT_EQUAL_INT(player.id, ev->actor);
    TEST_ASSERT_EQUAL_FLOAT(enemies.items[0].x, ev->x);
    TEST_ASSERT_EQUAL_FLOAT(enemies.items[0].y, ev->y);
}

static void test_without_armor_the_player_takes_full_damage(void)
{
    const EnemyDef *d = enemy_def(ENEMY_IMP);
    player.x = 5.5f;
    player.y = 5.5f;
    player.armor = 0.0f;

    for (int i = 0; i < 600 && player.hp >= 100.0f; i++) {
        step(1);
    }

    TEST_ASSERT_EQUAL_FLOAT(100.0f - d->damage, player.hp);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, player.armor);
}

static void test_attacks_are_spaced_by_the_cooldown(void)
{
    const EnemyDef *d = enemy_def(ENEMY_IMP);
    player.x = 5.5f;
    player.y = 5.5f;

    for (int i = 0; i < 600 && player.hp >= 100.0f; i++) {
        step(1);
    }
    float after_first = player.hp;

    /* just short of the cooldown: still a single hit */
    step((int)((d->attack_cd - 0.2f) / (float)TEST_STEP));
    TEST_ASSERT_EQUAL_FLOAT(after_first, player.hp);

    step((int)(0.4f / (float)TEST_STEP));
    TEST_ASSERT_EQUAL_FLOAT(after_first - d->damage, player.hp);
    TEST_ASSERT_EQUAL_INT(2, count_events(EV_PLAYER_HURT));
}

/* ---- losing sight ---- */

/* Bring the enemy to CHASE while the player is visible at (target_x, 5.5),
 * then hide the player. The enemy has not moved yet: the tick that leaves
 * ALERT does not also move. */
static void chase_then_hide(float target_x)
{
    player.x = target_x;
    player.y = 5.5f;
    step_until_state(ESTATE_CHASE, 120);

    TEST_ASSERT_EQUAL_FLOAT(target_x, enemies.items[0].last_seen_x);
    TEST_ASSERT_EQUAL_FLOAT(5.5f, enemies.items[0].last_seen_y);
    TEST_ASSERT_EQUAL_FLOAT(ENEMY_START_X, enemies.items[0].x);

    player.x = HIDDEN_X;
    player.y = HIDDEN_Y;
}

/* The search target is the last seen position, not the player. The hidden
 * player sits due north of the enemy, so chasing them would move it in -Y;
 * chasing the last seen position moves it in +X. */
static void test_enemy_searches_the_last_seen_position(void)
{
    chase_then_hide(4.5f);

    step(30);
    TEST_ASSERT_EQUAL_INT(ESTATE_CHASE, enemies.items[0].state);
    TEST_ASSERT_TRUE(enemies.items[0].x > ENEMY_START_X);
    TEST_ASSERT_EQUAL_FLOAT(ENEMY_START_Y, enemies.items[0].y);
    TEST_ASSERT_TRUE(enemies.items[0].y > HIDDEN_Y);
}

static void test_enemy_gives_up_on_reaching_the_last_seen_position(void)
{
    chase_then_hide(4.5f);

    step_until_state(ESTATE_IDLE, (int)(TEST_SEARCH_TIME / (float)TEST_STEP));
    /* it arrived rather than timing out */
    TEST_ASSERT_TRUE(dist_to(4.5f, 5.5f) <= TEST_SEARCH_REACH);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, enemies.items[0].search_timer);

    float x = enemies.items[0].x;
    float y = enemies.items[0].y;
    step(120);
    TEST_ASSERT_EQUAL_INT(ESTATE_IDLE, enemies.items[0].state);
    TEST_ASSERT_EQUAL_FLOAT(x, enemies.items[0].x);
    TEST_ASSERT_EQUAL_FLOAT(y, enemies.items[0].y);
}

/* Too far to reach in ENEMY_SEARCH_TIME: the enemy gives up short of the
 * target instead of walking to it forever. */
static void test_enemy_gives_up_when_the_search_times_out(void)
{
    const float target_x = 9.5f;
    chase_then_hide(target_x);

    step_until_state(ESTATE_IDLE, (int)(2.0f * TEST_SEARCH_TIME / (float)TEST_STEP));
    TEST_ASSERT_TRUE(enemies.items[0].x > ENEMY_START_X);
    TEST_ASSERT_TRUE(dist_to(target_x, 5.5f) > TEST_SEARCH_REACH);

    float x = enemies.items[0].x;
    step(120);
    TEST_ASSERT_EQUAL_FLOAT(x, enemies.items[0].x);
}

/* ---- damage ---- */

/* Being shot from out of detection range still starts a chase, towards
 * whoever fired. */
static void test_damage_from_out_of_range_starts_a_chase_at_the_shooter(void)
{
    const float shooter_x = 15.5f;
    const float shooter_y = 5.5f;
    const EnemyDef *d = enemy_def(ENEMY_IMP);

    player.x = shooter_x;
    player.y = shooter_y;
    TEST_ASSERT_TRUE(dist_to(shooter_x, shooter_y) > d->detect_range);

    step(60);
    TEST_ASSERT_EQUAL_INT(ESTATE_IDLE, enemies.items[0].state);

    enemy_damage(&enemies, &sprites, 0, 10.0f, &events, shooter_x, shooter_y);

    TEST_ASSERT_EQUAL_INT(ESTATE_CHASE, enemies.items[0].state);
    TEST_ASSERT_EQUAL_FLOAT(d->max_hp - 10.0f, enemies.items[0].hp);
    TEST_ASSERT_EQUAL_FLOAT(shooter_x, enemies.items[0].last_seen_x);
    TEST_ASSERT_EQUAL_FLOAT(shooter_y, enemies.items[0].last_seen_y);
    TEST_ASSERT_EQUAL_FLOAT(TEST_SEARCH_TIME, enemies.items[0].search_timer);

    /* surviving the hit is EV_ENEMY_HURT, positioned on the enemy */
    TEST_ASSERT_EQUAL_INT(1, count_events(EV_ENEMY_HURT));
    TEST_ASSERT_EQUAL_INT(0, count_events(EV_ENEMY_DEATH));
    const GameEvent *ev = find_event(EV_ENEMY_HURT);
    TEST_ASSERT_EQUAL_INT(0, ev->actor);
    TEST_ASSERT_EQUAL_FLOAT(enemies.items[0].x, ev->x);
    TEST_ASSERT_EQUAL_FLOAT(enemies.items[0].y, ev->y);

    step(30);
    TEST_ASSERT_TRUE(enemies.items[0].x > ENEMY_START_X);
    TEST_ASSERT_EQUAL_FLOAT(ENEMY_START_Y, enemies.items[0].y);
}

static void test_lethal_damage_turns_the_sprite_into_a_corpse(void)
{
    enemy_damage(&enemies, &sprites, 0, enemy_def(ENEMY_IMP)->max_hp, &events, 5.5f, 5.5f);

    TEST_ASSERT_EQUAL_INT(ESTATE_DEAD, enemies.items[0].state);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, enemies.items[0].hp);
    TEST_ASSERT_EQUAL_INT(SPRITE_ENEMY_DEAD, sprites.items[0].type);
    TEST_ASSERT_EQUAL_FLOAT(0.55f, sprites.items[0].scale);
    TEST_ASSERT_EQUAL_INT(0, sprites.items[0].vmove);
    TEST_ASSERT_EQUAL_INT(1, count_events(EV_ENEMY_DEATH));
    TEST_ASSERT_EQUAL_INT(0, count_events(EV_ENEMY_HURT));
    /* a corpse stays a corpse */
    enemy_damage(&enemies, &sprites, 0, 100.0f, &events, 5.5f, 5.5f);
    TEST_ASSERT_EQUAL_INT(ESTATE_DEAD, enemies.items[0].state);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, enemies.items[0].hp);
    /* and dies only once */
    TEST_ASSERT_EQUAL_INT(1, count_events(EV_ENEMY_DEATH));
}

static void test_a_dead_enemy_does_not_attack(void)
{
    player.x = ENEMY_START_X + 0.5f;
    player.y = ENEMY_START_Y;
    enemy_damage(&enemies, &sprites, 0, enemy_def(ENEMY_IMP)->max_hp, &events, player.x, player.y);

    step(600);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, player.hp);
    TEST_ASSERT_EQUAL_INT(ESTATE_DEAD, enemies.items[0].state);
}

static void test_damage_to_an_index_outside_the_list_is_ignored(void)
{
    enemy_damage(&enemies, &sprites, -1, 10.0f, &events, 0.0f, 0.0f);
    enemy_damage(&enemies, &sprites, enemies.count, 10.0f, &events, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(enemy_def(ENEMY_IMP)->max_hp, enemies.items[0].hp);
    TEST_ASSERT_EQUAL_INT(0, events.count);
}

static void test_enemy_all_dead(void)
{
    EnemyList empty;
    enemy_list_init(&empty);
    /* nothing spawned is not a win */
    TEST_ASSERT_EQUAL_INT(0, enemy_all_dead(&empty));

    TEST_ASSERT_EQUAL_INT(1, enemy_spawn(&enemies, &sprites, 8.5f, 5.5f, ENEMY_SERG));
    TEST_ASSERT_EQUAL_INT(0, enemy_all_dead(&enemies));

    enemy_damage(&enemies, &sprites, 0, 1000.0f, &events, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_INT(0, enemy_all_dead(&enemies));

    enemy_damage(&enemies, &sprites, 1, 1000.0f, &events, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_INT(1, enemy_all_dead(&enemies));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_enemy_spawn_creates_a_sprite_and_defaults);
    RUN_TEST(test_enemy_def_clamps_an_unknown_type);
    RUN_TEST(test_enemy_spawn_falls_back_to_imp_for_an_unknown_type);
    RUN_TEST(test_enemy_ignores_a_player_out_of_detect_range);
    RUN_TEST(test_enemy_does_not_see_through_a_wall);
    RUN_TEST(test_alert_precedes_the_chase);
    RUN_TEST(test_enemy_sprite_follows_the_enemy);
    RUN_TEST(test_enemy_in_line_of_sight_attacks_and_armor_absorbs_half);
    RUN_TEST(test_without_armor_the_player_takes_full_damage);
    RUN_TEST(test_attacks_are_spaced_by_the_cooldown);
    RUN_TEST(test_enemy_searches_the_last_seen_position);
    RUN_TEST(test_enemy_gives_up_on_reaching_the_last_seen_position);
    RUN_TEST(test_enemy_gives_up_when_the_search_times_out);
    RUN_TEST(test_damage_from_out_of_range_starts_a_chase_at_the_shooter);
    RUN_TEST(test_lethal_damage_turns_the_sprite_into_a_corpse);
    RUN_TEST(test_a_dead_enemy_does_not_attack);
    RUN_TEST(test_damage_to_an_index_outside_the_list_is_ignored);
    RUN_TEST(test_enemy_all_dead);
    return UNITY_END();
}
