/* event.h: the queue a tick fills, and who hears what it holds. Header-only
 * inline functions, so nothing from src/ is linked here.
 *
 * The queue holds one tick and keeps an event once however often the tick
 * posts it: seven pellets into one enemy are one pain sound. What reaches a
 * player is the gain the client plays a sound at and the server picks the
 * events of a snapshot by: their own at full, the rest fading to nothing at
 * the range of the kind. */

#include "unity.h"
#include "event.h"

#define ME 2    /* the listening player's id */
#define OTHER 5 /* another player's */

static EventQueue q;

void setUp(void)
{
    event_queue_clear(&q);
}

void tearDown(void)
{}

static GameEvent event_at(GameEventKind kind, int actor, float x, float y)
{
    GameEvent e = {(uint8_t)kind, (uint8_t)actor, 0, x, y};
    return e;
}

/* ---- the queue ---- */

static void test_the_same_event_twice_in_a_tick_is_kept_once(void)
{
    for (int pellet = 0; pellet < 7; pellet++) {
        event_push(&q, EV_ENEMY_HURT, 3, 0, 4.5f + (float)pellet, 2.5f);
    }
    TEST_ASSERT_EQUAL_INT(1, q.count);
    TEST_ASSERT_EQUAL_UINT8(EV_ENEMY_HURT, q.items[0].kind);
    TEST_ASSERT_EQUAL_UINT8(3, q.items[0].actor);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, q.items[0].x); /* where the first one put it */
}

/* Only kind, actor and arg together make an event the same: an enemy hurt and
 * one killed, two enemies hurt, an enemy and a player with one index, one
 * player killing two others. */
static void test_another_kind_or_actor_is_another_event(void)
{
    event_push(&q, EV_ENEMY_HURT, 3, 0, 1.5f, 1.5f);
    event_push(&q, EV_ENEMY_DEATH, 3, 0, 1.5f, 1.5f);
    event_push(&q, EV_ENEMY_HURT, 4, 0, 1.5f, 1.5f);
    event_push(&q, EV_SHOT, 3, 0, 1.5f, 1.5f);
    event_push(&q, EV_PLAYER_DEATH, 3, 1, 1.5f, 1.5f);
    event_push(&q, EV_PLAYER_DEATH, 3, 2, 1.5f, 1.5f);
    event_push(&q, EV_PLAYER_DEATH, 3, 2, 1.5f, 1.5f);
    TEST_ASSERT_EQUAL_INT(6, q.count);
    TEST_ASSERT_EQUAL_UINT8(1, q.items[4].arg);
    TEST_ASSERT_EQUAL_UINT8(2, q.items[5].arg);
}

/* The next tick starts from a cleared queue, and the same event is a new
 * sound there. */
static void test_the_same_event_in_the_next_tick_is_posted_again(void)
{
    event_push(&q, EV_SHOT, ME, 0, 1.5f, 1.5f);
    event_queue_clear(&q);
    event_push(&q, EV_SHOT, ME, 0, 1.5f, 1.5f);
    TEST_ASSERT_EQUAL_INT(1, q.count);
}

static void test_a_full_queue_drops_what_comes_after(void)
{
    for (int i = 0; i < MAX_EVENTS + 3; i++) {
        event_push(&q, EV_DOOR, i, 0, 0.5f, 0.5f);
    }
    TEST_ASSERT_EQUAL_INT(MAX_EVENTS, q.count);
    TEST_ASSERT_EQUAL_UINT8(MAX_EVENTS - 1, q.items[MAX_EVENTS - 1].actor);

    event_push(NULL, EV_DOOR, 0, 0, 0.5f, 0.5f); /* no queue: nothing happens */
}

/* ---- who hears it ---- */

/* A player's own shot, pain, pickup and empty click are theirs at full
 * volume wherever they happened — pain is posted where the hit came from. */
static void test_a_players_own_events_reach_them_whole_from_any_distance(void)
{
    const GameEventKind own[] = {EV_SHOT, EV_PLAYER_HURT, EV_PICKUP, EV_NO_AMMO};
    for (size_t i = 0; i < sizeof(own) / sizeof(own[0]); i++) {
        GameEvent e = event_at(own[i], ME, 100.0f, 0.0f);
        TEST_ASSERT_TRUE(event_is_own(&e, ME));
        TEST_ASSERT_EQUAL_FLOAT(1.0f, event_gain(&e, ME, 0.0f, 0.0f));
    }
}

/* Somebody else's pickup and empty click are nobody else's business. */
static void test_another_players_pickup_and_empty_click_are_not_heard(void)
{
    GameEvent pickup = event_at(EV_PICKUP, OTHER, 1.0f, 1.0f);
    GameEvent click = event_at(EV_NO_AMMO, OTHER, 1.0f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, event_gain(&pickup, ME, 1.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, event_gain(&click, ME, 1.0f, 1.0f));
}

static void test_another_players_shot_fades_out_at_its_range(void)
{
    float range = event_range(EV_SHOT);
    GameEvent shot = event_at(EV_SHOT, OTHER, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, event_gain(&shot, ME, 0.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.75f, event_gain(&shot, ME, 0.0f, range * 0.25f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, event_gain(&shot, ME, range * 0.3f, range * 0.4f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, event_gain(&shot, ME, range, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, event_gain(&shot, ME, range * 2.0f, 0.0f));
}

/* An enemy's or a door's index is not a player id: one that happens to equal
 * the listener's is still a sound somewhere else. */
static void test_an_enemy_or_door_with_the_listeners_number_is_not_theirs(void)
{
    const GameEventKind kinds[] = {EV_ENEMY_HURT, EV_ENEMY_DEATH, EV_DOOR};
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        GameEvent e = event_at(kinds[i], ME, 0.0f, 0.0f);
        TEST_ASSERT_FALSE(event_is_own(&e, ME));
        TEST_ASSERT_EQUAL_FLOAT(0.5f, event_gain(&e, ME, event_range(kinds[i]) * 0.5f, 0.0f));
        TEST_ASSERT_EQUAL_FLOAT(0.0f, event_gain(&e, ME, event_range(kinds[i]), 0.0f));
    }
}

/* A death is the business of the two players in it, the one who died and the
 * one who killed them, wherever each of them is — and of nobody else, however
 * near. A death no player caused is the victim's alone. */
static void test_a_death_belongs_to_the_victim_and_the_killer(void)
{
    GameEvent killed_by_other = {EV_PLAYER_DEATH, OTHER, ME, 100.0f, 0.0f};
    GameEvent killed_other = {EV_PLAYER_DEATH, ME, OTHER, 100.0f, 0.0f};
    GameEvent between_others = {EV_PLAYER_DEATH, OTHER, OTHER + 1, 0.0f, 0.0f};
    GameEvent on_my_own = {EV_PLAYER_DEATH, ME, ME, 100.0f, 0.0f};

    TEST_ASSERT_EQUAL_FLOAT(1.0f, event_gain(&killed_by_other, ME, 0.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, event_gain(&killed_other, ME, 0.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, event_gain(&on_my_own, ME, 0.0f, 0.0f));
    TEST_ASSERT_FALSE(event_is_own(&between_others, ME));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, event_gain(&between_others, ME, 0.0f, 0.0f));
    TEST_ASSERT_FALSE(event_is_own(&on_my_own, OTHER));
}

/* Every sound that others hear carries some way; the shot, which the server
 * sends farthest, carries farthest of all. */
static void test_a_shot_carries_farthest(void)
{
    for (int kind = 0; kind < EV_COUNT; kind++) {
        TEST_ASSERT_TRUE(event_range(kind) <= event_range(EV_SHOT));
    }
    TEST_ASSERT_TRUE(event_range(EV_PLAYER_HURT) > 0.0f);
    TEST_ASSERT_TRUE(event_range(EV_ENEMY_HURT) > 0.0f);
    TEST_ASSERT_TRUE(event_range(EV_ENEMY_DEATH) > 0.0f);
    TEST_ASSERT_TRUE(event_range(EV_DOOR) > 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, event_range(EV_PICKUP));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, event_range(EV_NO_AMMO));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, event_range(EV_PLAYER_DEATH));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_same_event_twice_in_a_tick_is_kept_once);
    RUN_TEST(test_another_kind_or_actor_is_another_event);
    RUN_TEST(test_the_same_event_in_the_next_tick_is_posted_again);
    RUN_TEST(test_a_full_queue_drops_what_comes_after);
    RUN_TEST(test_a_players_own_events_reach_them_whole_from_any_distance);
    RUN_TEST(test_another_players_pickup_and_empty_click_are_not_heard);
    RUN_TEST(test_another_players_shot_fades_out_at_its_range);
    RUN_TEST(test_an_enemy_or_door_with_the_listeners_number_is_not_theirs);
    RUN_TEST(test_a_death_belongs_to_the_victim_and_the_killer);
    RUN_TEST(test_a_shot_carries_farthest);
    return UNITY_END();
}
