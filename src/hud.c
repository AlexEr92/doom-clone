#include "hud.h"
#include "utils.h"
#include <string.h>

/* 3x5 block font for digits 0-9 and a few letters. Each glyph is 5 rows of
 * 3-bit masks (bit0 = left, bit1 = mid, bit2 = right). */
static const uint8_t digits[10][5] = {
    /* 0 */ {0b111, 0b101, 0b101, 0b101, 0b111},
    /* 1 */ {0b010, 0b110, 0b010, 0b010, 0b111},
    /* 2 */ {0b111, 0b001, 0b111, 0b100, 0b111},
    /* 3 */ {0b111, 0b001, 0b111, 0b001, 0b111},
    /* 4 */ {0b101, 0b101, 0b111, 0b001, 0b001},
    /* 5 */ {0b111, 0b100, 0b111, 0b001, 0b111},
    /* 6 */ {0b111, 0b100, 0b111, 0b101, 0b111},
    /* 7 */ {0b111, 0b001, 0b010, 0b010, 0b010},
    /* 8 */ {0b111, 0b101, 0b111, 0b101, 0b111},
    /* 9 */ {0b111, 0b101, 0b111, 0b001, 0b111},
};

static void draw_glyph(Framebuffer *fb, int x0, int y0, int ch, uint32_t color, int scale) {
    if (ch < 0 || ch > 9) return;
    for (int r = 0; r < 5; r++) {
        uint8_t row = digits[ch][r];
        for (int c = 0; c < 3; c++) {
            /* MSB = leftmost column */
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

static void draw_int(Framebuffer *fb, int x0, int y0, int value, uint32_t color, int scale) {
    char buf[16];
    int n = 0;
    if (value < 0) { value = -value; /* draw minus as nothing for simplicity */ }
    if (value == 0) { draw_glyph(fb, x0, y0, 0, color, scale); return; }
    int tmp = value;
    while (tmp > 0) { buf[n++] = (char)('0' + tmp % 10); tmp /= 10; }
    int x = x0 + (n - 1) * 4 * scale;
    for (int i = n - 1; i >= 0; i--) {
        draw_glyph(fb, x, y0, buf[i] - '0', color, scale);
        x -= 4 * scale;
    }
}

void hud_draw_weapon(Framebuffer *fb, const Texture *base, float anim) {
    if (!base || !base->pixels) return;
    int tw = base->w;
    int th = base->h;
    int target_h = (int)(SCREEN_H * 0.40f);
    float scale = (float)target_h / (float)th;
    int draw_w = (int)(tw * scale);
    int draw_h = target_h;
    int x0 = SCREEN_W / 2 - draw_w / 2;
    int y0 = SCREEN_H - draw_h;
    int recoil = (int)(18.0f * anim);
    y0 += recoil;
    if (x0 < 0) x0 = 0;

    for (int y = 0; y < draw_h; y++) {
        int sy = (int)((float)y / scale);
        if (sy < 0 || sy >= th) continue;
        int py = y0 + y;
        if (py < 0 || py >= SCREEN_H) continue;
        for (int x = 0; x < draw_w; x++) {
            int sx = (int)((float)x / scale);
            if (sx < 0 || sx >= tw) continue;
            int px = x0 + x;
            if (px < 0 || px >= SCREEN_W) continue;
            uint32_t c = base->pixels[(size_t)sy * tw + sx];
            if ((c & 0xFF000000u) == 0) continue;
            fb->pixels[(size_t)py * SCREEN_W + px] = c;
        }
    }
}

/* Procedural "face" indicator: health-based expression. */
static void hud_draw_face(Framebuffer *fb, int x0, int y0, int sz, float hp_frac) {
    uint32_t skin = make_color(200, 170, 140);
    uint32_t dark = make_color(40, 30, 25);
    uint32_t eye = make_color(20, 20, 20);
    uint32_t mouth_happy = make_color(120, 40, 40);
    uint32_t mouth_hurt = make_color(180, 30, 30);
    /* background panel */
    for (int y = 0; y < sz; y++)
        for (int x = 0; x < sz; x++)
            fb->pixels[(size_t)(y0 + y) * SCREEN_W + (x0 + x)] = dark;
    /* face area */
    int inset = sz / 8;
    for (int y = inset; y < sz - inset; y++)
        for (int x = inset; x < sz - inset; x++)
            fb->pixels[(size_t)(y0 + y) * SCREEN_W + (x0 + x)] = skin;
    /* eyes */
    int ey = sz / 2 - 2;
    int ex1 = sz / 2 - 3;
    int ex2 = sz / 2 + 2;
    for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++) {
            fb->pixels[(size_t)(y0 + ey + dy) * SCREEN_W + (x0 + ex1 + dx)] = eye;
            fb->pixels[(size_t)(y0 + ey + dy) * SCREEN_W + (x0 + ex2 + dx)] = eye;
        }
    /* mouth: smile if hp>50, flat if 25..50, frown if <25 */
    int my = sz / 2 + 4;
    uint32_t mc = hp_frac < 0.25f ? mouth_hurt : mouth_happy;
    if (hp_frac > 0.5f) {
        /* smile: V shape */
        for (int dx = -3; dx <= 3; dx++) {
            int yy = my + (dx < 0 ? -dx : dx) / 2;
            fb->pixels[(size_t)(y0 + yy) * SCREEN_W + (x0 + sz / 2 + dx)] = mc;
        }
    } else if (hp_frac > 0.25f) {
        for (int dx = -3; dx <= 3; dx++)
            fb->pixels[(size_t)(y0 + my) * SCREEN_W + (x0 + sz / 2 + dx)] = mc;
    } else {
        /* frown: inverted V */
        for (int dx = -3; dx <= 3; dx++) {
            int yy = my - (dx < 0 ? -dx : dx) / 2;
            fb->pixels[(size_t)(y0 + yy) * SCREEN_W + (x0 + sz / 2 + dx)] = mc;
        }
    }
}

void hud_draw(Framebuffer *fb, const Player *pl, const WeaponSystem *ws) {
    uint32_t bg = make_color(20, 20, 20);
    uint32_t panel = make_color(35, 35, 40);

    /* Bottom HUD panel bar */
    int panel_h = 26;
    for (int y = SCREEN_H - panel_h; y < SCREEN_H; y++) {
        uint32_t *row = fb->pixels + (size_t)y * SCREEN_W;
        for (int x = 0; x < SCREEN_W; x++) row[x] = panel;
    }

    /* HP (red) bottom-left with numeric */
    const Weapon *w = &ws->weapons[ws->current];
    int by = SCREEN_H - panel_h + 4;

    /* HP bar */
    int bar_w = 120, bar_h = 8;
    int bx = 10;
    for (int y = by; y < by + bar_h; y++)
        for (int x = bx; x < bx + bar_w; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = bg;
    int hpw = (int)((float)bar_w * clampf(pl->hp / 100.0f, 0.0f, 1.0f));
    uint32_t hp_col = make_color(200, 40, 40);
    for (int y = by; y < by + bar_h; y++)
        for (int x = bx; x < bx + hpw; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = hp_col;
    draw_int(fb, bx + bar_w + 6, by - 1, (int)pl->hp, make_color(255, 220, 220), 1);

    /* Armor bar (yellow) */
    int ar_by = by + bar_h + 2;
    for (int y = ar_by; y < ar_by + bar_h; y++)
        for (int x = bx; x < bx + bar_w; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = bg;
    int arw = (int)((float)bar_w * clampf(pl->armor / 100.0f, 0.0f, 1.0f));
    uint32_t ar_col = make_color(220, 200, 60);
    for (int y = ar_by; y < ar_by + bar_h; y++)
        for (int x = bx; x < bx + arw; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = ar_col;
    draw_int(fb, bx + bar_w + 6, ar_by - 1, (int)pl->armor, make_color(255, 255, 200), 1);

    /* Face indicator (center bottom) */
    hud_draw_face(fb, SCREEN_W / 2 - 11, SCREEN_H - panel_h + 3, 22,
                  clampf(pl->hp / 100.0f, 0.0f, 1.0f));

    /* Ammo (right) with numeric */
    uint32_t am_col = ws->current == WEAPON_PISTOL ? make_color(220, 220, 60)
                                                   : make_color(220, 140, 40);
    int abx = SCREEN_W - bar_w - 50;
    int aby = by;
    for (int y = aby; y < aby + bar_h; y++)
        for (int x = abx; x < abx + bar_w; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = bg;
    int amw = (int)((float)bar_w * clampf((float)w->ammo / (float)w->max_ammo, 0.0f, 1.0f));
    for (int y = aby; y < aby + bar_h; y++)
        for (int x = abx; x < abx + amw; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = am_col;
    draw_int(fb, abx - 30, aby - 1, w->ammo, am_col, 1);

    /* Weapon name-ish indicator: PISTOL=1, SHOTGUN=2 (two blocks lit) */
    int wsx = SCREEN_W - 24;
    int wsy = ar_by;
    for (int i = 0; i < WEAPON_COUNT; i++) {
        uint32_t c = (i == ws->current) ? make_color(240, 240, 80) : make_color(80, 80, 60);
        for (int dy = 0; dy < 8; dy++)
            for (int dx = 0; dx < 8; dx++)
                fb->pixels[(size_t)(wsy + dy) * SCREEN_W + (wsx - i * 12 + dx)] = c;
    }
}