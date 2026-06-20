#ifndef ENGINE_H
#define ENGINE_H

#include "utils.h"
#include <SDL.h>

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *screen_texture;
    Framebuffer fb;
    int running;
    double fps;
} Engine;

int engine_init(Engine *e);
void engine_shutdown(Engine *e);
void engine_present(Engine *e);
void fb_clear(Framebuffer *fb, uint32_t color);
void fb_vline(Framebuffer *fb, int x, int y0, int y1, uint32_t color);

#endif