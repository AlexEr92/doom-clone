#ifndef INPUT_H
#define INPUT_H

/* SDL_Event is a union tag, so the prototype below can name it without
 * pulling in SDL: player.c reaches this header for InputState, and the
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
#define BTN_WEAPON1 (1u << 6)
#define BTN_WEAPON2 (1u << 7)

/* What one player did during one tick — the only thing world_step() reads.
 * InputState below is the raw SDL state a client accumulates over a frame and
 * never leaves the client; this is the snapshot taken from it per tick.
 *
 * The angle is absolute and computed by the client: turning must not wait for
 * a round trip, so the simulation takes the facing as given. */
typedef struct {
    uint32_t tick;
    uint16_t buttons;
    float angle;
} PlayerInput;

typedef struct InputState {
    int forward, back, turn_left, turn_right, strafe_left, strafe_right;
    int mouse_dx;
    int fire;             /* current held state of fire button */
    int fire_pressed;     /* edge: set on keydown, cleared by consumer */
    int switch1, switch2; /* edge triggers for weapon switch */
    int use;              /* edge: door use (E) */
    int mute;             /* edge: toggle mute (M) */
    int pause_toggle;     /* edge: pause (P) */
} InputState;

void input_init(InputState *in);
void input_handle_event(InputState *in, union SDL_Event *ev, int *running);
void input_end_frame(InputState *in);

#endif
