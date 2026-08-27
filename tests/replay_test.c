/* Determinism of world_step(): two runs of the same input sequence from the
 * same world_load() must land on bit-identical state.
 *
 * This is the only automatic guard against prediction drift. A client predicts
 * its own moves by running world_step() ahead of the server and re-running it
 * over the acknowledged ticks when a snapshot arrives; if a tick can land
 * anywhere but where the server put it, the two diverge and the symptoms —
 * rubber-banding, shots that miss what they visibly hit — point everywhere but
 * at the cause. What that catches here is a tick that reads the wall clock,
 * reads rand(), or carries state world_load() forgot to reset.
 *
 * The two runs are seeded differently on purpose: the simulation must not
 * consume rand() at all. Shotgun spread does (weapon.c), which is why the run
 * below never switches to it — that draw becomes deterministic in stage 7,
 * where the server seeds it from the value it hands out in MSG_ACCEPT.
 *
 * The map is written out by the test, so it depends on neither the working
 * directory nor assets/maps/. The tick keeps no render state of its own, so
 * sprite.c is not linked and nothing has to be stubbed for it. */

#include "unity.h"
#include "world.h"
#include "config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAP_PATH "replay_map.txt"
#define REPLAY_TICKS 600

/* ---- fixture ---- */

/* Everything the tick touches has to be on the map, or the run proves nothing:
 * two enemy types, a door, all three pickups and a barrel. Two rooms divided
 * by a wall the door is the only way through, so the fight only starts once
 * the scripted run has opened it.
 *
 *   0123456789012345
 * 0 ################
 * 1 #P...#.........#
 * 2 #....#.........#
 * 3 #M...#....E....#
 * 4 #....#.........#
 * 5 #A...#....B....#
 * 6 #....#....S....#
 * 7 #R...D.........#
 * 8 ################ */
static const char *const map_rows[] = {
        "################", "#P...#.........#", "#....#.........#",
        "#M...#....E....#", "#....#.........#", "#A...#....B....#",
        "#....#....S....#", "#R...D.........#", "################",
};

static void write_map(void)
{
    FILE *f = fopen(MAP_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    for (size_t i = 0; i < sizeof(map_rows) / sizeof(map_rows[0]); i++) {
        fprintf(f, "%s\n", map_rows[i]);
    }
    fclose(f);
}

/* One tick of input, derived from the tick number alone: the sequence is the
 * same in both runs by construction rather than by being recorded.
 *
 * A route rather than a pattern, because a pattern goes nowhere: down the west
 * room over the three pickups, east into the door, one press to open it, then
 * through and into the enemies with the pistol raking across them. Use is
 * pressed on exactly one tick — door_try_use() toggles, so pressing it twice
 * would shut the door again. */
static PlayerInput scripted_input(uint32_t tick)
{
    PlayerInput in;
    in.tick = tick;
    in.buttons = BTN_FORWARD;
    if (tick < 150) {
        in.angle = MATH_PI / 2.0f; /* south, over the three pickups */
    } else if (tick < 300) {
        in.angle = 0.0f; /* east, up against the door */
    } else if (tick < 320) {
        /* Off the axis, or the cell USE_RANGE ahead lands past the door. */
        in.angle = -0.4f;
        if (tick == 310) {
            in.buttons |= BTN_USE;
        }
    } else if (tick < 420) {
        /* through the doorway and out into the far room */
        in.angle = -0.35f + 0.3f * sinf((float)tick * 0.03f);
    } else {
        /* stand and turn on the spot, firing into whatever came after you */
        in.buttons = 0;
        in.angle = (float)tick * 0.05f;
    }
    /* The pistol's cooldown is 0.45 s, so every 37th tick is a real shot. */
    if (tick % 37 == 0) {
        in.buttons |= BTN_FIRE;
    }
    /* Not while walking the pickups: strafing pins the player to the west wall
     * and the three of them are then only just within reach. */
    if (tick >= 150 && tick % 90 < 15) {
        in.buttons |= BTN_LEFT;
    }
    return in;
}

static void run_replay(World *w, unsigned seed)
{
    /* Seeded differently per run: a tick that reads rand() lands elsewhere. */
    srand(seed);
    TEST_ASSERT_EQUAL_INT(0, world_load(w, MAP_PATH));
    /* A loaded world is empty of players; the one this run drives joins at the
     * map's start cell and takes id 0, which is the input's index. */
    TEST_ASSERT_EQUAL_INT(0, world_add_player(w, 0));
    for (uint32_t t = 0; t < REPLAY_TICKS; t++) {
        PlayerInput in = scripted_input(t);
        world_step(w, &in, 1, FIXED_DT);
    }
}

static uint32_t float_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

/* Bit-exact, not approximate. A difference in the last place is not a rounding
 * detail here: it is where prediction drift starts, and it grows. */
#define ASSERT_SAME_FLOAT(a, b, what)                                                              \
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(float_bits(a), float_bits(b), what)

static void assert_worlds_match(const World *a, const World *b)
{
    TEST_ASSERT_EQUAL_UINT32(a->tick, b->tick);

    ASSERT_SAME_FLOAT(a->players[0].x, b->players[0].x, "player.x");
    ASSERT_SAME_FLOAT(a->players[0].y, b->players[0].y, "player.y");
    ASSERT_SAME_FLOAT(a->players[0].angle, b->players[0].angle, "player.angle");
    ASSERT_SAME_FLOAT(a->players[0].hp, b->players[0].hp, "player.hp");
    ASSERT_SAME_FLOAT(a->players[0].armor, b->players[0].armor, "player.armor");
    TEST_ASSERT_EQUAL_INT(a->players[0].weapons.current, b->players[0].weapons.current);
    for (int i = 0; i < WEAPON_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(a->players[0].weapons.weapons[i].ammo,
                              b->players[0].weapons.weapons[i].ammo);
        ASSERT_SAME_FLOAT(a->players[0].weapons.weapons[i].cooldown,
                          b->players[0].weapons.weapons[i].cooldown, "weapon.cooldown");
    }

    TEST_ASSERT_EQUAL_INT(a->enemies.count, b->enemies.count);
    for (int i = 0; i < a->enemies.count; i++) {
        const Enemy *ea = &a->enemies.items[i];
        const Enemy *eb = &b->enemies.items[i];
        TEST_ASSERT_EQUAL_INT(ea->state, eb->state);
        ASSERT_SAME_FLOAT(ea->hp, eb->hp, "enemy.hp");
        ASSERT_SAME_FLOAT(ea->x, eb->x, "enemy.x");
        ASSERT_SAME_FLOAT(ea->y, eb->y, "enemy.y");
        ASSERT_SAME_FLOAT(ea->attack_cooldown, eb->attack_cooldown, "enemy.attack_cooldown");
    }

    TEST_ASSERT_EQUAL_INT(a->doors.count, b->doors.count);
    for (int i = 0; i < a->doors.count; i++) {
        TEST_ASSERT_EQUAL_INT(a->doors.doors[i].state, b->doors.doors[i].state);
        ASSERT_SAME_FLOAT(a->doors.doors[i].openness, b->doors.doors[i].openness, "door.openness");
        ASSERT_SAME_FLOAT(a->doors.doors[i].timer, b->doors.doors[i].timer, "door.timer");
    }

    TEST_ASSERT_EQUAL_INT(a->items.count, b->items.count);
    for (int i = 0; i < a->items.count; i++) {
        TEST_ASSERT_EQUAL_INT(a->items.items[i].active, b->items.items[i].active);
    }
}

/* Reused across tests rather than declared inside them: a World is a few
 * hundred kilobytes of fixed-size arrays and does not belong on the stack. */
static World first, second;

void setUp(void)
{
    write_map();
}

void tearDown(void)
{
    remove(MAP_PATH);
}

/* ---- tests ---- */

static void test_two_runs_of_the_same_input_agree(void)
{
    run_replay(&first, 1u);
    run_replay(&second, 424242u);
    assert_worlds_match(&first, &second);
}

/* Reloading into a World that has already been played through must give the
 * same run as loading into a fresh one: anything world_load() leaves behind is
 * state a server would carry from one match into the next. */
static void test_a_reloaded_world_replays_the_same(void)
{
    run_replay(&first, 1u);
    run_replay(&first, 1u);
    run_replay(&second, 1u);
    assert_worlds_match(&first, &second);
}

/* Two identical runs of a world where nothing happens would agree just as
 * well, so check the run is worth comparing at all: the player walked, spent
 * ammo, picked things up, opened the door, took damage and dealt some. */
static void test_the_scripted_run_exercises_the_world(void)
{
    run_replay(&first, 1u);

    TEST_ASSERT_EQUAL_UINT32(REPLAY_TICKS, first.tick);
    TEST_ASSERT_EQUAL_INT(2, first.enemies.count);
    TEST_ASSERT_EQUAL_INT(1, first.doors.count);
    TEST_ASSERT_EQUAL_INT(3, first.items.count);

    /* out of the west room entirely: the door was opened and walked through */
    TEST_ASSERT_TRUE(first.players[0].x > 6.0f);
    TEST_ASSERT_TRUE(first.doors.doors[0].openness > 0.0f);

    TEST_ASSERT_TRUE(first.players[0].weapons.weapons[WEAPON_PISTOL].ammo < 50);
    TEST_ASSERT_TRUE(first.players[0].hp < 100.0f);

    /* The medkit is walked over at full health and stays: that refusal is a
     * path through item_update() as much as a pickup is. */
    int taken = 0;
    for (int i = 0; i < first.items.count; i++) {
        if (!first.items.items[i].active) {
            taken++;
        }
    }
    TEST_ASSERT_EQUAL_INT(2, taken);

    int alerted = 0, wounded = 0;
    for (int i = 0; i < first.enemies.count; i++) {
        const Enemy *e = &first.enemies.items[i];
        if (e->state != ESTATE_IDLE) {
            alerted = 1;
        }
        if (e->hp < enemy_def(e->type)->max_hp) {
            wounded = 1;
        }
    }
    TEST_ASSERT_TRUE(alerted);
    TEST_ASSERT_TRUE(wounded);
}

/* The queue holds what the last tick produced and nothing older: the client
 * drains it once per tick, and a stale event would be played twice. */
static void test_a_tick_clears_the_event_queue_it_fills(void)
{
    TEST_ASSERT_EQUAL_INT(0, world_load(&first, MAP_PATH));
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&first, 0));

    PlayerInput in = scripted_input(0);
    in.buttons = BTN_FIRE;
    world_step(&first, &in, 1, FIXED_DT);
    TEST_ASSERT_TRUE(first.events.count > 0);

    in.buttons = 0;
    world_step(&first, &in, 1, FIXED_DT);
    TEST_ASSERT_EQUAL_INT(0, first.events.count);
}

/* A tick without input is a standing player, not a player facing east: a
 * dropped packet must not spin the camera. */
static void test_a_tick_without_input_keeps_the_facing(void)
{
    TEST_ASSERT_EQUAL_INT(0, world_load(&first, MAP_PATH));
    TEST_ASSERT_EQUAL_INT(0, world_add_player(&first, 0));
    float angle = first.players[0].angle;
    float x = first.players[0].x, y = first.players[0].y;

    world_step(&first, NULL, 0, FIXED_DT);

    ASSERT_SAME_FLOAT(angle, first.players[0].angle, "player.angle");
    ASSERT_SAME_FLOAT(x, first.players[0].x, "player.x");
    ASSERT_SAME_FLOAT(y, first.players[0].y, "player.y");
    TEST_ASSERT_EQUAL_UINT32(1, first.tick);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_two_runs_of_the_same_input_agree);
    RUN_TEST(test_a_reloaded_world_replays_the_same);
    RUN_TEST(test_the_scripted_run_exercises_the_world);
    RUN_TEST(test_a_tick_clears_the_event_queue_it_fills);
    RUN_TEST(test_a_tick_without_input_keeps_the_facing);
    return UNITY_END();
}
