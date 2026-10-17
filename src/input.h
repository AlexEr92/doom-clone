#ifndef INPUT_H
#define INPUT_H

/* SDL_Event is a union tag, so the prototype below can name it without
 * pulling in SDL: player.c reaches this header for PlayerInput, and the
 * simulation must not depend on SDL. input.c includes SDL for real. */
union SDL_Event;

#include <stdint.h>

/* Buttons of one tick's input, as they go on the wire. */
#define BTN_FORWARD (1u << 0)
#define BTN_BACK (1u << 1)
#define BTN_LEFT (1u << 2) /* strafe */
#define BTN_RIGHT (1u << 3)
#define BTN_FIRE (1u << 4)
#define BTN_USE (1u << 5)
#define BTN_WEAPON1 (1u << 6) /* keys 1-5: WeaponType in order */
#define BTN_WEAPON2 (1u << 7)
#define BTN_WEAPON3 (1u << 8)
#define BTN_WEAPON4 (1u << 9)
#define BTN_WEAPON5 (1u << 10)

/* What one player did during one tick — the only thing world_step() reads.
 * InputState below is the raw SDL state a client accumulates over a frame and
 * never leaves the client; this is the snapshot input_sample() takes from it
 * per tick.
 *
 * The angle is absolute and computed by the client: turning must not wait for
 * a round trip, so the simulation takes the facing as given. */
typedef struct PlayerInput {
    uint32_t tick;
    uint16_t buttons;
    float angle;
} PlayerInput;

/* Held keys stay set until their key-up. Tick edges are set by a key-down and
 * stay set until input_sample() hands them to a tick; frame edges are read by
 * the client itself and last until input_end_frame(). */
typedef struct InputState {
    int forward, back, turn_left, turn_right, strafe_left, strafe_right;
    int mouse_dx;         /* motion input_turn() has not turned through yet */
    float angle;          /* the client's facing, radians in [-PI, PI] */
    int fire;             /* held */
    int fire_pressed;     /* tick edge: a press, even one released already */
    int switch1, switch2; /* tick edge: weapon switch, keys 1-5 */
    int switch3, switch4, switch5;
    int use;          /* tick edge: door use (E) */
    int mute;         /* frame edge: toggle mute (M) */
    int pause_toggle; /* frame edge: pause (P) */
} InputState;

void input_init(InputState *in);
void input_handle_event(InputState *in, union SDL_Event *ev, int *running);

/* Turn the client's facing by what the frame brought: the mouse motion since
 * the last call and the turn keys, held for `dt` seconds. Called once a frame,
 * outside the fixed step — every tick of the frame then carries the same
 * angle, rather than the first one getting the whole turn and the rest none. */
void input_turn(InputState *in, double dt);

/* The input of tick `tick`, as the state stands now. Called once per tick,
 * inside the fixed step. Every tick edge it hands over is cleared on the way
 * out, so a press reaches exactly one tick: two ticks in one frame do not both
 * use one press of E — a door would open and shut again — and a frame with no
 * tick in it keeps the press for the next one. */
PlayerInput input_sample(InputState *in, uint32_t tick);

/* Drop the tick edges without handing them to any tick. For a frame nothing is
 * simulated in: a press made while paused would otherwise fire on the first
 * tick after it. */
void input_clear_edges(InputState *in);

/* Clear what lives for one frame: the frame edges and whatever mouse motion was
 * not turned through. The tick edges are left to input_sample(). Called once a
 * frame, after every tick of it. */
void input_end_frame(InputState *in);

#endif
