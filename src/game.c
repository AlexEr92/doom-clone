#include "game.h"
#include "utils.h"
#include <string.h>
#include <math.h>

/* 3x5 uppercase font for the letters we use. Indexed by char 'A'..'Z' plus
 * space, ':', '%', '-', '!' handled specially. Each glyph: 5 rows of 3 bits. */
static const uint8_t glyphs[26][5] = {
    /* A */ {0b111, 0b101, 0b111, 0b101, 0b101},
    /* B */ {0b110, 0b101, 0b110, 0b101, 0b110},
    /* C */ {0b111, 0b100, 0b100, 0b100, 0b111},
    /* D */ {0b110, 0b101, 0b101, 0b101, 0b110},
    /* E */ {0b111, 0b100, 0b111, 0b100, 0b111},
    /* F */ {0b111, 0b100, 0b111, 0b100, 0b100},
    /* G */ {0b111, 0b100, 0b101, 0b101, 0b111},
    /* H */ {0b101, 0b101, 0b111, 0b101, 0b101},
    /* I */ {0b111, 0b010, 0b010, 0b010, 0b111},
    /* J */ {0b001, 0b001, 0b001, 0b101, 0b111},
    /* K */ {0b101, 0b110, 0b100, 0b110, 0b101},
    /* L */ {0b100, 0b100, 0b100, 0b100, 0b111},
    /* M */ {0b101, 0b111, 0b111, 0b101, 0b101},
    /* N */ {0b101, 0b111, 0b111, 0b111, 0b101},
    /* O */ {0b111, 0b101, 0b101, 0b101, 0b111},
    /* P */ {0b111, 0b101, 0b111, 0b100, 0b100},
    /* Q */ {0b111, 0b101, 0b101, 0b111, 0b011},
    /* R */ {0b111, 0b101, 0b110, 0b101, 0b101},
    /* S */ {0b111, 0b100, 0b111, 0b001, 0b111},
    /* T */ {0b111, 0b010, 0b010, 0b010, 0b010},
    /* U */ {0b101, 0b101, 0b101, 0b101, 0b111},
    /* V */ {0b101, 0b101, 0b101, 0b101, 0b010},
    /* W */ {0b101, 0b101, 0b111, 0b111, 0b101},
    /* X */ {0b101, 0b101, 0b010, 0b101, 0b101},
    /* Y */ {0b101, 0b101, 0b010, 0b010, 0b010},
    /* Z */ {0b111, 0b001, 0b010, 0b100, 0b111},
};

static const uint8_t glyph_digits[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111},
    {0b010, 0b110, 0b010, 0b010, 0b111},
    {0b111, 0b001, 0b111, 0b100, 0b111},
    {0b111, 0b001, 0b111, 0b001, 0b111},
    {0b101, 0b101, 0b111, 0b001, 0b001},
    {0b111, 0b100, 0b111, 0b001, 0b111},
    {0b111, 0b100, 0b111, 0b101, 0b111},
    {0b111, 0b001, 0b010, 0b010, 0b010},
    {0b111, 0b101, 0b111, 0b101, 0b111},
    {0b111, 0b101, 0b111, 0b001, 0b111},
};

static void draw_char(Framebuffer *fb, int x0, int y0, char ch, uint32_t color, int scale) {
    const uint8_t (*g)[5] = NULL;
    if (ch >= 'A' && ch <= 'Z') g = &glyphs[ch - 'A'];
    else if (ch >= 'a' && ch <= 'z') g = &glyphs[ch - 'a'];
    else if (ch >= '0' && ch <= '9') g = &glyph_digits[ch - '0'];
    else if (ch == '!' ) {
        /* exclamation: center column, gap, dot */
        for (int r = 0; r < 4; r++)
            for (int dx = 0; dx < 3; dx++)
                if (dx == 1) for (int dy = 0; dy < scale; dy++)
                    for (int dx2 = 0; dx2 < scale; dx2++)
                        fb->pixels[(size_t)(y0 + r * scale + dy) * SCREEN_W + (x0 + dx * scale + dx2)] = color;
        return;
    } else if (ch == '-' ) {
        for (int dx = 0; dx < 3; dx++)
            for (int dy = 0; dy < scale; dy++)
                for (int dx2 = 0; dx2 < scale; dx2++)
                    fb->pixels[(size_t)(y0 + 2 * scale + dy) * SCREEN_W + (x0 + dx * scale + dx2)] = color;
        return;
    } else if (ch == ':') {
        for (int dx = 0; dx < 3; dx++) if (dx == 1)
            for (int dy = 0; dy < scale; dy++)
                for (int dx2 = 0; dx2 < scale; dx2++) {
                    fb->pixels[(size_t)(y0 + scale + dy) * SCREEN_W + (x0 + dx * scale + dx2)] = color;
                    fb->pixels[(size_t)(y0 + 3 * scale + dy) * SCREEN_W + (x0 + dx * scale + dx2)] = color;
                }
        return;
    }
    /* space or unknown: blank */
    if (!g) return;
    for (int r = 0; r < 5; r++) {
        uint8_t row = (*g)[r];
        for (int c = 0; c < 3; c++) {
            /* Glyph masks are written MSB=left (natural digit reading), so
             * column 0 (leftmost) corresponds to bit 2. */
            if (row & (1 << (2 - c))) {
                int px = x0 + c * scale;
                int py = y0 + r * scale;
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++) {
                        int X = px + dx, Y = py + dy;
                        if (X < 0 || X >= SCREEN_W || Y < 0 || Y >= SCREEN_H) continue;
                        fb->pixels[(size_t)Y * SCREEN_W + X] = color;
                    }
            }
        }
    }
}

static void draw_text(Framebuffer *fb, int x0, int y0, const char *s, uint32_t color, int scale) {
    int x = x0;
    for (const char *p = s; *p; p++) {
        if (*p == ' ') { x += 4 * scale; continue; }
        draw_char(fb, x, y0, *p, color, scale);
        x += 4 * scale;
    }
}

static int text_width(const char *s, int scale) {
    int w = 0;
    for (const char *p = s; *p; p++) w += (*p == ' ') ? 4 * scale : 4 * scale;
    return w - scale;
}

static void draw_centered(Framebuffer *fb, int y, const char *s, uint32_t color, int scale) {
    int w = text_width(s, scale);
    draw_text(fb, SCREEN_W / 2 - w / 2, y, s, color, scale);
}

void game_init(Game *g, Engine *eng) {
    memset(g, 0, sizeof(*g));
    g->eng = eng;
    g->state = GSTATE_MENU;
}

int game_handle_event(Game *g, InputState *in, SDL_Event *ev) {
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

void game_update(Game *g, double dt) {
    g->menu_timer += (float)dt;
}

static void overlay_dim(Framebuffer *fb, float alpha) {
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

void game_draw_overlay(Framebuffer *fb, const Game *g, const Player *pl) {
    switch (g->state) {
        case GSTATE_MENU: {
            overlay_dim(fb, 0.85f);
            uint32_t red = make_color(220, 40, 40);
            uint32_t white = make_color(240, 240, 240);
            uint32_t yellow = make_color(240, 220, 80);
            draw_centered(fb, 60, "DOOM CLONE", red, 6);
            draw_centered(fb, 130, "A RAYCAST DEMO", white, 2);
            /* blink "PRESS ENTER" */
            if ((int)(g->menu_timer * 2.0f) % 2 == 0) {
                draw_centered(fb, 200, "PRESS ENTER TO START", yellow, 2);
            }
            draw_centered(fb, 250, "WASD MOVE  MOUSE LOOK", white, 2);
            draw_centered(fb, 272, "SPACE FIRE  1 2 WEAPON", white, 2);
            draw_centered(fb, 294, "E USE DOOR  M MUTE", white, 2);
            draw_centered(fb, 316, "ESC PAUSE OR QUIT", white, 2);
            break;
        }
        case GSTATE_PAUSED: {
            overlay_dim(fb, 0.6f);
            uint32_t white = make_color(240, 240, 240);
            uint32_t yellow = make_color(240, 220, 80);
            draw_centered(fb, SCREEN_H / 2 - 30, "PAUSED", yellow, 5);
            draw_centered(fb, SCREEN_H / 2 + 20, "ENTER RESUME", white, 2);
            draw_centered(fb, SCREEN_H / 2 + 40, "ESC QUIT TO MENU", white, 2);
            break;
        }
        case GSTATE_DEAD: {
            overlay_dim(fb, 0.7f);
            uint32_t red = make_color(220, 40, 40);
            uint32_t white = make_color(240, 240, 240);
            draw_centered(fb, 120, "YOU DIED", red, 6);
            draw_centered(fb, 200, "PRESS ENTER OR ESC", white, 2);
            draw_centered(fb, 220, "TO RETURN TO MENU", white, 2);
            (void)pl;
            break;
        }
        case GSTATE_WIN: {
            overlay_dim(fb, 0.5f);
            uint32_t green = make_color(80, 220, 80);
            uint32_t white = make_color(240, 240, 240);
            draw_centered(fb, 120, "VICTORY", green, 6);
            draw_centered(fb, 200, "ALL ENEMIES ELIMINATED", white, 2);
            draw_centered(fb, 230, "PRESS ENTER OR ESC", white, 2);
            draw_centered(fb, 250, "TO RETURN TO MENU", white, 2);
            break;
        }
        default:
            break;
    }
}