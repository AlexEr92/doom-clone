/* player.c: movement, wall/door collision and turning. The map is built in
 * memory so the test needs neither the working directory nor the level file. */

#include "unity.h"
#include "player.h"
#include "input.h"
#include "door.h"
#include "map.h"
#include <math.h>
#include <string.h>

/* weapon.c is not linked: it references the screen-space hitscan's zBuffer and
 * enemy_damage(), neither of which has anything to do with movement. What
 * player_init() has to do is call this at all — the weapon system lives inside
 * the player. What it fills in is test_weapon.c's business. */
static int weapon_system_init_calls;

void weapon_system_init(WeaponSystem *ws)
{
    memset(ws, 0, sizeof(*ws));
    weapon_system_init_calls++;
}

#define TEST_STEP (1.0 / 60.0)
#define TEST_PI 3.14159265358979323846f

#define DOOR_CELL_X 4
#define DOOR_CELL_Y 3

static Map map;
static DoorList doors;
static PlayerState player;
static InputState in;

/*  0 1 2 3 4 5 6 7
 * 0# # # # # # # #
 * 1# . . . . . . #
 * 2# . . . . . . #
 * 3# . . . D . . #
 * 4# . . . . . . #
 * 5# . . . # . . #
 * 6# . . . . . . #
 * 7# # # # # # # # */
static void build_map(Map *m)
{
    memset(m, 0, sizeof(*m));
    m->w = 8;
    m->h = 8;
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            int border = (x == 0 || y == 0 || x == m->w - 1 || y == m->h - 1);
            m->cells[y][x] = border ? 1 : 0;
        }
    }
    m->cells[DOOR_CELL_Y][DOOR_CELL_X] = 2;
    m->cells[5][4] = 1;
    m->start_x = 1;
    m->start_y = 1;
}

static void step(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        player_update(&player, &map, &doors, &in, TEST_STEP);
    }
}

void setUp(void)
{
    build_map(&map);
    door_list_init(&doors);
    door_discover(&doors, &map);
    weapon_system_init_calls = 0;
    player_init(&player, map.start_x, map.start_y);
    memset(&in, 0, sizeof(in));
}

void tearDown(void)
{}

static void test_player_init_centres_the_player_in_the_start_cell(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.5f, player.x);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, player.y);
    TEST_ASSERT_EQUAL_FLOAT(TEST_PI, player.angle);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, player.hp);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, player.armor);
    TEST_ASSERT_EQUAL_INT(1, player.alive);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, player.respawn_timer);
    TEST_ASSERT_EQUAL_UINT8(0, player.id);
    /* the weapon system belongs to the player and is initialised with them */
    TEST_ASSERT_EQUAL_INT(1, weapon_system_init_calls);
}

static void test_idle_input_does_not_move_the_player(void)
{
    player.x = 3.5f;
    player.y = 3.5f;
    player.angle = 0.0f;
    step(60);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, player.x);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, player.y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, player.angle);
}

static void test_forward_and_back_follow_the_facing_angle(void)
{
    player.x = 3.5f;
    player.y = 4.5f;
    player.angle = 0.0f; /* +X */

    in.forward = 1;
    step(1);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.5f + MOVE_SPEED * (float)TEST_STEP, player.x);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, player.y);

    in.forward = 0;
    in.back = 1;
    step(1);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.5f, player.x);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, player.y);
}

/* Strafing is perpendicular to the facing angle. Its speed is deliberately not
 * asserted: it is FOV-scaled today, which is bug-005. */
static void test_strafe_moves_sideways(void)
{
    player.x = 3.5f;
    player.y = 4.5f;
    player.angle = 0.0f; /* +X, so sideways is Y */

    in.strafe_left = 1;
    step(1);
    TEST_ASSERT_TRUE(player.y > 4.5f);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, player.x);

    float left_y = player.y;
    in.strafe_left = 0;
    in.strafe_right = 1;
    step(2);
    TEST_ASSERT_TRUE(player.y < left_y);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, player.x);
}

static void test_turning_changes_the_angle(void)
{
    player.angle = 0.0f;

    in.turn_left = 1;
    step(1);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, ROT_SPEED * (float)TEST_STEP, player.angle);

    in.turn_left = 0;
    in.turn_right = 1;
    step(1);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, player.angle);
}

static void test_mouse_look_turns_the_other_way_round(void)
{
    player.angle = 0.0f;
    in.mouse_dx = 100;
    step(1);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -100.0f * MOUSE_SENS, player.angle);
}

/* Without wrapping, the angle drifts out of [-PI, PI] and the shortest-arc
 * interpolation between snapshots would take the long way round. */
static void test_angle_stays_wrapped(void)
{
    player.angle = TEST_PI - 0.01f;
    in.turn_left = 1;
    step(600);
    TEST_ASSERT_TRUE(player.angle >= -TEST_PI);
    TEST_ASSERT_TRUE(player.angle <= TEST_PI);

    in.turn_left = 0;
    in.turn_right = 1;
    step(1200);
    TEST_ASSERT_TRUE(player.angle >= -TEST_PI);
    TEST_ASSERT_TRUE(player.angle <= TEST_PI);
}

static void test_player_cannot_walk_into_a_wall(void)
{
    player.x = 1.5f;
    player.y = 1.5f;
    player.angle = TEST_PI; /* -X, straight at the west wall */

    in.forward = 1;
    step(240);

    TEST_ASSERT_EQUAL_INT(0, map_is_wall(&map, player.x, player.y));
    TEST_ASSERT_TRUE(player.x - PLAYER_RADIUS >= 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, player.y);
}

/* The two axes are resolved independently, so a body pressed against a wall
 * keeps sliding along it instead of sticking. */
static void test_player_slides_along_a_wall(void)
{
    player.x = 3.5f;
    player.y = 1.2f;
    player.angle = -TEST_PI / 4.0f; /* into the north wall at 45 degrees */

    in.forward = 1;
    step(60);

    TEST_ASSERT_EQUAL_FLOAT(1.2f, player.y);
    TEST_ASSERT_TRUE(player.x > 3.5f);
    TEST_ASSERT_EQUAL_INT(0, map_is_wall(&map, player.x, player.y));
}

static void test_a_closed_door_blocks_the_player(void)
{
    player.x = DOOR_CELL_X + 0.5f;
    player.y = DOOR_CELL_Y - 0.8f;
    player.angle = TEST_PI / 2.0f; /* +Y, at the door */

    in.forward = 1;
    step(240);

    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, doors.doors[0].state);
    TEST_ASSERT_TRUE(player.y + PLAYER_RADIUS <= (float)DOOR_CELL_Y);
}

static void test_an_open_door_lets_the_player_through(void)
{
    player.x = DOOR_CELL_X + 0.5f;
    player.y = DOOR_CELL_Y - 0.8f;
    player.angle = TEST_PI / 2.0f;

    doors.doors[0].state = DOOR_OPEN;
    doors.doors[0].openness = 1.0f;

    in.forward = 1;
    step(240);

    TEST_ASSERT_TRUE(player.y > (float)(DOOR_CELL_Y + 1));
}

/* With no door list there is nothing to open, so the door cell is solid. */
static void test_without_a_door_list_the_door_is_solid(void)
{
    player.x = DOOR_CELL_X + 0.5f;
    player.y = DOOR_CELL_Y - 0.8f;
    player.angle = TEST_PI / 2.0f;

    in.forward = 1;
    for (int i = 0; i < 240; i++) {
        player_update(&player, &map, NULL, &in, TEST_STEP);
    }
    TEST_ASSERT_TRUE(player.y + PLAYER_RADIUS <= (float)DOOR_CELL_Y);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_player_init_centres_the_player_in_the_start_cell);
    RUN_TEST(test_idle_input_does_not_move_the_player);
    RUN_TEST(test_forward_and_back_follow_the_facing_angle);
    RUN_TEST(test_strafe_moves_sideways);
    RUN_TEST(test_turning_changes_the_angle);
    RUN_TEST(test_mouse_look_turns_the_other_way_round);
    RUN_TEST(test_angle_stays_wrapped);
    RUN_TEST(test_player_cannot_walk_into_a_wall);
    RUN_TEST(test_player_slides_along_a_wall);
    RUN_TEST(test_a_closed_door_blocks_the_player);
    RUN_TEST(test_an_open_door_lets_the_player_through);
    RUN_TEST(test_without_a_door_list_the_door_is_solid);
    return UNITY_END();
}
