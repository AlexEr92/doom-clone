/* world.c: who comes into the world, and where. The tick itself is held by
 * replay_test.c; this is world_add_player() and world_pick_spawn() — the solo
 * start, the spawn pool, and the score that picks a point out of it.
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
    return UNITY_END();
}
