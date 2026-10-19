/* assets/maps/: the maps the game ships, read from the source tree — unlike the
 * other tests, which write maps of their own, these are about the files
 * themselves, and an edit to one that breaks what it promises fails here.
 *
 * dm1.txt is the deathmatch map a server plays by default: 48x48, no enemies,
 * twice as many spawn points as players, none of them in sight of another,
 * each facing open floor, and every free cell reachable with every door shut —
 * no door is the only way anywhere. level1.txt is the single-player map. */

#include "unity.h"
#include "world.h"
#include "raycast_world.h"
#include <math.h>
#include <string.h>

/* The source tree's assets/maps/, from tests/CMakeLists.txt: CTest runs the
 * test in the build directory. */
#ifndef MAPS_DIR
#error "MAPS_DIR must name the source tree's assets/maps/"
#endif

/* Static: a World is tens of kilobytes, too much for the stack of a test. */
static World w;

void setUp(void)
{}

void tearDown(void)
{}

static void load(const char *name)
{
    char path[512];
    strcpy(path, MAPS_DIR);
    strcat(path, name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, world_load(&w, path), path);
}

static void test_dm1_is_a_deathmatch_map_for_ten(void)
{
    load("dm1.txt");
    TEST_ASSERT_EQUAL_INT(48, w.map.w);
    TEST_ASSERT_EQUAL_INT(48, w.map.h);
    TEST_ASSERT_EQUAL_INT(0, w.enemies.count);
    TEST_ASSERT_TRUE(w.map.spawn_count >= 2 * MAX_PLAYERS);
    TEST_ASSERT_TRUE(w.items.count > 0);
}

/* As spawn_view counts them: a pair is in sight if either point sees the
 * other, since a trace grazing a corner can get through one way only. Every
 * door is shut, as on a fresh map. */
static void test_dm1_spawn_points_are_out_of_sight_of_each_other(void)
{
    load("dm1.txt");
    for (int i = 0; i < w.map.spawn_count; i++) {
        const SpawnPoint *a = &w.map.spawns[i];
        for (int k = i + 1; k < w.map.spawn_count; k++) {
            const SpawnPoint *b = &w.map.spawns[k];
            char what[64];
            snprintf(what, sizeof(what), "points %d and %d see each other", i, k);
            TEST_ASSERT_FALSE_MESSAGE(world_line_of_sight(&w.map, &w.doors, a->x, a->y, b->x, b->y),
                                      what);
            TEST_ASSERT_FALSE_MESSAGE(world_line_of_sight(&w.map, &w.doors, b->x, b->y, a->x, a->y),
                                      what);
        }
    }
}

static void test_dm1_spawn_points_face_open_floor(void)
{
    load("dm1.txt");
    for (int i = 0; i < w.map.spawn_count; i++) {
        const SpawnPoint *sp = &w.map.spawns[i];
        int fx = (int)sp->x + (int)lroundf(cosf(sp->angle));
        int fy = (int)sp->y + (int)lroundf(sinf(sp->angle));
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, map_cell(&w.map, fx, fy), "a point facing a wall");
    }
}

/* A flood fill from the first spawn point over floor alone, doors counted as
 * wall, reaches every free cell of the map. */
static void test_dm1_every_cell_is_reachable_with_every_door_shut(void)
{
    static int seen[MAP_MAX_H][MAP_MAX_W];
    static int queue[MAP_MAX_W * MAP_MAX_H];
    static const int STEPS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    load("dm1.txt");
    memset(seen, 0, sizeof(seen));
    int head = 0, tail = 0;
    int sx = (int)w.map.spawns[0].x, sy = (int)w.map.spawns[0].y;
    seen[sy][sx] = 1;
    queue[tail++] = sy * MAP_MAX_W + sx;
    while (head < tail) {
        int x = queue[head] % MAP_MAX_W, y = queue[head] / MAP_MAX_W;
        head++;
        for (int i = 0; i < 4; i++) {
            int nx = x + STEPS[i][0], ny = y + STEPS[i][1];
            if (map_cell(&w.map, nx, ny) == 0 && !seen[ny][nx]) {
                seen[ny][nx] = 1;
                queue[tail++] = ny * MAP_MAX_W + nx;
            }
        }
    }
    int doors = 0;
    for (int y = 0; y < w.map.h; y++) {
        for (int x = 0; x < w.map.w; x++) {
            doors += map_cell(&w.map, x, y) == 2;
            if (map_cell(&w.map, x, y) == 0 && !seen[y][x]) {
                char what[48];
                snprintf(what, sizeof(what), "cell (%d, %d) is cut off", x, y);
                TEST_FAIL_MESSAGE(what);
            }
        }
    }
    TEST_ASSERT_TRUE(doors > 0);
}

/* The single-player map: a start on free floor, and enemies to clear. */
static void test_level1_is_a_map_to_play_alone(void)
{
    load("level1.txt");
    TEST_ASSERT_EQUAL_INT(0, map_cell(&w.map, (int)w.map.start.x, (int)w.map.start.y));
    TEST_ASSERT_TRUE(w.enemies.count > 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dm1_is_a_deathmatch_map_for_ten);
    RUN_TEST(test_dm1_spawn_points_are_out_of_sight_of_each_other);
    RUN_TEST(test_dm1_spawn_points_face_open_floor);
    RUN_TEST(test_dm1_every_cell_is_reachable_with_every_door_shut);
    RUN_TEST(test_level1_is_a_map_to_play_alone);
    return UNITY_END();
}
