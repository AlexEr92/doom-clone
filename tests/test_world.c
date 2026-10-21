/* world.c: who comes into the world, and where. The tick itself is held by
 * replay_test.c; this is world_add_player() and world_pick_spawn() — the solo
 * start, the spawn pool, and the score that picks a point out of it — and the
 * part of the tick that takes a player out and brings them back: death,
 * respawn and the protection after it.
 *
 * Each test writes its own map, so neither the working directory nor later
 * edits to assets/maps/ can break it. */

#include "unity.h"
#include "world.h"
#include "raycast_world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAP_PATH "world_map.txt"

/* Static: a World is tens of kilobytes, too much for the stack of a test. */
static World w;

static void load_rows(const char *const *rows, int n)
{
    FILE *f = fopen(MAP_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    for (int i = 0; i < n; i++) {
        fprintf(f, "%s\n", rows[i]);
    }
    fclose(f);
    TEST_ASSERT_EQUAL_INT(0, world_load(&w, MAP_PATH));
}
#define LOAD_ROWS(rows) load_rows(rows, (int)(sizeof(rows) / sizeof(rows[0])))

void setUp(void)
{}

void tearDown(void)
{
    remove(MAP_PATH);
}

/* Which of the pool's points player `id` stands on, or -1 for none. */
static int point_of(int id)
{
    for (int i = 0; i < w.map.spawn_count; i++) {
        if (w.players[id].x == w.map.spawns[i].x && w.players[id].y == w.map.spawns[i].y) {
            return i;
        }
    }
    return -1;
}

static int pick(int id)
{
    const SpawnPoint *sp = world_pick_spawn(&w, id);
    TEST_ASSERT_NOT_NULL(sp);
    return (int)(sp - w.map.spawns);
}

/* One room, three points in reading order: (1, 1), (8, 1) and (1, 6).
 *   0123456789
 * 0 ##########
 * 1 #sP.....s#
 * 2 #........#  ... to row 5
 * 6 #s.......#
 * 7 ########## */
static const char *const room[] = {
        "##########", "#sP.....s#", "#........#", "#........#",
        "#........#", "#........#", "#s.......#", "##########",
};

/* ---- the start and the pool ---- */

/* Solo comes in on the 'P', facing -X as it always has, and leaves no trace in
 * the pool's history. */
static void test_solo_player_starts_on_the_start_cell(void)
{
    LOAD_ROWS(room);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, &w.map.start));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, w.players[0].x);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, w.players[0].y);
    TEST_ASSERT_EQUAL_FLOAT(MATH_PI, w.players[0].angle);
    for (int i = 0; i < MAX_SPAWNS; i++) {
        TEST_ASSERT_EQUAL_UINT32(0, w.spawned_at[0][i]);
    }
}

/* Alone in the world every point scores the same, and the first one wins. The
 * player faces the way the point does. */
static void test_first_player_comes_in_at_the_first_point(void)
{
    LOAD_ROWS(room);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(0, point_of(0));
    TEST_ASSERT_EQUAL_FLOAT(w.map.spawns[0].angle, w.players[0].angle);
}

/* Everyone sees everything in one room, so distance decides: (8, 1) is seven
 * from (1, 1), (1, 6) five. */
static void test_next_player_comes_in_farthest_from_the_others(void)
{
    LOAD_ROWS(room);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(1, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(1, point_of(1));
    TEST_ASSERT_EQUAL_INT(2, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(2, point_of(2));
}

/* A map with no pool at all — nothing can make one, but the world must not
 * fall over — puts a joining player on the start. */
static void test_without_a_pool_a_player_comes_in_on_the_start(void)
{
    LOAD_ROWS(room);
    w.map.spawn_count = 0;
    TEST_ASSERT_NULL(world_pick_spawn(&w, 0));
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_FLOAT(w.map.start.x, w.players[0].x);
    TEST_ASSERT_EQUAL_FLOAT(w.map.start.y, w.players[0].y);
}

/* ---- the score ---- */

/* Out of sight beats any distance in it. Player 0 comes in at (1, 1); (10, 1)
 * is nine cells off but in the same room, (1, 4) three behind the wall.
 *   0123456789012
 * 0 ############
 * 1 #s........s#
 * 2 #..........#
 * 3 #####.######
 * 4 #s.........#
 * 5 ############ */
static void test_a_point_out_of_sight_beats_a_farther_one_in_sight(void)
{
    static const char *const rows[] = {
            "############", "#s........s#", "#..........#",
            "#####.######", "#s.........#", "############",
    };
    LOAD_ROWS(rows);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(0, point_of(0));
    TEST_ASSERT_EQUAL_INT(2, pick(1));

    /* Through the gap and into the lower room, player 0 has (1, 4) in sight
     * and both the others out of it: the farther of those two wins. */
    w.players[0].x = 5.5f;
    w.players[0].y = 4.5f;
    TEST_ASSERT_EQUAL_INT(1, pick(1));
}

/* In sight is in sight, by one or by many: among the points somebody sees,
 * the farthest wins. (1, 1) is seen by player 0 alone, two cells off; (10, 5)
 * by both players, the nearer four and a half away.
 *   012345678901
 * 0 ############
 * 1 #s...#.....#
 * 2 #....#.....#
 * 3 #....#.....#
 * 4 #..........#
 * 5 #.........s#
 * 6 ############ */
static void test_among_points_in_sight_the_farther_wins(void)
{
    static const char *const rows[] = {
            "############", "#s...#.....#", "#....#.....#", "#....#.....#",
            "#..........#", "#.........s#", "############",
    };
    LOAD_ROWS(rows);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(1, world_add_player(&w, NULL));
    w.players[0].x = 2.5f;
    w.players[0].y = 3.5f;
    w.players[1].x = 8.5f;
    w.players[1].y = 1.5f;
    TEST_ASSERT_EQUAL_INT(1, pick(2));
}

/* Sight reaches across the largest map: a wall at the far end of its diagonal
 * hides the point behind it, and that point, out of sight and the farther,
 * beats the one just round a corner. Player 0 is at (1, 1); the corner is a
 * wall at (1..3, 5) between it and (1, 10), the far point (46, 45) is behind a
 * block at (42..44, 41..43), and a trace there crosses over 80 cells. */
static void test_a_wall_across_the_largest_map_hides_a_point(void)
{
    static char rows[MAP_MAX_H][MAP_MAX_W + 1];
    const char *row_ptrs[MAP_MAX_H];
    for (int y = 0; y < MAP_MAX_H; y++) {
        for (int x = 0; x < MAP_MAX_W; x++) {
            int edge = x == 0 || y == 0 || x == MAP_MAX_W - 1 || y == MAP_MAX_H - 1;
            int corner = y == 5 && x <= 3;
            int block = x >= MAP_MAX_W - 6 && x <= MAP_MAX_W - 4 && y >= MAP_MAX_H - 7 &&
                        y <= MAP_MAX_H - 5;
            rows[y][x] = edge || corner || block ? '#' : '.';
        }
        rows[y][MAP_MAX_W] = '\0';
        row_ptrs[y] = rows[y];
    }
    rows[1][1] = 's';
    rows[10][1] = 's';
    rows[MAP_MAX_H - 3][MAP_MAX_W - 2] = 's';
    load_rows(row_ptrs, MAP_MAX_H);

    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(0, point_of(0));
    TEST_ASSERT_FALSE(
            world_line_of_sight(&w.map, &w.doors, 1.5f, 1.5f, MAP_MAX_W - 1.5f, MAP_MAX_H - 2.5f));
    TEST_ASSERT_EQUAL_INT(2, pick(1));
}

/* A live enemy counts against every point within its detect range, by as much
 * as the point is inside it; a dead one not at all. From (1, 1), (8, 1) is the
 * farther point by two cells, until an imp stands one cell from it. */
static void test_a_live_enemy_counts_against_a_point_near_it(void)
{
    LOAD_ROWS(room);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(1, pick(1));

    int imp = enemy_spawn(&w.enemies, 8.5f, 2.5f, ENEMY_IMP);
    TEST_ASSERT_TRUE(imp >= 0);
    TEST_ASSERT_EQUAL_INT(2, pick(1));

    w.enemies.items[imp].state = ESTATE_DEAD;
    TEST_ASSERT_EQUAL_INT(1, pick(1));
}

/* A point this player came in at less than ten seconds ago gives way to one
 * that is as good otherwise; after ten seconds it is as good as any. */
static void test_a_point_used_within_ten_seconds_gives_way(void)
{
    LOAD_ROWS(room);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(0, point_of(0));
    /* Nobody else is in, so every point is as far from everyone. */
    TEST_ASSERT_EQUAL_INT(1, pick(0));

    w.tick += 10 * TICK_RATE - 1;
    TEST_ASSERT_EQUAL_INT(1, pick(0));
    w.tick += 1;
    TEST_ASSERT_EQUAL_INT(0, pick(0));
}

/* Whoever takes a freed slot starts with no history: the point the last owner
 * came in at a moment ago is not held against them. */
static void test_a_reused_slot_starts_with_no_history(void)
{
    LOAD_ROWS(room);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    world_remove_player(&w, 0);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(0, point_of(0));
}

/* A point someone stands on is never chosen while another is free, however it
 * scores: here the two imps on (5, 1) make it worse than (1, 1), which is
 * taken. With every point taken, one of them is chosen all the same. */
static void test_a_taken_point_is_chosen_only_when_all_are(void)
{
    static const char *const rows[] = {"#######", "#s...s#", "#######"};
    LOAD_ROWS(rows);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(0, point_of(0));
    enemy_spawn(&w.enemies, 5.5f, 1.5f, ENEMY_IMP);
    enemy_spawn(&w.enemies, 5.5f, 1.5f, ENEMY_IMP);

    TEST_ASSERT_EQUAL_INT(1, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(1, point_of(1));

    TEST_ASSERT_EQUAL_INT(2, world_add_player(&w, NULL));
    TEST_ASSERT_TRUE(point_of(2) >= 0);
}

/* ---- a real map ---- */

/* level1.txt as it was before it had spawn points: what an old copy of a map
 * brings. Row 8 cut to the 24 columns map_load() reads. */
static const char *const old_level1[] = {
        "########################", "#......................#", "#..P..................E#",
        "#......................#", "#......##########......#", "#......#........#......#",
        "#......#..####...#.....#", "#......#..B..#...#.....#", "#......#..M..A...#.....#",
        "#......#..####...#.....#", "#......#........#......#", "#......###D######......#",
        "#......................#", "#........B.............#", "#......................#",
        "#........R..S..........#", "#......................#", "#......................#",
        "#..........E...........#", "#......................#", "#..........A...........#",
        "#......................#", "#......................#", "########################",
};

/* The pool made for it: twice as many points as players, spread over the map
 * — no two nearer than four cells, where twenty evenly spread over its free
 * cells would be four and a half apart — each on a free cell and facing a free
 * one. */
static void test_a_map_without_spawn_points_gets_a_sensible_pool(void)
{
    LOAD_ROWS(old_level1);
    TEST_ASSERT_EQUAL_INT(2 * MAX_PLAYERS, w.map.spawn_count);
    for (int i = 0; i < w.map.spawn_count; i++) {
        const SpawnPoint *sp = &w.map.spawns[i];
        int cx = (int)sp->x, cy = (int)sp->y;
        TEST_ASSERT_EQUAL_INT(0, map_cell(&w.map, cx, cy));
        int fx = cx + (int)lroundf(cosf(sp->angle));
        int fy = cy + (int)lroundf(sinf(sp->angle));
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, map_cell(&w.map, fx, fy), "a point facing a wall");
        for (int k = 0; k < i; k++) {
            float dx = sp->x - w.map.spawns[k].x, dy = sp->y - w.map.spawns[k].y;
            TEST_ASSERT_TRUE(sqrtf(dx * dx + dy * dy) >= 4.0f);
        }
    }
}

/* Ten players come in one after another, each at a point of their own, none
 * inside a wall or inside another. */
static void test_ten_players_come_in_apart(void)
{
    LOAD_ROWS(old_level1);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        TEST_ASSERT_EQUAL_INT(i, world_add_player(&w, NULL));
        TEST_ASSERT_TRUE(point_of(i) >= 0);
        TEST_ASSERT_EQUAL_INT(0,
                              map_is_wall_door(&w.map, &w.doors, w.players[i].x, w.players[i].y));
        for (int k = 0; k < i; k++) {
            TEST_ASSERT_NOT_EQUAL(point_of(k), point_of(i));
        }
    }
}

/* ---- death and respawn ---- */

/* Ticks of the world with nobody pressing anything. */
static void idle(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        world_step(&w, NULL, 0, FIXED_DT);
    }
}

/* Two players in `room`, 0 on the first point and 1 on the second, and 1
 * killed by 0 with a pickup of theirs in hand: as the respawn has to find
 * them. */
static PlayerState *room_with_one_dead(void)
{
    LOAD_ROWS(room);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    TEST_ASSERT_EQUAL_INT(1, world_add_player(&w, NULL));
    PlayerState *p = &w.players[1];
    p->weapons.weapons[WEAPON_SHOTGUN].owned = 1;
    p->weapons.weapons[WEAPON_SHOTGUN].ammo = 7;
    weapon_switch(p, WEAPON_SHOTGUN);
    p->weapons.weapons[WEAPON_PISTOL].ammo = 3;
    p->armor = 40.0f;
    player_damage(p, 1000.0f, 0, 0.0f, 0.0f, NULL);
    TEST_ASSERT_FALSE(p->alive);
    return p;
}

/* Tick until `p` is back, for RESPAWN_TIME and a tick or two at most. */
static void wait_for_respawn(const PlayerState *p)
{
    for (int i = 0; i < (int)(RESPAWN_TIME * TICK_RATE) + 2 && !p->alive; i++) {
        idle(1);
    }
    TEST_ASSERT_TRUE(p->alive);
}

/* RESPAWN_TIME after the death and no sooner, the player is back: at the point
 * world_pick_spawn() finds now and facing the way it looks, whole, with the
 * pistol alone and loaded, credited to nobody, and protected — the same id,
 * so the same character on every client. */
static void test_a_dead_player_comes_back_whole_after_respawn_time(void)
{
    PlayerState *p = room_with_one_dead();
    float fell_x = p->x, fell_y = p->y;

    int ticks = (int)(RESPAWN_TIME * TICK_RATE) - 1;
    idle(ticks);
    TEST_ASSERT_FALSE(p->alive);
    TEST_ASSERT_EQUAL_FLOAT(fell_x, p->x);
    TEST_ASSERT_EQUAL_FLOAT(fell_y, p->y);

    const SpawnPoint *at = NULL;
    for (int i = 0; i < 2 && !p->alive; i++, ticks++) {
        at = world_pick_spawn(&w, 1);
        idle(1);
    }
    TEST_ASSERT_TRUE(p->alive);
    TEST_ASSERT_INT_WITHIN(1, (int)(RESPAWN_TIME * TICK_RATE), ticks);
    TEST_ASSERT_NOT_NULL(at);
    TEST_ASSERT_EQUAL_FLOAT(at->x, p->x);
    TEST_ASSERT_EQUAL_FLOAT(at->y, p->y);
    TEST_ASSERT_EQUAL_FLOAT(at->angle, p->angle);
    TEST_ASSERT_FALSE(p->x == fell_x && p->y == fell_y);

    TEST_ASSERT_EQUAL_UINT8(1, p->id);
    TEST_ASSERT_TRUE(p->active);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, p->hp);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p->armor);
    TEST_ASSERT_EQUAL_INT(-1, p->last_attacker);
    TEST_ASSERT_EQUAL_FLOAT(SPAWN_PROTECT_TIME, p->spawn_protect_timer);
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, p->weapons.current);
    TEST_ASSERT_FALSE(p->weapons.weapons[WEAPON_SHOTGUN].owned);
    TEST_ASSERT_EQUAL_INT(0, p->weapons.weapons[WEAPON_SHOTGUN].ammo);
    TEST_ASSERT_EQUAL_INT(50, p->weapons.weapons[WEAPON_PISTOL].ammo);
}

/* Two points and a door between them, with a way round it.
 *   012345678
 * 0 #########
 * 1 #s..D..s#
 * 2 #.......#
 * 3 ######### */
static const char *const door_room[] = {
        "#########",
        "#s..D..s#",
        "#.......#",
        "#########",
};

/* Dead, a player does nothing whatever they press: they do not walk, turn,
 * fire, switch weapons or open a door. */
static void test_a_dead_player_does_nothing(void)
{
    LOAD_ROWS(door_room);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    PlayerState *p = &w.players[0];
    p->x = 3.5f;
    p->y = 1.5f;
    p->angle = 0.0f; /* at the door, facing it */
    p->weapons.weapons[WEAPON_SHOTGUN].owned = 1;
    player_damage(p, 1000.0f, -1, 0.0f, 0.0f, NULL);

    PlayerInput in = {0, BTN_FORWARD | BTN_FIRE | BTN_USE | BTN_WEAPON2, 1.0f};
    for (int i = 0; i < 30; i++) {
        world_step(&w, &in, 1, FIXED_DT);
        for (int k = 0; k < w.events.count; k++) {
            TEST_ASSERT_NOT_EQUAL(EV_SHOT, w.events.items[k].kind);
            TEST_ASSERT_NOT_EQUAL(EV_NO_AMMO, w.events.items[k].kind);
            TEST_ASSERT_NOT_EQUAL(EV_DOOR, w.events.items[k].kind);
        }
    }
    TEST_ASSERT_EQUAL_FLOAT(3.5f, p->x);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, p->y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p->angle);
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, p->weapons.current);
    TEST_ASSERT_EQUAL_INT(50, p->weapons.weapons[WEAPON_PISTOL].ammo);
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, w.doors.doors[0].state);
}

/* A body in the doorway does not hold the door open, as a live player there
 * would: the door starts to close once its time is up (DOOR_OPEN_TIME, 4 s
 * in door.c), not RESPAWN_TIME after that, when the body has gone. */
static void test_a_body_in_the_doorway_does_not_hold_the_door(void)
{
    LOAD_ROWS(door_room);
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&w, NULL));
    PlayerState *p = &w.players[0];
    p->x = 4.5f; /* in the doorway */
    p->y = 1.5f;
    Door *d = &w.doors.doors[0];
    d->state = DOOR_OPEN;
    d->openness = 1.0f;
    d->timer = 4.0f;
    player_damage(p, 1000.0f, -1, 0.0f, 0.0f, NULL);

    idle(4 * TICK_RATE + 2);
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSING, d->state);
}

/* While one player is dead the rest play on, tick for tick: they walk, fire,
 * and walk through where the dead one lies. */
static void test_the_others_play_on_while_one_is_dead(void)
{
    PlayerState *dead = room_with_one_dead();
    dead->x = 5.5f; /* where it fell: in the middle of the room */
    PlayerState *p = &w.players[0];
    p->x = 2.5f;
    p->y = dead->y;
    PlayerInput in[2] = {{0, BTN_FORWARD | BTN_FIRE, 0.0f}, {0, 0, 0.0f}};

    for (int i = 0; i < 90 && !dead->alive; i++) {
        float x = p->x;
        world_step(&w, in, 2, FIXED_DT);
        TEST_ASSERT_TRUE(p->x > x);
    }
    TEST_ASSERT_FALSE(dead->alive);
    TEST_ASSERT_TRUE(p->x > dead->x); /* past them, not stopped by them */
    TEST_ASSERT_TRUE(p->weapons.weapons[WEAPON_PISTOL].ammo < 50);
}

/* Back from the dead, nothing hurts a player for SPAWN_PROTECT_TIME; then
 * everything does again. */
static void test_protection_wears_off_after_its_time(void)
{
    PlayerState *p = room_with_one_dead();
    wait_for_respawn(p);
    player_damage(p, 50.0f, 0, 0.0f, 0.0f, NULL);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, p->hp);

    idle((int)(SPAWN_PROTECT_TIME * TICK_RATE) - 1);
    TEST_ASSERT_TRUE(p->spawn_protect_timer > 0.0f);
    idle(2);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p->spawn_protect_timer);
    player_damage(p, 50.0f, 0, 0.0f, 0.0f, NULL);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, p->hp);
}

/* The first shot of a protected player ends the protection there and then. */
static void test_firing_ends_the_protection(void)
{
    PlayerState *p = room_with_one_dead();
    wait_for_respawn(p);
    PlayerInput in[2] = {{0, 0, w.players[0].angle}, {0, BTN_FIRE, p->angle}};
    world_step(&w, in, 2, FIXED_DT);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p->spawn_protect_timer);
    TEST_ASSERT_EQUAL_INT(49, p->weapons.weapons[WEAPON_PISTOL].ammo);
}

/* Players on one line across `room`, facing along it: `id` at column x. */
static void line_up(int id, float x, float angle)
{
    w.players[id].x = x;
    w.players[id].y = 3.5f;
    w.players[id].angle = angle;
}

/* Two players fire at each other in one tick, one of them with a hit left in
 * them. The one with the lower id shoots first; the other is dead by then and
 * fires no more — their shot does not go off at all. */
static void test_a_player_shot_dead_does_not_fire_back_in_that_tick(void)
{
    LOAD_ROWS(room);
    world_add_player(&w, NULL);
    world_add_player(&w, NULL);
    line_up(0, 2.5f, 0.0f);
    line_up(1, 6.5f, MATH_PI);
    w.players[1].hp = 1.0f;

    PlayerInput in[2] = {{0, BTN_FIRE, 0.0f}, {0, BTN_FIRE, MATH_PI}};
    world_step(&w, in, 2, FIXED_DT);

    TEST_ASSERT_FALSE(w.players[1].alive);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, w.players[0].hp);
    TEST_ASSERT_EQUAL_INT(50, w.players[1].weapons.weapons[WEAPON_PISTOL].ammo);
}

/* Two players shoot a third in one tick, and the first shot kills: the second
 * passes through where the third fell and the kill stays the first one's. */
static void test_a_kill_is_not_taken_over_in_the_same_tick(void)
{
    LOAD_ROWS(room);
    for (int i = 0; i < 3; i++) {
        world_add_player(&w, NULL);
    }
    line_up(0, 2.5f, 0.0f);
    line_up(1, 5.5f, 0.0f);
    line_up(2, 8.5f, MATH_PI);
    w.players[1].hp = 1.0f;

    PlayerInput in[3] = {{0, BTN_FIRE, 0.0f}, {0, 0, 0.0f}, {0, BTN_FIRE, MATH_PI}};
    world_step(&w, in, 3, FIXED_DT);

    TEST_ASSERT_FALSE(w.players[1].alive);
    TEST_ASSERT_EQUAL_INT(0, w.players[1].last_attacker);
    int deaths = 0;
    for (int k = 0; k < w.events.count; k++) {
        const GameEvent *e = &w.events.items[k];
        if (e->kind == EV_PLAYER_DEATH) {
            deaths++;
            TEST_ASSERT_EQUAL_UINT8(0, e->actor);
            TEST_ASSERT_EQUAL_UINT8(1, e->arg);
        }
    }
    TEST_ASSERT_EQUAL_INT(1, deaths);
    /* ...and 2's shot flew on through the gap 1 left, into 0 */
    TEST_ASSERT_TRUE(w.players[0].hp < 100.0f);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_solo_player_starts_on_the_start_cell);
    RUN_TEST(test_first_player_comes_in_at_the_first_point);
    RUN_TEST(test_next_player_comes_in_farthest_from_the_others);
    RUN_TEST(test_without_a_pool_a_player_comes_in_on_the_start);
    RUN_TEST(test_a_point_out_of_sight_beats_a_farther_one_in_sight);
    RUN_TEST(test_among_points_in_sight_the_farther_wins);
    RUN_TEST(test_a_wall_across_the_largest_map_hides_a_point);
    RUN_TEST(test_a_live_enemy_counts_against_a_point_near_it);
    RUN_TEST(test_a_point_used_within_ten_seconds_gives_way);
    RUN_TEST(test_a_reused_slot_starts_with_no_history);
    RUN_TEST(test_a_taken_point_is_chosen_only_when_all_are);
    RUN_TEST(test_a_map_without_spawn_points_gets_a_sensible_pool);
    RUN_TEST(test_ten_players_come_in_apart);
    RUN_TEST(test_a_dead_player_comes_back_whole_after_respawn_time);
    RUN_TEST(test_a_dead_player_does_nothing);
    RUN_TEST(test_a_body_in_the_doorway_does_not_hold_the_door);
    RUN_TEST(test_the_others_play_on_while_one_is_dead);
    RUN_TEST(test_protection_wears_off_after_its_time);
    RUN_TEST(test_firing_ends_the_protection);
    RUN_TEST(test_a_player_shot_dead_does_not_fire_back_in_that_tick);
    RUN_TEST(test_a_kill_is_not_taken_over_in_the_same_tick);
    return UNITY_END();
}
