#include "input.h"
#include <SDL.h>
#include <string.h>

void input_init(InputState *in) {
    memset(in, 0, sizeof(*in));
}

void input_handle_event(InputState *in, SDL_Event *ev, int *running) {
    switch (ev->type) {
        case SDL_QUIT:
            *running = 0;
            break;
        case SDL_KEYDOWN:
            switch (ev->key.keysym.sym) {
                case SDLK_ESCAPE: *running = 0; break;
                case SDLK_w: case SDLK_UP:    in->forward = 1; break;
                case SDLK_s: case SDLK_DOWN:  in->back = 1; break;
                case SDLK_a: case SDLK_LEFT:  in->turn_left = 1; break;
                case SDLK_d: case SDLK_RIGHT: in->turn_right = 1; break;
                case SDLK_COMMA:  in->strafe_left = 1; break;
                case SDLK_PERIOD: in->strafe_right = 1; break;
                default: break;
            }
            break;
        case SDL_KEYUP:
            switch (ev->key.keysym.sym) {
                case SDLK_w: case SDLK_UP:    in->forward = 0; break;
                case SDLK_s: case SDLK_DOWN:  in->back = 0; break;
                case SDLK_a: case SDLK_LEFT:  in->turn_left = 0; break;
                case SDLK_d: case SDLK_RIGHT: in->turn_right = 0; break;
                case SDLK_COMMA:  in->strafe_left = 0; break;
                case SDLK_PERIOD: in->strafe_right = 0; break;
                default: break;
            }
            break;
        case SDL_MOUSEMOTION:
            in->mouse_dx += ev->motion.xrel;
            break;
        default:
            break;
    }
}

void input_end_frame(InputState *in) {
    in->mouse_dx = 0;
}