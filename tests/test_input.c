/* input.c: the edge/hold split. Held keys stay set until the matching key-up;
 * edge triggers survive exactly one frame and are cleared by input_end_frame.
 *
 * input.c compiles against the SDL2 headers for SDL_Event and the SDLK_*
 * constants but calls no SDL function, so nothing is linked against libSDL2
 * here. */

#include "unity.h"
#include "input.h"
#include <SDL.h>
#include <string.h>

static InputState in;
static int running;

static SDL_Event key_event(Uint32 type, SDL_Keycode sym)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.key.keysym.sym = sym;
    return ev;
}

static void press(SDL_Keycode sym)
{
    SDL_Event ev = key_event(SDL_KEYDOWN, sym);
    input_handle_event(&in, &ev, &running);
}

static void release(SDL_Keycode sym)
{
    SDL_Event ev = key_event(SDL_KEYUP, sym);
    input_handle_event(&in, &ev, &running);
}

static void mouse_button(Uint32 type, Uint8 button)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.button.button = button;
    input_handle_event(&in, &ev, &running);
}

static void mouse_move(Sint32 xrel)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_MOUSEMOTION;
    ev.motion.xrel = xrel;
    input_handle_event(&in, &ev, &running);
}

void setUp(void)
{
    input_init(&in);
    running = 1;
}

void tearDown(void)
{}

static void test_input_init_clears_everything(void)
{
    memset(&in, 0xFF, sizeof(in));
    input_init(&in);

    TEST_ASSERT_EQUAL_INT(0, in.forward);
    TEST_ASSERT_EQUAL_INT(0, in.back);
    TEST_ASSERT_EQUAL_INT(0, in.turn_left);
    TEST_ASSERT_EQUAL_INT(0, in.turn_right);
    TEST_ASSERT_EQUAL_INT(0, in.strafe_left);
    TEST_ASSERT_EQUAL_INT(0, in.strafe_right);
    TEST_ASSERT_EQUAL_INT(0, in.mouse_dx);
    TEST_ASSERT_EQUAL_INT(0, in.fire);
    TEST_ASSERT_EQUAL_INT(0, in.fire_pressed);
    TEST_ASSERT_EQUAL_INT(0, in.use);
}

/* Movement keys are held: set on key-down, cleared only on key-up. */
static void test_movement_keys_are_held(void)
{
    press(SDLK_w);
    TEST_ASSERT_EQUAL_INT(1, in.forward);
    input_end_frame(&in);
    TEST_ASSERT_EQUAL_INT(1, in.forward);
    release(SDLK_w);
    TEST_ASSERT_EQUAL_INT(0, in.forward);

    press(SDLK_s);
    press(SDLK_a);
    press(SDLK_d);
    press(SDLK_COMMA);
    press(SDLK_PERIOD);
    TEST_ASSERT_EQUAL_INT(1, in.back);
    TEST_ASSERT_EQUAL_INT(1, in.turn_left);
    TEST_ASSERT_EQUAL_INT(1, in.turn_right);
    TEST_ASSERT_EQUAL_INT(1, in.strafe_left);
    TEST_ASSERT_EQUAL_INT(1, in.strafe_right);

    release(SDLK_s);
    release(SDLK_a);
    release(SDLK_d);
    release(SDLK_COMMA);
    release(SDLK_PERIOD);
    TEST_ASSERT_EQUAL_INT(0, in.back);
    TEST_ASSERT_EQUAL_INT(0, in.turn_left);
    TEST_ASSERT_EQUAL_INT(0, in.turn_right);
    TEST_ASSERT_EQUAL_INT(0, in.strafe_left);
    TEST_ASSERT_EQUAL_INT(0, in.strafe_right);
}

static void test_arrow_keys_mirror_wasd(void)
{
    press(SDLK_UP);
    press(SDLK_DOWN);
    press(SDLK_LEFT);
    press(SDLK_RIGHT);
    TEST_ASSERT_EQUAL_INT(1, in.forward);
    TEST_ASSERT_EQUAL_INT(1, in.back);
    TEST_ASSERT_EQUAL_INT(1, in.turn_left);
    TEST_ASSERT_EQUAL_INT(1, in.turn_right);

    release(SDLK_UP);
    release(SDLK_DOWN);
    release(SDLK_LEFT);
    release(SDLK_RIGHT);
    TEST_ASSERT_EQUAL_INT(0, in.forward);
    TEST_ASSERT_EQUAL_INT(0, in.back);
    TEST_ASSERT_EQUAL_INT(0, in.turn_left);
    TEST_ASSERT_EQUAL_INT(0, in.turn_right);
}

/* Fire is both: a held state and a one-frame edge. */
static void test_fire_is_held_and_edge_triggered(void)
{
    press(SDLK_SPACE);
    TEST_ASSERT_EQUAL_INT(1, in.fire);
    TEST_ASSERT_EQUAL_INT(1, in.fire_pressed);

    input_end_frame(&in);
    TEST_ASSERT_EQUAL_INT(1, in.fire);
    TEST_ASSERT_EQUAL_INT(0, in.fire_pressed);

    release(SDLK_SPACE);
    TEST_ASSERT_EQUAL_INT(0, in.fire);

    /* both control keys work the same way */
    press(SDLK_LCTRL);
    TEST_ASSERT_EQUAL_INT(1, in.fire);
    release(SDLK_LCTRL);
    TEST_ASSERT_EQUAL_INT(0, in.fire);
    press(SDLK_RCTRL);
    TEST_ASSERT_EQUAL_INT(1, in.fire);
    release(SDLK_RCTRL);
    TEST_ASSERT_EQUAL_INT(0, in.fire);
}

static void test_edge_triggers_last_one_frame(void)
{
    press(SDLK_1);
    press(SDLK_2);
    press(SDLK_e);
    press(SDLK_m);
    press(SDLK_p);
    TEST_ASSERT_EQUAL_INT(1, in.switch1);
    TEST_ASSERT_EQUAL_INT(1, in.switch2);
    TEST_ASSERT_EQUAL_INT(1, in.use);
    TEST_ASSERT_EQUAL_INT(1, in.mute);
    TEST_ASSERT_EQUAL_INT(1, in.pause_toggle);

    input_end_frame(&in);
    TEST_ASSERT_EQUAL_INT(0, in.switch1);
    TEST_ASSERT_EQUAL_INT(0, in.switch2);
    TEST_ASSERT_EQUAL_INT(0, in.use);
    TEST_ASSERT_EQUAL_INT(0, in.mute);
    TEST_ASSERT_EQUAL_INT(0, in.pause_toggle);
}

/* Holding an edge key down does not need a key-up to fire again: every
 * key-down event re-arms it. */
static void test_an_edge_key_rearms_on_the_next_press(void)
{
    press(SDLK_e);
    input_end_frame(&in);
    TEST_ASSERT_EQUAL_INT(0, in.use);
    press(SDLK_e);
    TEST_ASSERT_EQUAL_INT(1, in.use);
}

static void test_mouse_fires_like_the_fire_key(void)
{
    mouse_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
    TEST_ASSERT_EQUAL_INT(1, in.fire);
    TEST_ASSERT_EQUAL_INT(1, in.fire_pressed);

    mouse_button(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT);
    TEST_ASSERT_EQUAL_INT(0, in.fire);

    /* other buttons are not the trigger */
    mouse_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
    TEST_ASSERT_EQUAL_INT(0, in.fire);
}

/* Motion accumulates over the frame rather than overwriting. */
static void test_mouse_motion_accumulates_until_end_of_frame(void)
{
    mouse_move(10);
    mouse_move(-4);
    mouse_move(7);
    TEST_ASSERT_EQUAL_INT(13, in.mouse_dx);

    input_end_frame(&in);
    TEST_ASSERT_EQUAL_INT(0, in.mouse_dx);
}

static void test_quit_and_escape_stop_the_loop(void)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_QUIT;
    input_handle_event(&in, &ev, &running);
    TEST_ASSERT_EQUAL_INT(0, running);

    running = 1;
    press(SDLK_ESCAPE);
    TEST_ASSERT_EQUAL_INT(0, running);
}

static void test_unmapped_keys_and_events_are_ignored(void)
{
    InputState before = in;
    press(SDLK_z);
    release(SDLK_z);
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &in, sizeof(in)));
    TEST_ASSERT_EQUAL_INT(1, running);

    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_WINDOWEVENT;
    input_handle_event(&in, &ev, &running);
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &in, sizeof(in)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_input_init_clears_everything);
    RUN_TEST(test_movement_keys_are_held);
    RUN_TEST(test_arrow_keys_mirror_wasd);
    RUN_TEST(test_fire_is_held_and_edge_triggered);
    RUN_TEST(test_edge_triggers_last_one_frame);
    RUN_TEST(test_an_edge_key_rearms_on_the_next_press);
    RUN_TEST(test_mouse_fires_like_the_fire_key);
    RUN_TEST(test_mouse_motion_accumulates_until_end_of_frame);
    RUN_TEST(test_quit_and_escape_stop_the_loop);
    RUN_TEST(test_unmapped_keys_and_events_are_ignored);
    return UNITY_END();
}
