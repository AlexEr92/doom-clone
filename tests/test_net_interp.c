/* net_interp.c: which moment of the server's a client draws, and what the
 * players and enemies look like at it.
 *
 * The client's world is the map loaded fresh with each snapshot put into it,
 * as net_client.c does. Every snapshot here arrives LATENCY after its tick, so
 * the moment drawn at view_at(tick) is that tick exactly; the last test sends
 * them through a network of its own — late by a varying amount, some lost —
 * and reads the view a frame at a time, as the game does.
 *
 * The map is written out by the test, as in replay_test.c. */

#include "unity.h"
#include "net_interp.h"
#include "net_protocol.h"
#include "world.h"
#include "config.h"
#include "utils.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAP_PATH "net_interp_map.txt"

/*   0123456789
 * 0 ##########
 * 1 #P.......#
 * 2 #.....E..#
 * 3 #........#
 * 4 ########## */
static const char *const map_rows[] = {"##########", "#P.......#", "#.....E..#", "#........#",
                                       "##########"};

/* Seconds from a snapshot's tick to its arrival. */
#define LATENCY 0.05

#define ME 0    /* the local player */
#define OTHER 1 /* another one */

/* Static: a World is tens of kilobytes, too much for the stack of a test. */
static World world, view;
static SnapshotBuffer buf;
static MsgSnapshot snap;

void setUp(void)
{
    FILE *f = fopen(MAP_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    for (size_t i = 0; i < sizeof(map_rows) / sizeof(map_rows[0]); i++) {
        fprintf(f, "%s\n", map_rows[i]);
    }
    fclose(f);
    TEST_ASSERT_EQUAL_INT(0, world_load(&world, MAP_PATH));
    world.local_player = ME;
    interp_reset(&buf);
}

void tearDown(void)
{
    remove(MAP_PATH);
}

/* ---- building snapshots ---- */

/* A snapshot of `tick` with the local player standing still and the imp of
 * the map alive where it stands; the tests add the rest. */
static MsgSnapshot *snap_new(uint32_t tick)
{
    memset(&snap, 0, sizeof(snap));
    snap.h.type = MSG_SNAPSHOT;
    snap.tick = tick;
    NetPlayer *me = &snap.players[snap.player_count++];
    me->id = ME;
    me->alive = 1;
    me->x = 1.5f;
    me->y = 1.5f;
    me->hp = 100;
    snap.enemy_count = 1;
    snap.enemies[0].type = ENEMY_IMP;
    snap.enemies[0].state = ESTATE_IDLE;
    snap.enemies[0].hp = 30;
    snap.enemies[0].x = 6.5f;
    snap.enemies[0].y = 2.5f;
    return &snap;
}

static NetPlayer *snap_player(uint8_t id, float x, float y, float angle, int alive)
{
    NetPlayer *p = &snap.players[snap.player_count++];
    p->id = id;
    p->alive = (uint8_t)alive;
    p->x = x;
    p->y = y;
    p->angle = angle;
    p->hp = alive ? 100 : 0;
    return p;
}

/* What the client does with a snapshot: into its world, and into the buffer
 * at `now`. */
static void receive_at(double now)
{
    snapshot_apply(&world, &snap);
    interp_push(&buf, &snap, now);
}

static void receive(void)
{
    receive_at((double)snap.tick / TICK_RATE + LATENCY);
}

/* The view of the moment `tick`, which may fall between ticks. */
static const World *view_at(double tick)
{
    interp_view(&view, &world, &buf, tick / TICK_RATE + LATENCY + INTERP_DELAY);
    return &view;
}

static const PlayerState *other_at(double tick)
{
    return &view_at(tick)->players[OTHER];
}

/* The other player walking east from x = 2 in two snapshots, at ticks 0 and
 * 3, facing `a0` and then `a3`. */
static void other_walks(float x0, float x3, float a0, float a3)
{
    snap_new(0);
    snap_player(OTHER, x0, 2.5f, a0, 1);
    receive();
    snap_new(3);
    snap_player(OTHER, x3, 2.5f, a3, 1);
    receive();
}

/* ---- between two snapshots ---- */

static void test_with_no_snapshot_the_view_is_the_world(void)
{
    interp_view(&view, &world, &buf, 1.0);
    TEST_ASSERT_EQUAL_MEMORY(&world, &view, sizeof(World));
}

static void test_other_player_moves_between_two_snapshots(void)
{
    other_walks(2.0f, 2.3f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(other_at(0.0)->active);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f, other_at(0.0)->x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.1f, other_at(1.0)->x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.15f, other_at(1.5)->x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.3f, other_at(3.0)->x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.5f, other_at(1.5)->y);
    /* The world itself is the newest snapshot, as before. */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.3f, world.players[OTHER].x);
}

/* The moment drawn is the server's: a snapshot 30 ms late, almost two ticks,
 * moves it by a fraction of that — less than a quarter of a tick, 0.025 of the
 * way here — rather than stretching the walk from the last one by 30 ms. */
static void test_snapshot_arriving_late_barely_moves_the_moment_drawn(void)
{
    other_walks(2.0f, 2.3f, 0.0f, 0.0f);
    snap_new(6);
    snap_player(OTHER, 2.6f, 2.5f, 0.0f, 1);
    receive_at(6.0 / TICK_RATE + LATENCY + 0.030);
    TEST_ASSERT_FLOAT_WITHIN(0.025f, 2.45f, other_at(4.5)->x);
}

static void test_local_player_is_the_worlds_not_interpolated(void)
{
    snap_new(0);
    snap_player(OTHER, 2.0f, 2.5f, 0.0f, 1);
    receive();
    snap_new(3);
    snap.players[0].x = 1.8f;
    snap.players[0].hp = 90;
    snap_player(OTHER, 2.3f, 2.5f, 0.0f, 1);
    receive();
    const World *v = view_at(1.5);
    TEST_ASSERT_EQUAL_MEMORY(&world.players[ME], &v->players[ME], sizeof(PlayerState));
    TEST_ASSERT_EQUAL_FLOAT(1.8f, v->players[ME].x);
    TEST_ASSERT_EQUAL_INT(ME, v->local_player);
}

/* A facing of 3.0 then -3.0 is a turn of 0.28 through PI. A plain lerp would
 * swing the whole way back through 0 — and the sheet row with it. */
static void test_facing_turns_the_short_way_across_pi(void)
{
    other_walks(2.0f, 2.0f, 3.0f, -3.0f);
    for (int i = 0; i <= 6; i++) {
        float a = other_at(i * 0.5)->angle;
        TEST_ASSERT_TRUE_MESSAGE(fabsf(a) >= 3.0f - 1e-5f, "the facing went the long way round");
        TEST_ASSERT_TRUE(a >= -MATH_PI && a <= MATH_PI);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, MATH_PI, fabsf(other_at(1.5)->angle));
}

static void test_what_does_not_move_comes_from_the_earlier_snapshot(void)
{
    snap_new(0);
    NetPlayer *p = snap_player(OTHER, 2.0f, 2.5f, 0.0f, 1);
    p->weapon = WEAPON_PISTOL;
    snap.enemies[0].state = ESTATE_CHASE;
    receive();
    snap_new(3);
    p = snap_player(OTHER, 2.3f, 2.5f, 0.0f, 1);
    p->hp = 40;
    p->weapon = WEAPON_SHOTGUN;
    snap.enemies[0].state = ESTATE_ATTACK;
    snap.enemies[0].hp = 10;
    receive();

    const World *v = view_at(2.9);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, v->players[OTHER].hp);
    TEST_ASSERT_EQUAL_INT(WEAPON_PISTOL, v->players[OTHER].weapons.current);
    TEST_ASSERT_EQUAL_INT(ESTATE_CHASE, v->enemies.items[0].state);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, v->enemies.items[0].hp);

    v = view_at(3.0);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, v->players[OTHER].hp);
    TEST_ASSERT_EQUAL_INT(WEAPON_SHOTGUN, v->players[OTHER].weapons.current);
    TEST_ASSERT_EQUAL_INT(ESTATE_ATTACK, v->enemies.items[0].state);
}

/* Items and doors are not characters: they are the newest snapshot's. */
static void test_items_and_doors_are_the_worlds(void)
{
    snap_new(0);
    receive();
    snap_new(3);
    snap.items_alive = 0;
    receive();
    TEST_ASSERT_EQUAL_MEMORY(&world.items, &view_at(0.0)->items, sizeof(ItemList));
    TEST_ASSERT_EQUAL_MEMORY(&world.doors, &view_at(0.0)->doors, sizeof(DoorList));
}

static void test_enemy_moves_between_two_snapshots(void)
{
    snap_new(0);
    receive();
    snap_new(3);
    snap.enemies[0].x = 6.2f;
    snap.enemies[0].y = 2.2f;
    receive();
    const Enemy *e = &view_at(1.5)->enemies.items[0];
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 6.35f, e->x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.35f, e->y);
}

/* A corpse is not dragged anywhere: the server may move what lies where an
 * enemy fell, but nothing walks there. */
static void test_corpse_does_not_move(void)
{
    snap_new(0);
    snap.enemies[0].state = ESTATE_DEAD;
    receive();
    snap_new(3);
    snap.enemies[0].state = ESTATE_DEAD;
    snap.enemies[0].x = 7.5f;
    receive();
    TEST_ASSERT_EQUAL_FLOAT(6.5f, view_at(1.5)->enemies.items[0].x);
    TEST_ASSERT_EQUAL_FLOAT(7.5f, view_at(3.0)->enemies.items[0].x);
}

/* ---- coming and going ---- */

static void test_player_who_joined_after_the_moment_drawn_is_not_there_yet(void)
{
    snap_new(0);
    receive();
    snap_new(3);
    snap_player(OTHER, 2.3f, 2.5f, 0.0f, 1);
    receive();
    TEST_ASSERT_TRUE(world.players[OTHER].active);
    TEST_ASSERT_FALSE(other_at(1.5)->active);
    TEST_ASSERT_TRUE(other_at(3.0)->active);
    TEST_ASSERT_EQUAL_FLOAT(2.3f, other_at(3.0)->x);
}

static void test_player_who_left_stays_until_the_moment_drawn_passes(void)
{
    snap_new(0);
    snap_player(OTHER, 2.0f, 2.5f, 0.0f, 1);
    receive();
    snap_new(3);
    receive();
    TEST_ASSERT_FALSE(world.players[OTHER].active);
    TEST_ASSERT_TRUE(other_at(1.5)->active);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, other_at(1.5)->x);
    TEST_ASSERT_FALSE(other_at(3.0)->active);
}

/* Dead at one end of the map, alive at the other: a respawn, drawn as a jump
 * once the moment drawn reaches it, and not as a walk across the map. */
static void test_respawn_is_a_jump(void)
{
    snap_new(0);
    snap_player(OTHER, 8.0f, 3.0f, 0.0f, 0);
    receive();
    snap_new(3);
    snap_player(OTHER, 2.0f, 1.5f, 0.0f, 1);
    receive();
    TEST_ASSERT_FALSE(other_at(2.9)->alive);
    TEST_ASSERT_EQUAL_FLOAT(8.0f, other_at(2.9)->x);
    TEST_ASSERT_TRUE(other_at(3.0)->alive);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, other_at(3.0)->x);
}

/* A player shot dead on the run reaches the place they fell, alive, and lies
 * there: they do not slide on past it. */
static void test_death_is_walked_to_and_not_past(void)
{
    snap_new(0);
    snap_player(OTHER, 2.0f, 2.5f, 0.0f, 1);
    receive();
    snap_new(3);
    snap_player(OTHER, 2.3f, 2.5f, 0.0f, 0);
    receive();
    TEST_ASSERT_TRUE(other_at(1.5)->alive);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.15f, other_at(1.5)->x);
    TEST_ASSERT_FALSE(other_at(6.0)->alive);
    TEST_ASSERT_EQUAL_FLOAT(2.3f, other_at(6.0)->x);
}

/* ---- outside the snapshots kept ---- */

static void test_motion_goes_on_past_the_newest_then_stops(void)
{
    other_walks(2.0f, 2.3f, 0.0f, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.6f, other_at(6.0)->x);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.2f, other_at(6.0)->angle);
    /* INTERP_EXTRAPOLATE past tick 3, at 0.1 a tick. */
    double last = 3.0 + INTERP_EXTRAPOLATE * TICK_RATE;
    float stop = 2.3f + 0.1f * (float)(INTERP_EXTRAPOLATE * TICK_RATE);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, stop, other_at(last)->x);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, stop, other_at(last + 30.0)->x);
}

static void test_before_the_oldest_snapshot_the_oldest_is_drawn(void)
{
    other_walks(2.0f, 2.3f, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, other_at(-20.0)->x);
}

static void test_single_snapshot_is_drawn_as_it_is(void)
{
    snap_new(0);
    snap_player(OTHER, 2.0f, 2.5f, 0.0f, 1);
    receive();
    TEST_ASSERT_EQUAL_FLOAT(2.0f, other_at(-3.0)->x);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, other_at(30.0)->x);
}

/* ---- the buffer ---- */

static void test_buffer_keeps_the_newest_history(void)
{
    for (uint32_t i = 0; i < SNAPSHOT_HISTORY + 4; i++) {
        snap_new(i * 3);
        snap_player(OTHER, 2.0f + 0.3f * (float)i, 2.5f, 0.0f, 1);
        receive();
    }
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_HISTORY, buf.count);
    /* The four oldest are gone: far back, the oldest kept is drawn. */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f + 0.3f * 4.0f, other_at(0.0)->x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f + 0.3f * 10.5f, other_at(31.5)->x);
}

static void test_snapshot_not_newer_than_the_newest_is_not_kept(void)
{
    snap_new(6);
    receive();
    snap_new(3);
    interp_push(&buf, &snap, 1.0);
    snap_new(6);
    interp_push(&buf, &snap, 1.0);
    TEST_ASSERT_EQUAL_INT(1, buf.count);
}

/* A poll that reads three snapshots at once — they waited in the socket while
 * the frame was long — dates them all by itself, so the older two look late.
 * Only the newest says how the clocks line up: the moment drawn is the one a
 * poll with the newest alone would give, and the latency has not changed. */
static void test_poll_with_several_snapshots_is_timed_by_the_newest(void)
{
    static SnapshotBuffer one;
    static World seen_one;
    interp_reset(&one);
    for (uint32_t tick = 0; tick <= 21; tick += 3) {
        snap_new(tick);
        snap_player(OTHER, 2.0f + 0.1f * (float)tick, 2.5f, 0.0f, 1);
        if (tick < 15) {
            receive();
        } else {
            receive_at(21.0 / TICK_RATE + LATENCY);
        }
        if (tick == 0 || tick == 3) {
            interp_push(&one, &snap, (double)tick / TICK_RATE + LATENCY);
        } else if (tick == 21) {
            interp_push(&one, &snap, 21.0 / TICK_RATE + LATENCY);
        }
    }
    /* Tick 15.6: on the way from 15 to 18 in the one buffer, from 3 to 21 in
     * the other, and the player walks a straight line. */
    double now = 21.0 / TICK_RATE + LATENCY + 0.01;
    interp_view(&seen_one, &world, &one, now);
    interp_view(&view, &world, &buf, now);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f + 0.1f * 15.6f, view.players[OTHER].x);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, seen_one.players[OTHER].x, view.players[OTHER].x);
}

/* A server that stood still for a second — its window dragged, say — is a
 * second behind the client's clock from then on. That is no jitter to smooth
 * over: the moment drawn would stand past every snapshot for seconds. It
 * follows at once. */
static void test_server_that_lost_a_second_is_followed_at_once(void)
{
    other_walks(2.0f, 2.3f, 0.0f, 0.0f);
    snap_new(6);
    snap_player(OTHER, 2.6f, 2.5f, 0.0f, 1);
    double late = 6.0 / TICK_RATE + LATENCY + 1.0;
    receive_at(late);
    interp_view(&view, &world, &buf, late + INTERP_DELAY);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.6f, view.players[OTHER].x);
}

/* ---- frame by frame ---- */

/* The criterion of the task: another player moves smoothly at SNAPSHOT_RATE
 * and 60 frames a second, with a tenth of the snapshots lost.
 *
 * The other player runs a circle at MOVE_SPEED, facing along it, so the
 * facing crosses PI once a lap. The server snapshots every SIM_EVERY ticks.
 * Each snapshot is lost with probability 1/10 or takes 75 ms ± 10 ms, as
 * `tc netem delay 75ms 10ms loss 10%` would have it; the client reads what
 * has come at the start of each frame, on a clock of whole milliseconds as
 * ENet's is, and draws the view.
 *
 * Every frame then moves the player by about one frame's worth of the run and
 * turns them by one frame's worth of the lap. Drawn from the snapshots as
 * they come, a frame would move them three frames' worth, then nothing twice;
 * a jerk here is a frame a quarter off its share.
 *
 * No more than two go missing in a row. Three are a gap of 200 ms, a whole
 * interval past what INTERP_DELAY bridges: the view goes on in a straight
 * line, and on a curve the next snapshot that comes sets it back by a step
 * — half a frame's worth after three, more after four. At a tenth lost,
 * three in a row come about once a minute. */
#define SIM_SECONDS 8
#define SIM_EVERY (TICK_RATE / SNAPSHOT_RATE) /* ticks from one snapshot to the next */
#define SIM_SENT (SIM_SECONDS * TICK_RATE / SIM_EVERY)
#define SIM_RADIUS 2.0
#define SIM_LOSS 0.1
#define SIM_DELAY 0.075
#define SIM_JITTER 0.010

static uint32_t sim_seed;

static double sim_random(void)
{
    sim_seed = sim_seed * 1664525u + 1013904223u;
    return (double)(sim_seed >> 8) / 16777216.0;
}

static void sim_snapshot(uint32_t tick)
{
    double turn = MOVE_SPEED / SIM_RADIUS; /* radians a second */
    double a = turn * (double)tick / TICK_RATE;
    snap_new(tick);
    snap_player(OTHER, (float)(5.0 + SIM_RADIUS * cos(a)), (float)(2.5 + SIM_RADIUS * sin(a)),
                wrap_angle((float)(a + MATH_PI / 2.0)), 1);
}

static void test_other_player_moves_smoothly_through_loss_and_jitter(void)
{
    static double arrives[SIM_SENT]; /* negative: lost */
    sim_seed = 20261019u;
    int lost = 0, twice = 0;
    for (int i = 0; i < SIM_SENT; i++) {
        double sent = (double)(i * SIM_EVERY) / TICK_RATE;
        int after_two = i >= 2 && arrives[i - 1] < 0.0 && arrives[i - 2] < 0.0;
        if (sim_random() < SIM_LOSS && !after_two) {
            arrives[i] = -1.0;
            lost++;
            twice += i >= 1 && arrives[i - 1] < 0.0;
        } else {
            arrives[i] = sent + SIM_DELAY + (sim_random() * 2.0 - 1.0) * SIM_JITTER;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(lost > SIM_SENT / 20, "the network lost too little to test with");
    TEST_ASSERT_TRUE_MESSAGE(twice > 0, "no two lost in a row to test with");

    double step = MOVE_SPEED / 60.0;
    double turn = MOVE_SPEED / SIM_RADIUS / 60.0;
    double worst_step = 0.0, worst_turn = 0.0;
    int next = 0, frames = 0;
    float x = 0.0f, y = 0.0f, angle = 0.0f;
    /* A frame starts off the server's ticks, as two clocks do. */
    for (double t = 0.0037; t < SIM_SECONDS; t += 1.0 / 60.0) {
        double now = floor(t * 1000.0) / 1000.0;
        /* What came in since the last frame, in the order it came: none came
         * out of order at this jitter, so the order sent. */
        for (; next < SIM_SENT && (arrives[next] < 0.0 || arrives[next] <= t); next++) {
            if (arrives[next] >= 0.0) {
                sim_snapshot((uint32_t)(next * SIM_EVERY));
                receive_at(now);
            }
        }
        if (buf.count == 0) {
            continue;
        }
        interp_view(&view, &world, &buf, now);
        const PlayerState *p = &view.players[OTHER];
        if (t > 1.0) {
            double moved = hypot(p->x - x, p->y - y);
            double turned = fabs(wrap_angle(p->angle - angle));
            worst_step = fmax(worst_step, fabs(moved - step) / step);
            worst_turn = fmax(worst_turn, fabs(turned - turn) / turn);
            frames++;
        }
        x = p->x;
        y = p->y;
        angle = p->angle;
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "worst frame %.0f%% off its step, %.0f%% off its turn",
             worst_step * 100.0, worst_turn * 100.0);
    TEST_ASSERT_TRUE(frames > (SIM_SECONDS - 2) * 60);
    TEST_ASSERT_TRUE_MESSAGE(worst_step < 0.25, msg);
    TEST_ASSERT_TRUE_MESSAGE(worst_turn < 0.25, msg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_with_no_snapshot_the_view_is_the_world);
    RUN_TEST(test_other_player_moves_between_two_snapshots);
    RUN_TEST(test_snapshot_arriving_late_barely_moves_the_moment_drawn);
    RUN_TEST(test_local_player_is_the_worlds_not_interpolated);
    RUN_TEST(test_facing_turns_the_short_way_across_pi);
    RUN_TEST(test_what_does_not_move_comes_from_the_earlier_snapshot);
    RUN_TEST(test_items_and_doors_are_the_worlds);
    RUN_TEST(test_enemy_moves_between_two_snapshots);
    RUN_TEST(test_corpse_does_not_move);
    RUN_TEST(test_player_who_joined_after_the_moment_drawn_is_not_there_yet);
    RUN_TEST(test_player_who_left_stays_until_the_moment_drawn_passes);
    RUN_TEST(test_respawn_is_a_jump);
    RUN_TEST(test_death_is_walked_to_and_not_past);
    RUN_TEST(test_motion_goes_on_past_the_newest_then_stops);
    RUN_TEST(test_before_the_oldest_snapshot_the_oldest_is_drawn);
    RUN_TEST(test_single_snapshot_is_drawn_as_it_is);
    RUN_TEST(test_buffer_keeps_the_newest_history);
    RUN_TEST(test_snapshot_not_newer_than_the_newest_is_not_kept);
    RUN_TEST(test_poll_with_several_snapshots_is_timed_by_the_newest);
    RUN_TEST(test_server_that_lost_a_second_is_followed_at_once);
    RUN_TEST(test_other_player_moves_smoothly_through_loss_and_jitter);
    return UNITY_END();
}
