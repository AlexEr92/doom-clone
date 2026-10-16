/* input.c: the raw per-frame InputState and the per-tick PlayerInput taken
 * from it. Held keys stay set until the matching key-up. Tick edges (fire,
 * use, weapon switch) stay set until input_sample() hands them to exactly one
 * tick, however many ticks a frame holds and even when it holds none; frame
 * edges (mute, pause) are cleared by input_end_frame(). Turning is done here
 * too, once a frame, by input_turn().
 *
 * input.c compiles against the SDL2 headers for SDL_Event and the SDLK_*
 * constants but calls no SDL function, so nothing is linked against libSDL2
 * here. */

#include "unity.h"
#include "input.h"
#include "utils.h"
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
    TEST_ASSERT_EQUAL_FLOAT(0.0f, in.angle);
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

/* Fire is both: a held state and a tick edge. */
static void test_fire_is_held_and_edge_triggered(void)
{
    press(SDLK_SPACE);
    TEST_ASSERT_EQUAL_INT(1, in.fire);
    TEST_ASSERT_EQUAL_INT(1, in.fire_pressed);

    input_sample(&in, 0);
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

/* Mute and pause are the client's, read once a frame, and die with it. */
static void test_frame_edges_last_one_frame(void)
{
    press(SDLK_m);
    press(SDLK_p);
    TEST_ASSERT_EQUAL_INT(1, in.mute);
    TEST_ASSERT_EQUAL_INT(1, in.pause_toggle);

    input_end_frame(&in);
    TEST_ASSERT_EQUAL_INT(0, in.mute);
    TEST_ASSERT_EQUAL_INT(0, in.pause_toggle);
}

/* At more frames than ticks a frame can pass with no tick in it, and a press
 * made in it belongs to the next tick rather than to nobody. */
static void test_tick_edges_outlive_a_frame_with_no_tick(void)
{
    press(SDLK_1);
    press(SDLK_2);
    press(SDLK_e);
    press(SDLK_SPACE);
    release(SDLK_SPACE);
    input_end_frame(&in);
    TEST_ASSERT_EQUAL_INT(1, in.switch1);
    TEST_ASSERT_EQUAL_INT(1, in.switch2);
    TEST_ASSERT_EQUAL_INT(1, in.use);
    TEST_ASSERT_EQUAL_INT(1, in.fire_pressed);

    PlayerInput pi = input_sample(&in, 0);
    TEST_ASSERT_EQUAL_HEX16(BTN_WEAPON1 | BTN_WEAPON2 | BTN_USE | BTN_FIRE, pi.buttons);
}

/* Holding an edge key down does not need a key-up to fire again: every
 * key-down event re-arms it. */
static void test_an_edge_key_rearms_on_the_next_press(void)
{
    press(SDLK_e);
    input_sample(&in, 0);
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

/* ---- input_sample(): one PlayerInput per tick ---- */

/* The mask comes from the same keys that drove the game before it existed. The
 * turn keys are not in it: turning reaches a tick as the angle. */
static void test_the_mask_is_built_from_the_keys(void)
{
    press(SDLK_w);
    press(SDLK_s);
    press(SDLK_COMMA);
    press(SDLK_PERIOD);
    press(SDLK_SPACE);
    press(SDLK_e);
    press(SDLK_1);
    press(SDLK_2);
    press(SDLK_a);
    press(SDLK_d);

    PlayerInput pi = input_sample(&in, 0);
    TEST_ASSERT_EQUAL_HEX16(BTN_FORWARD | BTN_BACK | BTN_LEFT | BTN_RIGHT | BTN_FIRE | BTN_USE |
                                    BTN_WEAPON1 | BTN_WEAPON2,
                            pi.buttons);

    PlayerInput idle;
    input_init(&in);
    idle = input_sample(&in, 0);
    TEST_ASSERT_EQUAL_HEX16(0, idle.buttons);
}

static void test_a_sample_carries_its_tick_and_the_angle(void)
{
    in.angle = 1.25f;
    PlayerInput pi = input_sample(&in, 4242);
    TEST_ASSERT_EQUAL_UINT32(4242, pi.tick);
    TEST_ASSERT_EQUAL_FLOAT(1.25f, pi.angle);
}

/* A frame holding three ticks samples three times. An edge goes to the first
 * of them and to no other: door_try_use() toggles, so the same press of E in
 * two ticks would open the door and shut it again. Held keys reach every one. */
static void test_an_edge_reaches_exactly_one_tick_of_a_frame(void)
{
    press(SDLK_w);
    press(SDLK_e);
    press(SDLK_1);
    press(SDLK_SPACE);
    release(SDLK_SPACE);

    PlayerInput t[3];
    for (uint32_t i = 0; i < 3; i++) {
        t[i] = input_sample(&in, 100 + i);
    }

    TEST_ASSERT_EQUAL_HEX16(BTN_FORWARD | BTN_USE | BTN_WEAPON1 | BTN_FIRE, t[0].buttons);
    TEST_ASSERT_EQUAL_HEX16(BTN_FORWARD, t[1].buttons);
    TEST_ASSERT_EQUAL_HEX16(BTN_FORWARD, t[2].buttons);
    TEST_ASSERT_EQUAL_UINT32(100, t[0].tick);
    TEST_ASSERT_EQUAL_UINT32(101, t[1].tick);
    TEST_ASSERT_EQUAL_UINT32(102, t[2].tick);
}

/* Held, the trigger is pressed in every tick and the weapon's cooldown sets the
 * pace of fire; only a tap is limited to one tick. */
static void test_held_fire_reaches_every_tick(void)
{
    press(SDLK_SPACE);
    for (uint32_t i = 0; i < 3; i++) {
        PlayerInput pi = input_sample(&in, i);
        TEST_ASSERT_EQUAL_HEX16(BTN_FIRE, pi.buttons);
    }
}

/* At a low frame rate a quick tap lands its key-down and key-up in one sweep of
 * the event queue: by the time a tick looks, nothing is held any more. The
 * press must fire all the same. */
static void test_a_tap_released_before_the_tick_still_fires(void)
{
    press(SDLK_SPACE);
    release(SDLK_SPACE);
    mouse_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
    mouse_button(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT);
    TEST_ASSERT_EQUAL_INT(0, in.fire);

    PlayerInput pi = input_sample(&in, 0);
    TEST_ASSERT_EQUAL_HEX16(BTN_FIRE, pi.buttons);
    pi = input_sample(&in, 1);
    TEST_ASSERT_EQUAL_HEX16(0, pi.buttons);
}

/* While nothing is simulated a press has no tick to go to and is dropped, but
 * what is held down stays held. */
static void test_cleared_edges_reach_no_tick(void)
{
    press(SDLK_w);
    press(SDLK_e);
    press(SDLK_2);
    press(SDLK_SPACE);
    release(SDLK_SPACE);

    input_clear_edges(&in);
    PlayerInput pi = input_sample(&in, 0);
    TEST_ASSERT_EQUAL_HEX16(BTN_FORWARD, pi.buttons);
}

/* ---- input_turn(): the client's facing ---- */

static void test_the_turn_keys_turn_by_rot_speed(void)
{
    press(SDLK_a);
    input_turn(&in, 0.1);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ROT_SPEED * 0.1f, in.angle);

    release(SDLK_a);
    press(SDLK_d);
    input_turn(&in, 0.1);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, in.angle);

    /* both held cancel out */
    press(SDLK_a);
    input_turn(&in, 0.1);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, in.angle);
}

/* Mouse right turns right, which is the negative direction. The motion is
 * turned through once: a second call in the same frame adds nothing. */
static void test_the_mouse_turns_by_its_sensitivity_once(void)
{
    mouse_move(60);
    mouse_move(40);
    input_turn(&in, 1.0 / 60.0);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -100.0f * MOUSE_SENS, in.angle);
    TEST_ASSERT_EQUAL_INT(0, in.mouse_dx);

    input_turn(&in, 1.0 / 60.0);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -100.0f * MOUSE_SENS, in.angle);
}

/* The angle is what goes on the wire, so it stays in [-PI, PI] for the
 * shortest-arc interpolation between snapshots. */
static void test_turning_keeps_the_angle_wrapped(void)
{
    in.angle = MATH_PI - 0.01f;
    press(SDLK_a);
    for (int i = 0; i < 100; i++) {
        input_turn(&in, 1.0 / 60.0);
        TEST_ASSERT_TRUE(in.angle >= -MATH_PI && in.angle <= MATH_PI);
    }
    release(SDLK_a);

    mouse_move(100000);
    input_turn(&in, 0.0);
    TEST_ASSERT_TRUE(in.angle >= -MATH_PI && in.angle <= MATH_PI);
}

/* The turn of a frame is done before its ticks, so every one of them carries
 * it — not the first tick the whole turn and the rest none. */
static void test_every_tick_of_a_frame_carries_the_turned_angle(void)
{
    mouse_move(250);
    input_turn(&in, 1.0 / 20.0);
    PlayerInput a = input_sample(&in, 0);
    PlayerInput b = input_sample(&in, 1);
    PlayerInput c = input_sample(&in, 2);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -250.0f * MOUSE_SENS, a.angle);
    TEST_ASSERT_EQUAL_FLOAT(a.angle, b.angle);
    TEST_ASSERT_EQUAL_FLOAT(a.angle, c.angle);
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
    RUN_TEST(test_frame_edges_last_one_frame);
    RUN_TEST(test_tick_edges_outlive_a_frame_with_no_tick);
    RUN_TEST(test_an_edge_key_rearms_on_the_next_press);
    RUN_TEST(test_mouse_fires_like_the_fire_key);
    RUN_TEST(test_mouse_motion_accumulates_until_end_of_frame);
    RUN_TEST(test_the_mask_is_built_from_the_keys);
    RUN_TEST(test_a_sample_carries_its_tick_and_the_angle);
    RUN_TEST(test_an_edge_reaches_exactly_one_tick_of_a_frame);
    RUN_TEST(test_held_fire_reaches_every_tick);
    RUN_TEST(test_a_tap_released_before_the_tick_still_fires);
    RUN_TEST(test_cleared_edges_reach_no_tick);
    RUN_TEST(test_the_turn_keys_turn_by_rot_speed);
    RUN_TEST(test_the_mouse_turns_by_its_sensitivity_once);
    RUN_TEST(test_turning_keeps_the_angle_wrapped);
    RUN_TEST(test_every_tick_of_a_frame_carries_the_turned_angle);
    RUN_TEST(test_quit_and_escape_stop_the_loop);
    RUN_TEST(test_unmapped_keys_and_events_are_ignored);
    return UNITY_END();
}
