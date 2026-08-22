/* game.c: the menu/pause/dead/win state machine.
 *
 * game_handle_event returns 1 when it consumed the event, so the caller knows
 * whether gameplay input should still see it. The overlays and the bitmap font
 * in the same file are pixel output and are checked by eye through
 * driver.sh, not here.
 *
 * game.c compiles against the SDL2 headers but calls no SDL function, so the
 * Engine below never needs a window. */

#include "unity.h"
#include "game.h"
#include "engine.h"
#include "input.h"
#include <SDL.h>
#include <string.h>

static Game game;
static Engine engine;
static InputState in;

static int send_key(SDL_Keycode sym)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = sym;
    return game_handle_event(&game, &in, &ev);
}

static void start_in(GameState state)
{
    game.state = state;
    game.restart = 0;
    game.quit = 0;
}

void setUp(void)
{
    memset(&engine, 0, sizeof(engine));
    engine.running = 1;
    memset(&in, 0, sizeof(in));
    game_init(&game, &engine);
}

void tearDown(void)
{}

static void test_game_init_starts_in_the_menu(void)
{
    TEST_ASSERT_EQUAL_INT(GSTATE_MENU, game.state);
    TEST_ASSERT_EQUAL_INT(0, game.restart);
    TEST_ASSERT_EQUAL_INT(0, game.quit);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, game.menu_timer);
    TEST_ASSERT_EQUAL_PTR(&engine, game.eng);
}

/* Starting a game from the menu also asks for a restart, which is what
 * rebuilds the level. */
static void test_menu_starts_the_game(void)
{
    TEST_ASSERT_EQUAL_INT(1, send_key(SDLK_RETURN));
    TEST_ASSERT_EQUAL_INT(GSTATE_PLAYING, game.state);
    TEST_ASSERT_EQUAL_INT(1, game.restart);

    start_in(GSTATE_MENU);
    TEST_ASSERT_EQUAL_INT(1, send_key(SDLK_SPACE));
    TEST_ASSERT_EQUAL_INT(GSTATE_PLAYING, game.state);
    TEST_ASSERT_EQUAL_INT(1, game.restart);
}

static void test_escape_in_the_menu_quits_the_program(void)
{
    TEST_ASSERT_EQUAL_INT(1, send_key(SDLK_ESCAPE));
    TEST_ASSERT_EQUAL_INT(1, game.quit);
    TEST_ASSERT_EQUAL_INT(0, engine.running);
}

static void test_escape_while_playing_pauses(void)
{
    start_in(GSTATE_PLAYING);
    TEST_ASSERT_EQUAL_INT(1, send_key(SDLK_ESCAPE));
    TEST_ASSERT_EQUAL_INT(GSTATE_PAUSED, game.state);
    TEST_ASSERT_EQUAL_INT(0, game.restart);
    TEST_ASSERT_EQUAL_INT(1, engine.running);
}

/* Mute is consumed by the FSM but acted on by the caller, so the state must
 * not change. */
static void test_mute_keys_are_consumed_while_playing(void)
{
    start_in(GSTATE_PLAYING);
    TEST_ASSERT_EQUAL_INT(1, send_key(SDLK_m));
    TEST_ASSERT_EQUAL_INT(GSTATE_PLAYING, game.state);

    TEST_ASSERT_EQUAL_INT(1, send_key(SDLK_F2));
    TEST_ASSERT_EQUAL_INT(GSTATE_PLAYING, game.state);
}

static void test_gameplay_keys_pass_through_while_playing(void)
{
    start_in(GSTATE_PLAYING);
    TEST_ASSERT_EQUAL_INT(0, send_key(SDLK_w));
    TEST_ASSERT_EQUAL_INT(0, send_key(SDLK_SPACE));
    TEST_ASSERT_EQUAL_INT(0, send_key(SDLK_e));
    TEST_ASSERT_EQUAL_INT(GSTATE_PLAYING, game.state);
}

static void test_pause_resumes_without_a_restart(void)
{
    start_in(GSTATE_PAUSED);
    TEST_ASSERT_EQUAL_INT(1, send_key(SDLK_RETURN));
    TEST_ASSERT_EQUAL_INT(GSTATE_PLAYING, game.state);
    TEST_ASSERT_EQUAL_INT(0, game.restart);

    start_in(GSTATE_PAUSED);
    TEST_ASSERT_EQUAL_INT(1, send_key(SDLK_p));
    TEST_ASSERT_EQUAL_INT(GSTATE_PLAYING, game.state);
    TEST_ASSERT_EQUAL_INT(0, game.restart);
}

/* Escape out of a pause abandons the run rather than quitting the program. */
static void test_escape_while_paused_returns_to_the_menu(void)
{
    start_in(GSTATE_PAUSED);
    TEST_ASSERT_EQUAL_INT(1, send_key(SDLK_ESCAPE));
    TEST_ASSERT_EQUAL_INT(GSTATE_MENU, game.state);
    TEST_ASSERT_EQUAL_INT(1, game.restart);
    TEST_ASSERT_EQUAL_INT(0, game.quit);
    TEST_ASSERT_EQUAL_INT(1, engine.running);
}

static void test_every_key_out_of_dead_and_win_returns_to_the_menu(void)
{
    const SDL_Keycode keys[] = {SDLK_RETURN, SDLK_SPACE, SDLK_ESCAPE};
    const GameState ends[] = {GSTATE_DEAD, GSTATE_WIN};

    for (int e = 0; e < 2; e++) {
        for (int k = 0; k < 3; k++) {
            start_in(ends[e]);
            TEST_ASSERT_EQUAL_INT(1, send_key(keys[k]));
            TEST_ASSERT_EQUAL_INT(GSTATE_MENU, game.state);
            TEST_ASSERT_EQUAL_INT(1, game.restart);
            TEST_ASSERT_EQUAL_INT(0, game.quit);
        }
    }
}

static void test_unhandled_keys_leave_the_state_alone(void)
{
    const GameState states[] = {GSTATE_MENU, GSTATE_PLAYING, GSTATE_PAUSED, GSTATE_DEAD,
                                GSTATE_WIN};
    for (int i = 0; i < 5; i++) {
        start_in(states[i]);
        TEST_ASSERT_EQUAL_INT(0, send_key(SDLK_z));
        TEST_ASSERT_EQUAL_INT(states[i], game.state);
        TEST_ASSERT_EQUAL_INT(0, game.restart);
    }
}

/* Only key-down drives the FSM: a key-up must not, or releasing escape would
 * pause a second time. */
static void test_only_keydown_drives_the_fsm(void)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYUP;
    ev.key.keysym.sym = SDLK_ESCAPE;

    start_in(GSTATE_PLAYING);
    TEST_ASSERT_EQUAL_INT(0, game_handle_event(&game, &in, &ev));
    TEST_ASSERT_EQUAL_INT(GSTATE_PLAYING, game.state);
}

static void test_game_update_advances_the_menu_timer(void)
{
    game_update(&game, 0.5);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, game.menu_timer);
    game_update(&game, 0.25);
    TEST_ASSERT_EQUAL_FLOAT(0.75f, game.menu_timer);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_game_init_starts_in_the_menu);
    RUN_TEST(test_menu_starts_the_game);
    RUN_TEST(test_escape_in_the_menu_quits_the_program);
    RUN_TEST(test_escape_while_playing_pauses);
    RUN_TEST(test_mute_keys_are_consumed_while_playing);
    RUN_TEST(test_gameplay_keys_pass_through_while_playing);
    RUN_TEST(test_pause_resumes_without_a_restart);
    RUN_TEST(test_escape_while_paused_returns_to_the_menu);
    RUN_TEST(test_every_key_out_of_dead_and_win_returns_to_the_menu);
    RUN_TEST(test_unhandled_keys_leave_the_state_alone);
    RUN_TEST(test_only_keydown_drives_the_fsm);
    RUN_TEST(test_game_update_advances_the_menu_timer);
    return UNITY_END();
}
