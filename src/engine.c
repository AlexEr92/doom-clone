#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int engine_init(Engine *e) {
    memset(e, 0, sizeof(*e));
    e->running = 1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    e->window = SDL_CreateWindow("doom-clone",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 WINDOW_W, WINDOW_H,
                                 SDL_WINDOW_SHOWN);
    if (!e->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    e->renderer = SDL_CreateRenderer(e->window, -1,
                                     SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!e->renderer) {
        e->renderer = SDL_CreateRenderer(e->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!e->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }

    e->screen_texture = SDL_CreateTexture(e->renderer,
                                          SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          SCREEN_W, SCREEN_H);
    if (!e->screen_texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return -1;
    }

    e->fb.pixels = (uint32_t *)malloc((size_t)SCREEN_W * SCREEN_H * sizeof(uint32_t));
    if (!e->fb.pixels) {
        fprintf(stderr, "Failed to allocate framebuffer\n");
        return -1;
    }

    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "1");
    SDL_SetRelativeMouseMode(SDL_TRUE);
    return 0;
}

void engine_shutdown(Engine *e) {
    free(e->fb.pixels);
    e->fb.pixels = NULL;
    if (e->screen_texture) SDL_DestroyTexture(e->screen_texture);
    if (e->renderer) SDL_DestroyRenderer(e->renderer);
    if (e->window) SDL_DestroyWindow(e->window);
    SDL_Quit();
}

void engine_present(Engine *e) {
    SDL_UpdateTexture(e->screen_texture, NULL, e->fb.pixels,
                      SCREEN_W * (int)sizeof(uint32_t));
    SDL_RenderClear(e->renderer);
    SDL_RenderCopy(e->renderer, e->screen_texture, NULL, NULL);
    SDL_RenderPresent(e->renderer);
}

void fb_clear(Framebuffer *fb, uint32_t color) {
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        fb->pixels[i] = color;
    }
}

void fb_vline(Framebuffer *fb, int x, int y0, int y1, uint32_t color) {
    if (x < 0 || x >= SCREEN_W) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= SCREEN_H) y1 = SCREEN_H - 1;
    if (y0 > y1) return;
    uint32_t *p = fb->pixels + (size_t)y0 * SCREEN_W + x;
    for (int y = y0; y <= y1; y++) {
        *p = color;
        p += SCREEN_W;
    }
}