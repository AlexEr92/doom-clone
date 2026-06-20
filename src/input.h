#ifndef INPUT_H
#define INPUT_H

#include "player.h"
#include <SDL.h>

void input_init(InputState *in);
void input_handle_event(InputState *in, SDL_Event *ev, int *running);
void input_end_frame(InputState *in);

#endif