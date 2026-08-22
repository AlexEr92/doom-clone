/* door.c: the open/close timer, the doorway-occupied rule and the use range.
 *
 * The map is built in memory rather than loaded from assets/maps/level1.txt,
 * so these tests depend on neither the working directory nor the level. The
 * step is 1/60 s, as in the game.
 *
 * Timing trap: DOOR_OPEN_TIME starts counting when openness reaches 1.0, not
 * when door_try_use() is called — the ramp at DOOR_SPEED eats part of the
 * interval first. Asserts here therefore bracket the interval instead of
 * landing on its edge. */

#include "unity.h"
#include "door.h"
#include "map.h"
#include "player.h"
#include <string.h>

/* Mirrors of the constants private to door.c. */
#define TEST_DOOR_OPEN_TIME 4.0f
#define TEST_DOOR_SPEED 2.0f
#define TEST_USE_RANGE 1.2f

#define TEST_STEP (1.0 / 60.0)
#define TEST_PI 3.14159265358979323846f

/* Time the ramp takes at DOOR_SPEED, plus a tick of slack. */
#define TEST_RAMP_TIME (1.0f / TEST_DOOR_SPEED)

#define DOOR_CELL_X 2
#define DOOR_CELL_Y 2

static Map map;
static DoorList doors;

/*  0 1 2 3 4 5
 * 0# # # # # #
 * 1# . . . . #
 * 2# . D . . #
 * 3# . . . . #
 * 4# . . . . #
 * 5# # # # # # */
static void build_map(Map *m)
{
    memset(m, 0, sizeof(*m));
    m->w = 6;
    m->h = 6;
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            int border = (x == 0 || y == 0 || x == m->w - 1 || y == m->h - 1);
            m->cells[y][x] = border ? 1 : 0;
        }
    }
    m->cells[DOOR_CELL_Y][DOOR_CELL_X] = 2;
}

static int ticks_for(float seconds)
{
    return (int)(seconds / (float)TEST_STEP) + 1;
}

static void run_ticks(const DoorOccupant *occ, int occ_count, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        door_update_all(&doors, occ, occ_count, TEST_STEP);
    }
}

/* A player one cell south of the door, facing it. */
static PlayerState player_facing_door(void)
{
    PlayerState p;
    memset(&p, 0, sizeof(p));
    p.x = DOOR_CELL_X + 0.5f;
    p.y = DOOR_CELL_Y - 0.5f;
    p.angle = TEST_PI / 2.0f; /* +Y, towards the door */
    return p;
}

/* Bring the door to DOOR_OPEN with nobody standing in it. */
static void open_the_door(void)
{
    PlayerState p = player_facing_door();
    door_try_use(&doors, &p, &map);
    run_ticks(NULL, 0, ticks_for(TEST_RAMP_TIME));
    TEST_ASSERT_EQUAL_INT(DOOR_OPEN, doors.doors[0].state);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, doors.doors[0].openness);
}

void setUp(void)
{
    build_map(&map);
    door_list_init(&doors);
    TEST_ASSERT_EQUAL_INT(1, door_discover(&doors, &map));
}

void tearDown(void)
{}

static void test_door_discover_registers_every_door_cell(void)
{
    Map m;
    DoorList dl;
    build_map(&m);
    m.cells[1][3] = 2;
    m.cells[4][1] = 2;

    door_list_init(&dl);
    TEST_ASSERT_EQUAL_INT(3, door_discover(&dl, &m));

    /* scanned row by row, so the order follows the grid */
    TEST_ASSERT_EQUAL_INT(3, dl.doors[0].cellx);
    TEST_ASSERT_EQUAL_INT(1, dl.doors[0].celly);
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, dl.doors[0].state);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dl.doors[0].openness);

    TEST_ASSERT_EQUAL_INT(0, door_at(&dl, 3, 1));
    TEST_ASSERT_EQUAL_INT(-1, door_at(&dl, 1, 1));

    /* a cell with no door never blocks, whatever the door list says */
    TEST_ASSERT_EQUAL_INT(0, door_is_blocking(&dl, 1, 1));
}

static void test_door_is_blocking_below_half_open(void)
{
    TEST_ASSERT_EQUAL_INT(1, door_is_blocking(&doors, DOOR_CELL_X, DOOR_CELL_Y));
    doors.doors[0].openness = 0.49f;
    TEST_ASSERT_EQUAL_INT(1, door_is_blocking(&doors, DOOR_CELL_X, DOOR_CELL_Y));
    doors.doors[0].openness = 0.5f;
    TEST_ASSERT_EQUAL_INT(0, door_is_blocking(&doors, DOOR_CELL_X, DOOR_CELL_Y));
    doors.doors[0].openness = 1.0f;
    TEST_ASSERT_EQUAL_INT(0, door_is_blocking(&doors, DOOR_CELL_X, DOOR_CELL_Y));
}

static void test_door_try_use_toggles_the_state(void)
{
    PlayerState p = player_facing_door();

    door_try_use(&doors, &p, &map);
    TEST_ASSERT_EQUAL_INT(DOOR_OPENING, doors.doors[0].state);
    TEST_ASSERT_EQUAL_INT(1, doors.doors[0].triggered);

    /* using it mid-ramp reverses the direction */
    door_try_use(&doors, &p, &map);
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSING, doors.doors[0].state);

    door_try_use(&doors, &p, &map);
    TEST_ASSERT_EQUAL_INT(DOOR_OPENING, doors.doors[0].state);

    doors.doors[0].state = DOOR_OPEN;
    doors.doors[0].timer = TEST_DOOR_OPEN_TIME;
    door_try_use(&doors, &p, &map);
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSING, doors.doors[0].state);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, doors.doors[0].timer);

    /* triggered is a one-tick flag */
    run_ticks(NULL, 0, 1);
    TEST_ASSERT_EQUAL_INT(0, doors.doors[0].triggered);
}

static void test_door_try_use_out_of_range_does_nothing(void)
{
    PlayerState p = player_facing_door();
    p.y = DOOR_CELL_Y + 2.5f; /* well beyond USE_RANGE, and facing away */
    p.x = 4.5f;
    door_try_use(&doors, &p, &map);
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, doors.doors[0].state);
    TEST_ASSERT_EQUAL_INT(0, doors.doors[0].triggered);

    /* the cell in front is one USE_RANGE away, so just short of it misses */
    p.x = DOOR_CELL_X + 0.5f;
    p.y = DOOR_CELL_Y - 0.5f - TEST_USE_RANGE;
    p.angle = TEST_PI / 2.0f;
    door_try_use(&doors, &p, &map);
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, doors.doors[0].state);
}

/* Standing in the doorway and facing away still opens it: door_try_use falls
 * back to the cell the player occupies. */
static void test_door_try_use_from_inside_the_doorway(void)
{
    PlayerState p = player_facing_door();
    p.x = DOOR_CELL_X + 0.5f;
    p.y = DOOR_CELL_Y + 0.5f;
    p.angle = -TEST_PI / 2.0f;
    door_try_use(&doors, &p, &map);
    TEST_ASSERT_EQUAL_INT(DOOR_OPENING, doors.doors[0].state);
}

/* --- Mandatory scenarios --- */

static void test_empty_door_closes_after_open_time(void)
{
    open_the_door();
    TEST_ASSERT_EQUAL_INT(0, door_is_blocking(&doors, DOOR_CELL_X, DOOR_CELL_Y));

    /* safely inside DOOR_OPEN_TIME */
    run_ticks(NULL, 0, ticks_for(TEST_DOOR_OPEN_TIME - 0.5f));
    TEST_ASSERT_EQUAL_INT(DOOR_OPEN, doors.doors[0].state);
    TEST_ASSERT_EQUAL_INT(0, door_is_blocking(&doors, DOOR_CELL_X, DOOR_CELL_Y));

    /* safely past it, but not yet past the closing ramp */
    run_ticks(NULL, 0, ticks_for(1.0f));
    TEST_ASSERT_NOT_EQUAL_INT(DOOR_OPEN, doors.doors[0].state);

    /* and all the way down: solid again */
    run_ticks(NULL, 0, ticks_for(TEST_RAMP_TIME));
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, doors.doors[0].state);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, doors.doors[0].openness);
    TEST_ASSERT_EQUAL_INT(1, door_is_blocking(&doors, DOOR_CELL_X, DOOR_CELL_Y));
}

/* The point of bug-002: whoever stands in the doorway is never walled in. */
static void test_occupied_door_stays_open_indefinitely(void)
{
    const DoorOccupant occ = {DOOR_CELL_X + 0.5f, DOOR_CELL_Y + 0.5f};

    open_the_door();

    for (int i = 0; i < ticks_for(5.0f * TEST_DOOR_OPEN_TIME); i++) {
        door_update_all(&doors, &occ, 1, TEST_STEP);
        TEST_ASSERT_EQUAL_INT(DOOR_OPEN, doors.doors[0].state);
        TEST_ASSERT_EQUAL_INT(0, door_is_blocking(&doors, DOOR_CELL_X, DOOR_CELL_Y));
    }
}

static void test_door_closes_once_the_cell_is_free(void)
{
    const DoorOccupant occ = {DOOR_CELL_X + 0.5f, DOOR_CELL_Y + 0.5f};

    open_the_door();
    run_ticks(&occ, 1, ticks_for(2.0f * TEST_DOOR_OPEN_TIME));
    TEST_ASSERT_EQUAL_INT(DOOR_OPEN, doors.doors[0].state);

    run_ticks(NULL, 0, ticks_for(TEST_DOOR_OPEN_TIME + TEST_RAMP_TIME + 0.5f));
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, doors.doors[0].state);
    TEST_ASSERT_EQUAL_INT(1, door_is_blocking(&doors, DOOR_CELL_X, DOOR_CELL_Y));
}

static void test_closing_door_reopens_for_an_occupant(void)
{
    const DoorOccupant occ = {DOOR_CELL_X + 0.5f, DOOR_CELL_Y + 0.5f};

    open_the_door();
    run_ticks(NULL, 0, ticks_for(TEST_DOOR_OPEN_TIME + 0.1f));
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSING, doors.doors[0].state);
    TEST_ASSERT_TRUE(doors.doors[0].openness < 1.0f);

    run_ticks(&occ, 1, 1);
    TEST_ASSERT_EQUAL_INT(DOOR_OPENING, doors.doors[0].state);

    run_ticks(&occ, 1, ticks_for(TEST_RAMP_TIME));
    TEST_ASSERT_EQUAL_INT(DOOR_OPEN, doors.doors[0].state);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, doors.doors[0].openness);
}

/* Occupancy is a circle of PLAYER_RADIUS against the cell square, so a body
 * overlapping the threshold counts even with its centre outside the cell. */
static void test_occupant_on_the_threshold_holds_the_door(void)
{
    DoorOccupant occ;
    occ.x = (float)DOOR_CELL_X - 0.1f; /* centre outside, body overlapping */
    occ.y = DOOR_CELL_Y + 0.5f;
    TEST_ASSERT_EQUAL_INT(DOOR_CELL_X - 1, (int)occ.x);
    TEST_ASSERT_TRUE(occ.x + PLAYER_RADIUS > (float)DOOR_CELL_X);

    open_the_door();
    run_ticks(&occ, 1, ticks_for(3.0f * TEST_DOOR_OPEN_TIME));
    TEST_ASSERT_EQUAL_INT(DOOR_OPEN, doors.doors[0].state);

    /* stepping back clear of the threshold releases it */
    occ.x = (float)DOOR_CELL_X - PLAYER_RADIUS - 0.05f;
    run_ticks(&occ, 1, ticks_for(TEST_DOOR_OPEN_TIME + TEST_RAMP_TIME + 0.5f));
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, doors.doors[0].state);
    TEST_ASSERT_EQUAL_INT(1, door_is_blocking(&doors, DOOR_CELL_X, DOOR_CELL_Y));
}

static void test_occupant_in_another_cell_is_ignored(void)
{
    const DoorOccupant occ = {4.5f, 4.5f};

    open_the_door();
    run_ticks(&occ, 1, ticks_for(TEST_DOOR_OPEN_TIME + TEST_RAMP_TIME + 0.5f));
    TEST_ASSERT_EQUAL_INT(DOOR_CLOSED, doors.doors[0].state);
    TEST_ASSERT_EQUAL_INT(1, door_is_blocking(&doors, DOOR_CELL_X, DOOR_CELL_Y));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_door_discover_registers_every_door_cell);
    RUN_TEST(test_door_is_blocking_below_half_open);
    RUN_TEST(test_door_try_use_toggles_the_state);
    RUN_TEST(test_door_try_use_out_of_range_does_nothing);
    RUN_TEST(test_door_try_use_from_inside_the_doorway);
    RUN_TEST(test_empty_door_closes_after_open_time);
    RUN_TEST(test_occupied_door_stays_open_indefinitely);
    RUN_TEST(test_door_closes_once_the_cell_is_free);
    RUN_TEST(test_closing_door_reopens_for_an_occupant);
    RUN_TEST(test_occupant_on_the_threshold_holds_the_door);
    RUN_TEST(test_occupant_in_another_cell_is_ignored);
    return UNITY_END();
}
