/* map.c: parsing the ASCII grid, and the wall tests built on it. The map is
 * written to a temporary file by the test itself rather than read from
 * assets/maps/level1.txt, so neither the working directory nor later edits to
 * the level can break it. */

#include "unity.h"
#include "map.h"
#include "door.h"
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

/* 'P' marks the start and leaves an empty cell behind. */
static void test_map_load_reads_the_player_start(void)
{
    TEST_ASSERT_EQUAL_INT(1, map.start_x);
    TEST_ASSERT_EQUAL_INT(1, map.start_y);
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
    TEST_ASSERT_EQUAL_INT(1, map.start_x);
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
    return UNITY_END();
}
