/* player.c: movement, wall/door collision and the facing it takes from the
 * input. Turning itself is the client's (input_turn(), tests/test_input.c):
 * PlayerInput has no mouse_dx or turn keys for player_update() to read. The
 * map is built in memory so the test needs neither the working directory nor
 * the level file. */

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
#define START_CELL 1 /* the player starts in cell (1, 1) */

static Map map;
static DoorList doors;
static PlayerState player;
static PlayerInput in;

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
}

/* The lone player is passed the array they live in — themselves and nobody
 * else — which is what a one-player world looks like. */
static void step(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        player_update(&player, &map, &doors, &in, &player, 1, TEST_STEP);
    }
}

static float distance(const PlayerState *a, const PlayerState *b)
{
    float dx = a->x - b->x, dy = a->y - b->y;
    return sqrtf(dx * dx + dy * dy);
}

void setUp(void)
{
    build_map(&map);
    door_list_init(&doors);
    door_discover(&doors, &map);
    weapon_system_init_calls = 0;
    player_init(&player, START_CELL, START_CELL);
    memset(&in, 0, sizeof(in));
    in.angle = player.angle;
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
    TEST_ASSERT_EQUAL_INT(1, player.active);
    /* the weapon system belongs to the player and is initialised with them */
    TEST_ASSERT_EQUAL_INT(1, weapon_system_init_calls);
}

static void test_idle_input_does_not_move_the_player(void)
{
    player.x = 3.5f;
    player.y = 3.5f;
    in.angle = 0.0f;
    step(60);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, player.x);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, player.y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, player.angle);
}

/* The player spawned facing -X: walking +X on the very first tick means the
 * step follows the angle of this tick's input, not the facing it had before. */
static void test_forward_and_back_follow_the_facing_angle(void)
{
    player.x = 3.5f;
    player.y = 4.5f;
    in.angle = 0.0f; /* +X */

    in.buttons = BTN_FORWARD;
    step(1);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.5f + MOVE_SPEED * (float)TEST_STEP, player.x);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, player.y);

    in.buttons = BTN_BACK;
    step(1);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.5f, player.x);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, player.y);
}

/* Strafing is perpendicular to the facing angle and exactly as fast as walking,
 * either way: the speed is a movement constant, not the length of the camera
 * plane, which is the field of view. */
static void test_strafe_moves_sideways_as_fast_as_walking(void)
{
    player.x = 3.5f;
    player.y = 4.5f;
    in.angle = 0.0f; /* +X, so left is +Y */

    in.buttons = BTN_LEFT;
    step(1);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 4.5f + MOVE_SPEED * (float)TEST_STEP, player.y);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, player.x);

    in.buttons = BTN_RIGHT;
    step(2);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 4.5f - MOVE_SPEED * (float)TEST_STEP, player.y);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, player.x);
}

/* A wall stops a sidestep exactly where it stops a step forward, on either
 * side: from one start, walking into the south wall and strafing into it with
 * the wall on the left or on the right all end at the same distance from it. */
static void test_strafe_stops_at_a_wall_like_walking(void)
{
    player.x = 2.5f;
    player.y = 5.5f;
    in.angle = TEST_PI / 2.0f; /* +Y, facing the south wall */
    in.buttons = BTN_FORWARD;
    step(240);
    float walked_y = player.y;
    TEST_ASSERT_TRUE(walked_y > 6.5f);
    TEST_ASSERT_TRUE(walked_y + PLAYER_RADIUS <= 7.0f);

    player.x = 2.5f;
    player.y = 5.5f;
    in.angle = 0.0f; /* +X: the south wall is on the left */
    in.buttons = BTN_LEFT;
    step(240);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, walked_y, player.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.5f, player.x);

    player.x = 2.5f;
    player.y = 5.5f;
    in.angle = TEST_PI; /* -X: the south wall is on the right */
    in.buttons = BTN_RIGHT;
    step(240);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, walked_y, player.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.5f, player.x);
}

/* The facing is the input's, taken as given, and nothing else turns the
 * player: an idle tick keeps whatever angle the input carries. */
static void test_the_player_faces_the_angle_of_the_input(void)
{
    in.angle = 0.7f;
    step(1);
    TEST_ASSERT_EQUAL_FLOAT(0.7f, player.angle);

    in.angle = -2.0f;
    step(30);
    TEST_ASSERT_EQUAL_FLOAT(-2.0f, player.angle);
}

/* The angle comes off the wire, and nothing guarantees the client wrapped it.
 * Left unwrapped, the shortest-arc interpolation between snapshots would take
 * the long way round. */
static void test_angle_stays_wrapped(void)
{
    in.angle = 3.0f * TEST_PI + 0.5f;
    step(1);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -TEST_PI + 0.5f, player.angle);

    in.angle = -5.0f * TEST_PI - 0.5f;
    step(1);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, TEST_PI - 0.5f, player.angle);
}

static void test_player_cannot_walk_into_a_wall(void)
{
    player.x = 1.5f;
    player.y = 1.5f;
    in.angle = TEST_PI; /* -X, straight at the west wall */

    in.buttons = BTN_FORWARD;
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
    in.angle = -TEST_PI / 4.0f; /* into the north wall at 45 degrees */

    in.buttons = BTN_FORWARD;
    step(60);

    TEST_ASSERT_EQUAL_FLOAT(1.2f, player.y);
    TEST_ASSERT_TRUE(player.x > 3.5f);
    TEST_ASSERT_EQUAL_INT(0, map_is_wall(&map, player.x, player.y));
}

static void test_a_closed_door_blocks_the_player(void)
{
    player.x = DOOR_CELL_X + 0.5f;
    player.y = DOOR_CELL_Y - 0.8f;
    in.angle = TEST_PI / 2.0f; /* +Y, at the door */

    in.buttons = BTN_FORWARD;
    step(240);

    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, doors.doors[0].state);
    TEST_ASSERT_TRUE(player.y + PLAYER_RADIUS <= (float)DOOR_CELL_Y);
}

static void test_an_open_door_lets_the_player_through(void)
{
    player.x = DOOR_CELL_X + 0.5f;
    player.y = DOOR_CELL_Y - 0.8f;
    in.angle = TEST_PI / 2.0f;

    doors.doors[0].state = DOOR_OPEN;
    doors.doors[0].openness = 1.0f;

    in.buttons = BTN_FORWARD;
    step(240);

    TEST_ASSERT_TRUE(player.y > (float)(DOOR_CELL_Y + 1));
}

/* With no door list there is nothing to open, so the door cell is solid. */
static void test_without_a_door_list_the_door_is_solid(void)
{
    player.x = DOOR_CELL_X + 0.5f;
    player.y = DOOR_CELL_Y - 0.8f;
    in.angle = TEST_PI / 2.0f;

    in.buttons = BTN_FORWARD;
    for (int i = 0; i < 240; i++) {
        player_update(&player, &map, NULL, &in, &player, 1, TEST_STEP);
    }
    TEST_ASSERT_TRUE(player.y + PLAYER_RADIUS <= (float)DOOR_CELL_Y);
}

/* Another player is a solid body: walking at one closes the gap down to two
 * radii and stops there, and the one standing still is not pushed. */
static void test_a_second_player_blocks_the_way(void)
{
    PlayerState two[2];
    player_init(&two[0], 2, 4);
    player_init(&two[1], 4, 4);
    two[1].id = 1;
    in.angle = 0.0f; /* +X, straight at the other one */

    in.buttons = BTN_FORWARD;
    for (int i = 0; i < 240; i++) {
        player_update(&two[0], &map, &doors, &in, two, 2, TEST_STEP);
        TEST_ASSERT_TRUE(distance(&two[0], &two[1]) >= 2.0f * PLAYER_RADIUS - 1e-4f);
    }

    TEST_ASSERT_TRUE(two[0].x > 2.5f); /* it did walk up to them */
    TEST_ASSERT_TRUE(two[0].x < 4.5f); /* and not through them */
    TEST_ASSERT_EQUAL_FLOAT(4.5f, two[1].x);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, two[1].y);
}

/* A player who has left their slot is not a wall: the body goes with them. */
static void test_an_inactive_player_does_not_block(void)
{
    PlayerState two[2];
    player_init(&two[0], 2, 4);
    player_init(&two[1], 4, 4);
    two[1].id = 1;
    two[1].active = 0;
    in.angle = 0.0f;

    in.buttons = BTN_FORWARD;
    for (int i = 0; i < 240; i++) {
        player_update(&two[0], &map, &doors, &in, two, 2, TEST_STEP);
    }
    TEST_ASSERT_TRUE(two[0].x > 4.5f);
}

/* Alone in the world, movement is what it was before anyone else could get in
 * the way: passing the one-element array and passing none agree exactly. */
static void test_a_lone_player_moves_as_without_the_array(void)
{
    player.x = 3.5f;
    player.y = 4.5f;
    in.angle = 0.6f;
    in.buttons = BTN_FORWARD | BTN_LEFT;
    step(30);
    float alone_x = player.x, alone_y = player.y;

    player.x = 3.5f;
    player.y = 4.5f;
    for (int i = 0; i < 30; i++) {
        player_update(&player, &map, &doors, &in, NULL, 0, TEST_STEP);
    }
    TEST_ASSERT_EQUAL_FLOAT(alone_x, player.x);
    TEST_ASSERT_EQUAL_FLOAT(alone_y, player.y);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_player_init_centres_the_player_in_the_start_cell);
    RUN_TEST(test_idle_input_does_not_move_the_player);
    RUN_TEST(test_forward_and_back_follow_the_facing_angle);
    RUN_TEST(test_strafe_moves_sideways_as_fast_as_walking);
    RUN_TEST(test_strafe_stops_at_a_wall_like_walking);
    RUN_TEST(test_the_player_faces_the_angle_of_the_input);
    RUN_TEST(test_angle_stays_wrapped);
    RUN_TEST(test_player_cannot_walk_into_a_wall);
    RUN_TEST(test_player_slides_along_a_wall);
    RUN_TEST(test_a_closed_door_blocks_the_player);
    RUN_TEST(test_an_open_door_lets_the_player_through);
    RUN_TEST(test_without_a_door_list_the_door_is_solid);
    RUN_TEST(test_a_second_player_blocks_the_way);
    RUN_TEST(test_an_inactive_player_does_not_block);
    RUN_TEST(test_a_lone_player_moves_as_without_the_array);
    return UNITY_END();
}
