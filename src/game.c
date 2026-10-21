#include "game.h"
#include "text.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

void game_init(Game *g, Engine *eng)
{
    memset(g, 0, sizeof(*g));
    g->eng = eng;
    g->state = GSTATE_MENU;
}

int game_handle_event(Game *g, InputState *in, SDL_Event *ev)
{
    (void)in;
    if (ev->type == SDL_KEYDOWN) {
        switch (g->state) {
            case GSTATE_MENU:
                if (ev->key.keysym.sym == SDLK_RETURN || ev->key.keysym.sym == SDLK_SPACE) {
                    g->state = GSTATE_PLAYING;
                    g->restart = 1;
                    return 1;
                }
                if (ev->key.keysym.sym == SDLK_ESCAPE) {
                    g->quit = 1;
                    g->eng->running = 0;
                    return 1;
                }
                break;
            case GSTATE_PLAYING:
                if (ev->key.keysym.sym == SDLK_ESCAPE) {
                    g->state = GSTATE_PAUSED;
                    return 1;
                }
                if (ev->key.keysym.sym == SDLK_m || ev->key.keysym.sym == SDLK_F2) {
                    return 1; /* handled by caller for mute */
                }
                break;
            case GSTATE_PAUSED:
                if (ev->key.keysym.sym == SDLK_ESCAPE) {
                    /* quit to menu */
                    g->state = GSTATE_MENU;
                    g->restart = 1;
                    return 1;
                }
                if (ev->key.keysym.sym == SDLK_RETURN || ev->key.keysym.sym == SDLK_p) {
                    g->state = GSTATE_PLAYING;
                    return 1;
                }
                break;
            case GSTATE_DEAD:
                if (ev->key.keysym.sym == SDLK_RETURN || ev->key.keysym.sym == SDLK_SPACE) {
                    g->state = GSTATE_MENU;
                    g->restart = 1;
                    return 1;
                }
                if (ev->key.keysym.sym == SDLK_ESCAPE) {
                    g->state = GSTATE_MENU;
                    g->restart = 1;
                    return 1;
                }
                break;
            case GSTATE_WIN:
                if (ev->key.keysym.sym == SDLK_RETURN || ev->key.keysym.sym == SDLK_SPACE) {
                    g->state = GSTATE_MENU;
                    g->restart = 1;
                    return 1;
                }
                if (ev->key.keysym.sym == SDLK_ESCAPE) {
                    g->state = GSTATE_MENU;
                    g->restart = 1;
                    return 1;
                }
                break;
        }
    }
    return 0;
}

void game_update(Game *g, double dt)
{
    g->menu_timer += (float)dt;
}

static void overlay_dim(Framebuffer *fb, float alpha)
{
    uint32_t dim = make_color((uint8_t)(10 * alpha), (uint8_t)(10 * alpha), (uint8_t)(15 * alpha));
    /* blend: simple darken by multiplying */
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint32_t c = fb->pixels[i];
        uint8_t r = (uint8_t)(c & 0xFF);
        uint8_t gr = (uint8_t)((c >> 8) & 0xFF);
        uint8_t b = (uint8_t)((c >> 16) & 0xFF);
        r = (uint8_t)(r * (1.0f - alpha));
        gr = (uint8_t)(gr * (1.0f - alpha));
        b = (uint8_t)(b * (1.0f - alpha));
        fb->pixels[i] = make_color(r, gr, b);
    }
    (void)dim;
}

/* A dead player in a game that goes on without them: the world is still drawn
 * and still moving, dimmed a little, from where they fell. Who killed them —
 * a player's id until players have names — and how long until they are back,
 * which the world counts down. */
static void draw_respawn(Framebuffer *fb, const PlayerState *pl)
{
    overlay_dim(fb, 0.35f);
    uint32_t red = make_color(220, 40, 40);
    uint32_t white = make_color(240, 240, 240);
    char line[32];
    text_draw_centered(fb, 120, "YOU DIED", red, 6);
    if (pl->last_attacker >= 0 && pl->last_attacker != pl->id) {
        snprintf(line, sizeof(line), "KILLED BY PLAYER %d", pl->last_attacker);
        text_draw_centered(fb, 200, line, white, 2);
    }
    snprintf(line, sizeof(line), "RESPAWN IN %d", (int)ceilf(pl->respawn_timer));
    text_draw_centered(fb, 230, line, white, 2);
}

void game_draw_overlay(Framebuffer *fb, const Game *g, const PlayerState *pl)
{
    switch (g->state) {
        case GSTATE_PLAYING: {
            if (pl && !pl->alive) {
                draw_respawn(fb, pl);
            }
            break;
        }
        case GSTATE_MENU: {
            overlay_dim(fb, 0.85f);
            uint32_t red = make_color(220, 40, 40);
            uint32_t white = make_color(240, 240, 240);
            uint32_t yellow = make_color(240, 220, 80);
            text_draw_centered(fb, 60, "DOOM CLONE", red, 6);
            text_draw_centered(fb, 130, "A RAYCAST DEMO", white, 2);
            /* blink "PRESS ENTER" */
            if ((int)(g->menu_timer * 2.0f) % 2 == 0) {
                text_draw_centered(fb, 200, "PRESS ENTER TO START", yellow, 2);
            }
            text_draw_centered(fb, 250, "WASD MOVE  MOUSE LOOK", white, 2);
            text_draw_centered(fb, 272, "SPACE FIRE  1 2 WEAPON", white, 2);
            text_draw_centered(fb, 294, "E USE DOOR  M MUTE", white, 2);
            text_draw_centered(fb, 316, "ESC PAUSE OR QUIT", white, 2);
            break;
        }
        case GSTATE_PAUSED: {
            overlay_dim(fb, 0.6f);
            uint32_t white = make_color(240, 240, 240);
            uint32_t yellow = make_color(240, 220, 80);
            text_draw_centered(fb, SCREEN_H / 2 - 30, "PAUSED", yellow, 5);
            text_draw_centered(fb, SCREEN_H / 2 + 20, "ENTER RESUME", white, 2);
            text_draw_centered(fb, SCREEN_H / 2 + 40, "ESC QUIT TO MENU", white, 2);
            break;
        }
        case GSTATE_DEAD: {
            overlay_dim(fb, 0.7f);
            uint32_t red = make_color(220, 40, 40);
            uint32_t white = make_color(240, 240, 240);
            text_draw_centered(fb, 120, "YOU DIED", red, 6);
            text_draw_centered(fb, 200, "PRESS ENTER OR ESC", white, 2);
            text_draw_centered(fb, 220, "TO RETURN TO MENU", white, 2);
            (void)pl;
            break;
        }
        case GSTATE_WIN: {
            overlay_dim(fb, 0.5f);
            uint32_t green = make_color(80, 220, 80);
            uint32_t white = make_color(240, 240, 240);
            text_draw_centered(fb, 120, "VICTORY", green, 6);
            text_draw_centered(fb, 200, "ALL ENEMIES ELIMINATED", white, 2);
            text_draw_centered(fb, 230, "PRESS ENTER OR ESC", white, 2);
            text_draw_centered(fb, 250, "TO RETURN TO MENU", white, 2);
            break;
        }
        default: break;
    }
}
