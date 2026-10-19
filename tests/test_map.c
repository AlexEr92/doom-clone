/* map.c: parsing the ASCII grid, and the wall tests built on it. The map is
 * written to a temporary file by the test itself rather than read from
 * assets/maps/level1.txt, so neither the working directory nor later edits to
 * the level can break it. */

#include "unity.h"
#include "map.h"
#include "door.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_MAP_PATH "test_map_data.txt"

/*  0123456789
 * 0##########
 * 1#P.......#
 * 2#..D.....#
 * 3#.EMA....#
 * 4##########
 * Everything past column 9 and past row 4 is filled in as wall by map_load. */
static const char *const map_rows[] = {
        "##########", "#P.......#", "#..D.....#", "#.EMA....#", "##########",
};

static void write_map(const char *path, const char *const *rows, int n, const char *eol)
{
    FILE *f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    for (int i = 0; i < n; i++) {
        fputs(rows[i], f);
        fputs(eol, f);
    }
    fclose(f);
}

static Map map;

/* A map of the test's own in place of the one setUp() loads. */
static void load_rows(Map *m, const char *const *rows, int n)
{
    write_map(TEST_MAP_PATH, rows, n, "\n");
    TEST_ASSERT_EQUAL_INT(0, map_load(m, TEST_MAP_PATH));
}
#define LOAD_ROWS(m, rows) load_rows(m, rows, (int)(sizeof(rows) / sizeof(rows[0])))

void setUp(void)
{
    memset(&map, 0, sizeof(map));
    write_map(TEST_MAP_PATH, map_rows, (int)(sizeof(map_rows) / sizeof(map_rows[0])), "\n");
    TEST_ASSERT_EQUAL_INT(0, map_load(&map, TEST_MAP_PATH));
}

void tearDown(void)
{
    remove(TEST_MAP_PATH);
}

static void test_map_load_fills_the_full_grid(void)
{
    TEST_ASSERT_EQUAL_INT(MAP_MAX_W, map.w);
    TEST_ASSERT_EQUAL_INT(MAP_MAX_H, map.h);
}

static void test_map_load_reads_walls_and_floor(void)
{
    TEST_ASSERT_EQUAL_INT(1, map_cell(&map, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, map_cell(&map, 9, 0));
    TEST_ASSERT_EQUAL_INT(0, map_cell(&map, 2, 1));
}

static void test_map_load_reads_the_door_cell(void)
{
    TEST_ASSERT_EQUAL_INT(2, map_cell(&map, 3, 2));
}

/* 'P' marks the start, at the centre of its cell and facing -X as a solo game
 * always has, and leaves an empty cell behind. */
static void test_map_load_reads_the_player_start(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.5f, map.start.x);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, map.start.y);
    TEST_ASSERT_EQUAL_FLOAT(MATH_PI, map.start.angle);
    TEST_ASSERT_EQUAL_INT(0, map_cell(&map, 1, 1));
}

/* Sprite markers are recorded at the centre of their cell and leave the cell
 * itself walkable. */
static void test_map_load_collects_sprite_spawns(void)
{
    TEST_ASSERT_EQUAL_INT(3, map.sprite_count);

    TEST_ASSERT_EQUAL_CHAR('E', map.sprites[0].marker);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, map.sprites[0].x);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, map.sprites[0].y);

    TEST_ASSERT_EQUAL_CHAR('M', map.sprites[1].marker);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, map.sprites[1].x);

    TEST_ASSERT_EQUAL_CHAR('A', map.sprites[2].marker);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, map.sprites[2].x);

    TEST_ASSERT_EQUAL_INT(0, map_cell(&map, 2, 3));
    TEST_ASSERT_EQUAL_INT(0, map_cell(&map, 3, 3));
    TEST_ASSERT_EQUAL_INT(0, map_cell(&map, 4, 3));
}

/* Short lines and missing rows become wall, so a small map is still sealed. */
static void test_map_load_pads_short_rows_and_missing_rows(void)
{
    TEST_ASSERT_EQUAL_INT(1, map_cell(&map, 10, 1));
    TEST_ASSERT_EQUAL_INT(1, map_cell(&map, MAP_MAX_W - 1, 1));
    TEST_ASSERT_EQUAL_INT(1, map_cell(&map, 2, 5));
    TEST_ASSERT_EQUAL_INT(1, map_cell(&map, 2, MAP_MAX_H - 1));
}

/* A CRLF file must parse identically: the trailing \r is stripped, not taken
 * for an unknown character that would leave a hole in the wall. */
static void test_map_load_strips_crlf(void)
{
    Map crlf;
    write_map(TEST_MAP_PATH, map_rows, (int)(sizeof(map_rows) / sizeof(map_rows[0])), "\r\n");
    TEST_ASSERT_EQUAL_INT(0, map_load(&crlf, TEST_MAP_PATH));
    TEST_ASSERT_EQUAL_INT(1, map_cell(&crlf, 9, 1));
    TEST_ASSERT_EQUAL_INT(1, map_cell(&crlf, 10, 1));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, crlf.start.x);
}

static void test_map_load_rejects_a_missing_file(void)
{
    Map missing;
    TEST_ASSERT_EQUAL_INT(-1, map_load(&missing, "no_such_map_file.txt"));
}

/* Off-grid coordinates read as wall, so callers never need a bounds check. */
static void test_map_cell_outside_the_grid_is_wall(void)
{
    TEST_ASSERT_EQUAL_INT(1, map_cell(&map, -1, 1));
    TEST_ASSERT_EQUAL_INT(1, map_cell(&map, 1, -1));
    TEST_ASSERT_EQUAL_INT(1, map_cell(&map, MAP_MAX_W, 1));
    TEST_ASSERT_EQUAL_INT(1, map_cell(&map, 1, MAP_MAX_H));
}

static void test_map_is_wall_uses_the_containing_cell(void)
{
    TEST_ASSERT_EQUAL_INT(1, map_is_wall(&map, 0.5f, 0.5f));
    TEST_ASSERT_EQUAL_INT(0, map_is_wall(&map, 1.5f, 1.5f));
    TEST_ASSERT_EQUAL_INT(0, map_is_wall(&map, 1.99f, 1.01f));
    /* a door is non-empty geometry as far as map_is_wall is concerned */
    TEST_ASSERT_EQUAL_INT(1, map_is_wall(&map, 3.5f, 2.5f));
}

/* Without a door list there is nothing to open, so a door reads as solid. */
static void test_map_is_wall_door_without_doors_blocks(void)
{
    TEST_ASSERT_EQUAL_INT(1, map_is_wall_door(&map, NULL, 3.5f, 2.5f));
    TEST_ASSERT_EQUAL_INT(1, map_is_wall_door(&map, NULL, 0.5f, 0.5f));
    TEST_ASSERT_EQUAL_INT(0, map_is_wall_door(&map, NULL, 1.5f, 1.5f));
}

static void test_map_is_wall_door_follows_openness(void)
{
    DoorList dl;
    door_list_init(&dl);
    TEST_ASSERT_EQUAL_INT(1, door_discover(&dl, &map));

    /* closed */
    TEST_ASSERT_EQUAL_INT(1, map_is_wall_door(&map, &dl, 3.5f, 2.5f));

    /* half open is already walk-through: door_is_blocking tests < 0.5 */
    dl.doors[0].openness = 0.5f;
    TEST_ASSERT_EQUAL_INT(0, map_is_wall_door(&map, &dl, 3.5f, 2.5f));

    dl.doors[0].openness = 0.49f;
    TEST_ASSERT_EQUAL_INT(1, map_is_wall_door(&map, &dl, 3.5f, 2.5f));

    dl.doors[0].openness = 1.0f;
    TEST_ASSERT_EQUAL_INT(0, map_is_wall_door(&map, &dl, 3.5f, 2.5f));

    /* a real wall is unaffected by the door list */
    TEST_ASSERT_EQUAL_INT(1, map_is_wall_door(&map, &dl, 0.5f, 0.5f));
    TEST_ASSERT_EQUAL_INT(0, map_is_wall_door(&map, &dl, 1.5f, 1.5f));
}

/* ---- spawn points ---- */

/* 's' is a spawn point at the centre of its cell, the cell itself walkable,
 * taken in reading order. Each faces along the longest clear line out of it:
 * down the corridor, not into the wall beside it.
 *   0123456789
 * 0 ##########
 * 1 #s......s#
 * 2 ########.#   ... down to row 9
 * 10##########
 */
static void test_map_load_reads_spawn_points_facing_open_space(void)
{
    static const char *const rows[] = {
            "##########", "#s......s#", "########.#", "########.#", "########.#", "########.#",
            "########.#", "########.#", "########.#", "########.#", "##########",
    };
    Map m;
    LOAD_ROWS(&m, rows);
    TEST_ASSERT_EQUAL_INT(2, m.spawn_count);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, m.spawns[0].x);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, m.spawns[0].y);
    TEST_ASSERT_EQUAL_FLOAT(8.5f, m.spawns[1].x);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, m.spawns[1].y);
    TEST_ASSERT_EQUAL_INT(0, map_cell(&m, 1, 1));
    TEST_ASSERT_EQUAL_INT(0, map_cell(&m, 8, 1));

    /* East along row 1, seven cells; south down column 8, eight, which beats
     * the seven back west. */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.spawns[0].angle);
    TEST_ASSERT_EQUAL_FLOAT(MATH_PI / 2.0f, m.spawns[1].angle);
}

/* Past MAX_SPAWNS the markers are dropped, with a warning, and the cells are
 * still floor. */
static void test_map_load_keeps_at_most_max_spawns(void)
{
    static const char *const rows[] = {
            "#################",
            "#sssssssssssssss#",
            "#sssssssssssssss#",
            "#################",
    };
    Map m;
    LOAD_ROWS(&m, rows);
    TEST_ASSERT_TRUE(30 > MAX_SPAWNS);
    TEST_ASSERT_EQUAL_INT(MAX_SPAWNS, m.spawn_count);
    TEST_ASSERT_EQUAL_INT(0, map_cell(&m, 15, 2));
}

/* With no 's' the pool is made of the free cells reachable from the start,
 * through a door too: twice as many points as players, the start first, none
 * in the sealed pocket, on the door or under a pickup.
 *   0123456789012
 * 0 #############
 * 1 #P....#.....#
 * 2 #.....D..M..#
 * 3 #.....#.....#
 * 4 #######.....#
 * 5 #...#########   <- sealed
 * 6 #############
 */
static void test_map_load_makes_a_pool_when_the_map_has_none(void)
{
    static const char *const rows[] = {
            "#############", "#P....#.....#", "#.....D..M..#", "#.....#.....#",
            "#######.....#", "#...#########", "#############",
    };
    Map m;
    LOAD_ROWS(&m, rows);
    TEST_ASSERT_EQUAL_INT(2 * MAX_PLAYERS, m.spawn_count);
    TEST_ASSERT_EQUAL_FLOAT(m.start.x, m.spawns[0].x);
    TEST_ASSERT_EQUAL_FLOAT(m.start.y, m.spawns[0].y);

    int beyond_door = 0;
    for (int i = 0; i < m.spawn_count; i++) {
        int cx = (int)m.spawns[i].x, cy = (int)m.spawns[i].y;
        TEST_ASSERT_EQUAL_FLOAT(cx + 0.5f, m.spawns[i].x);
        TEST_ASSERT_EQUAL_FLOAT(cy + 0.5f, m.spawns[i].y);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, map_cell(&m, cx, cy), "a point on a wall or a door");
        TEST_ASSERT_NOT_EQUAL_MESSAGE(5, cy, "a point in the sealed pocket");
        TEST_ASSERT_FALSE_MESSAGE(cx == 9 && cy == 2, "a point under the medkit");
        beyond_door += cx > 6;
        for (int k = 0; k < i; k++) {
            TEST_ASSERT_FALSE(m.spawns[k].x == m.spawns[i].x && m.spawns[k].y == m.spawns[i].y);
        }
    }
    TEST_ASSERT_TRUE(beyond_door > 0);
}

/* Each next point is the cell farthest from those already taken: from the
 * start in one corner of a room, the opposite corner. */
static void test_map_pool_takes_the_farthest_cell_next(void)
{
    static const char *const rows[] = {
            "########", "#P.....#", "#......#", "#......#", "#......#", "########",
    };
    Map m;
    LOAD_ROWS(&m, rows);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, m.spawns[0].x);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, m.spawns[0].y);
    TEST_ASSERT_EQUAL_FLOAT(6.5f, m.spawns[1].x);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, m.spawns[1].y);
    /* No cell is farther than three from both corners, and the third point is
     * one of those that are. */
    for (int i = 0; i < 2; i++) {
        float dx = m.spawns[2].x - m.spawns[i].x, dy = m.spawns[2].y - m.spawns[i].y;
        TEST_ASSERT_TRUE(sqrtf(dx * dx + dy * dy) >= 3.0f);
    }
}

/* A map with fewer free cells than the pool wants has every one of them in
 * it but the one under a pickup; one without a 'P' starts from its first free
 * cell; one with no floor at all has no pool. */
static void test_map_pool_on_small_maps(void)
{
    static const char *const tiny[] = {"#####", "#P..#", "#####"};
    static const char *const with_item[] = {"#####", "#PM.#", "#####"};
    static const char *const no_start[] = {"#####", "#...#", "#####"};
    static const char *const solid[] = {"#####", "#####"};
    Map m;

    LOAD_ROWS(&m, tiny);
    TEST_ASSERT_EQUAL_INT(3, m.spawn_count);

    LOAD_ROWS(&m, with_item);
    TEST_ASSERT_EQUAL_INT(2, m.spawn_count);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, m.spawns[1].x);

    LOAD_ROWS(&m, no_start);
    TEST_ASSERT_EQUAL_INT(3, m.spawn_count);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, m.spawns[0].x);

    LOAD_ROWS(&m, solid);
    TEST_ASSERT_EQUAL_INT(0, m.spawn_count);
}

/* A facing never runs through the corner where two walls meet. From (2, 1)
 * south-east would be the longest line, three diagonal steps, but its first
 * one squeezes between (3, 1) and (2, 2); west, one cell, is all there is.
 *   01234567
 * 0 ########
 * 1 #.s#...#
 * 2 #.#....#
 * 3 #......#
 * 4 #......#
 * 5 ######## */
static void test_map_spawn_facing_does_not_slip_through_a_corner(void)
{
    static const char *const rows[] = {
            "########", "#.s#...#", "#.#....#", "#......#", "#......#", "########",
    };
    Map m;
    LOAD_ROWS(&m, rows);
    TEST_ASSERT_EQUAL_INT(1, m.spawn_count);
    TEST_ASSERT_EQUAL_FLOAT(MATH_PI, m.spawns[0].angle);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_map_load_fills_the_full_grid);
    RUN_TEST(test_map_load_reads_walls_and_floor);
    RUN_TEST(test_map_load_reads_the_door_cell);
    RUN_TEST(test_map_load_reads_the_player_start);
    RUN_TEST(test_map_load_collects_sprite_spawns);
    RUN_TEST(test_map_load_pads_short_rows_and_missing_rows);
    RUN_TEST(test_map_load_strips_crlf);
    RUN_TEST(test_map_load_rejects_a_missing_file);
    RUN_TEST(test_map_cell_outside_the_grid_is_wall);
    RUN_TEST(test_map_is_wall_uses_the_containing_cell);
    RUN_TEST(test_map_is_wall_door_without_doors_blocks);
    RUN_TEST(test_map_is_wall_door_follows_openness);
    RUN_TEST(test_map_load_reads_spawn_points_facing_open_space);
    RUN_TEST(test_map_load_keeps_at_most_max_spawns);
    RUN_TEST(test_map_load_makes_a_pool_when_the_map_has_none);
    RUN_TEST(test_map_pool_takes_the_farthest_cell_next);
    RUN_TEST(test_map_pool_on_small_maps);
    RUN_TEST(test_map_spawn_facing_does_not_slip_through_a_corner);
    return UNITY_END();
}
