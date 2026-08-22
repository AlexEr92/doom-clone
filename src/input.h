#ifndef INPUT_H
#define INPUT_H

/* SDL_Event is a union tag, so the prototype below can name it without
 * pulling in SDL: player.c reaches this header for InputState, and the
 * simulation must not depend on SDL. input.c includes SDL for real. */
union SDL_Event;

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
