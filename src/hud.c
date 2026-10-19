#include "hud.h"
#include "text.h"
#include "utils.h"
#include <math.h>
#include <string.h>

/* Frame `col` of t with its top-left corner at (x0, y0). A pixel at alpha 0
 * is skipped, as everywhere, and so is one off the screen. A `tone` of 0
 * draws each pixel in its own colour; any other makes the frame a mask, every
 * pixel it keeps drawn in `tone` — make_color() never gives 0, its alpha is
 * always 0xFF. */
static void draw_frame(Framebuffer *fb, const Texture *t, int col, int x0, int y0, uint32_t tone)
{
    const uint32_t *src = texture_frame(t, col, 0);
    for (int y = 0; y < t->fh; y++) {
        int py = y0 + y;
        if (py < 0 || py >= SCREEN_H) {
            continue;
        }
        for (int x = 0; x < t->fw; x++) {
            int px = x0 + x;
            if (px < 0 || px >= SCREEN_W) {
                continue;
            }
            uint32_t c = src[(size_t)y * t->w + x];
            if ((c & 0xFF000000u) == 0) {
                continue;
            }
            fb->pixels[(size_t)py * SCREEN_W + px] = tone ? tone : c;
        }
    }
}

void hud_face_init(HudFace *f)
{
    memset(f, 0, sizeof(*f));
}

void hud_face_update(HudFace *f, const PlayerState *pl, float dt)
{
    int hurt = f->seen && pl->hp < f->hp;
    f->hp = pl->hp;
    f->seen = 1;
    if (!pl->alive) {
        f->pain_left = 0.0f;
    } else if (hurt) {
        f->pain_left = HUD_PAIN_TIME;
    } else {
        f->pain_left = fmaxf(f->pain_left - dt, 0.0f);
    }
}

int hud_face_frame(const HudFace *f, const PlayerState *pl)
{
    if (!pl->alive || pl->hp <= 0.0f) {
        return FACE_DEAD;
    }
    if (f->pain_left > 0.0f) {
        return FACE_PAIN;
    }
    /* Each step takes the 20 hp below it, so 80 hp still shows the first:
     * the face starts to look hurt after a fifth of its health is gone. */
    float per_step = 100.0f / (float)FACE_HEALTH_STEPS;
    int step = FACE_HEALTH_STEPS - 1 - (int)(clampf(pl->hp, 0.0f, 100.0f) / per_step);
    return FACE_HEALTH + (step < 0 ? 0 : step);
}

int hud_weapon_frame(int cols, float anim)
{
    if (anim <= 0.0f || cols <= 1) {
        return 0;
    }
    int f = 1 + (int)((1.0f - anim) * (float)(cols - 1));
    return f < cols - 1 ? f : cols - 1;
}

void hud_draw_weapon(Framebuffer *fb, const Texture *base, float anim)
{
    if (!base || !base->pixels) {
        return;
    }
    const uint32_t *src = texture_frame(base, hud_weapon_frame(base->cols, anim), 0);
    int tw = base->fw;
    int th = base->fh;
    int target_h = (int)(SCREEN_H * 0.40f);
    float scale = (float)target_h / (float)th;
    int draw_w = (int)(tw * scale);
    int draw_h = target_h;
    int x0 = SCREEN_W / 2 - draw_w / 2;
    int y0 = SCREEN_H - HUD_PANEL_H - draw_h;
    int recoil = (int)(18.0f * anim);
    y0 += recoil;
    if (x0 < 0) {
        x0 = 0;
    }

    for (int y = 0; y < draw_h; y++) {
        int sy = (int)((float)y / scale);
        if (sy < 0 || sy >= th) {
            continue;
        }
        int py = y0 + y;
        if (py < 0 || py >= SCREEN_H) {
            continue;
        }
        for (int x = 0; x < draw_w; x++) {
            int sx = (int)((float)x / scale);
            if (sx < 0 || sx >= tw) {
                continue;
            }
            int px = x0 + x;
            if (px < 0 || px >= SCREEN_W) {
                continue;
            }
            uint32_t c = src[(size_t)sy * base->w + sx];
            if ((c & 0xFF000000u) == 0) {
                continue;
            }
            fb->pixels[(size_t)py * SCREEN_W + px] = c;
        }
    }
}

/* Procedural "face" indicator: health-based expression. */
static void hud_draw_face(Framebuffer *fb, int x0, int y0, int sz, float hp_frac)
{
    uint32_t skin = make_color(200, 170, 140);
    uint32_t dark = make_color(40, 30, 25);
    uint32_t eye = make_color(20, 20, 20);
    uint32_t mouth_happy = make_color(120, 40, 40);
    uint32_t mouth_hurt = make_color(180, 30, 30);
    /* background panel */
    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            fb->pixels[(size_t)(y0 + y) * SCREEN_W + (x0 + x)] = dark;
        }
    }
    /* face area */
    int inset = sz / 8;
    for (int y = inset; y < sz - inset; y++) {
        for (int x = inset; x < sz - inset; x++) {
            fb->pixels[(size_t)(y0 + y) * SCREEN_W + (x0 + x)] = skin;
        }
    }
    /* eyes */
    int ey = sz / 2 - 2;
    int ex1 = sz / 2 - 3;
    int ex2 = sz / 2 + 2;
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            fb->pixels[(size_t)(y0 + ey + dy) * SCREEN_W + (x0 + ex1 + dx)] = eye;
            fb->pixels[(size_t)(y0 + ey + dy) * SCREEN_W + (x0 + ex2 + dx)] = eye;
        }
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
        for (int dx = -3; dx <= 3; dx++) {
            fb->pixels[(size_t)(y0 + my) * SCREEN_W + (x0 + sz / 2 + dx)] = mc;
        }
    } else {
        /* frown: inverted V */
        for (int dx = -3; dx <= 3; dx++) {
            int yy = my - (dx < 0 ? -dx : dx) / 2;
            fb->pixels[(size_t)(y0 + yy) * SCREEN_W + (x0 + sz / 2 + dx)] = mc;
        }
    }
}

/* ---- The panel ---- */

/* Five boxes side by side: health, armor, the face, the weapon, ammo. The
 * face's box is as wide as the face and its frame, and the other four share
 * what is left. */
#define FACE_BOX_W 52
#define STAT_BOX_W ((SCREEN_W - FACE_BOX_W) / 4)

/* Every box but the face's lays out on two lines, from the top of the panel:
 * the icon and the number on the first, a gauge the width of the box on the
 * second. */
#define BOX_PAD 14 /* left edge of a box's contents */
#define ICON_Y 10
#define NUMBER_Y 9
#define GAUGE_Y 33
#define GAUGE_H 7
#define GAUGE_W (STAT_BOX_W - 2 * BOX_PAD - 1)

#define PANEL_COLOR make_color(38, 38, 44)
#define WELL_COLOR make_color(18, 18, 22) /* an empty gauge */

/* Icons of the stat boxes, 16x16, a row a word, bit 15 the leftmost column. */
static const uint16_t ICON_HEALTH[16] = {
        0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0xFFFF, 0xFFFF, 0xFFFF,
        0xFFFF, 0xFFFF, 0xFFFF, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0,
};
static const uint16_t ICON_ARMOR[16] = {
        0x7FFE, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x7FFE,
        0x7FFE, 0x3FFC, 0x3FFC, 0x1FF8, 0x0FF0, 0x07E0, 0x03C0, 0x0180,
};
/* A cartridge: the case, and the bullet drawn over its top in its own colour. */
static const uint16_t ICON_AMMO[16] = {
        0x03C0, 0x07E0, 0x07E0, 0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0,
        0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0, 0x1FF8, 0x1FF8, 0x0000,
};
static const uint16_t ICON_BULLET[16] = {
        0x03C0, 0x07E0, 0x07E0, 0x0FF0, 0x0FF0,
};

static void fill_rect(Framebuffer *fb, int x0, int y0, int w, int h, uint32_t color)
{
    for (int y = y0 < 0 ? 0 : y0; y < y0 + h && y < SCREEN_H; y++) {
        for (int x = x0 < 0 ? 0 : x0; x < x0 + w && x < SCREEN_W; x++) {
            fb->pixels[(size_t)y * SCREEN_W + x] = color;
        }
    }
}

static void draw_icon(Framebuffer *fb, const uint16_t rows[16], int x0, int y0, uint32_t color)
{
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            if (rows[r] & (0x8000u >> c)) {
                fill_rect(fb, x0 + c, y0 + r, 1, 1, color);
            }
        }
    }
}

/* Halfway to white. The same in every channel, so the byte order of
 * make_color() does not come into it. */
static uint32_t lighten(uint32_t c)
{
    return 0xFF000000u | (((c & 0x00FEFEFEu) >> 1) + 0x007F7F7Fu);
}

/* A segment of a gauge, lit along the top. */
static void draw_segment(Framebuffer *fb, int x0, int w, uint32_t color)
{
    int y0 = SCREEN_H - HUD_PANEL_H + GAUGE_Y;
    fill_rect(fb, x0, y0, w, GAUGE_H, color);
    fill_rect(fb, x0, y0, w, 1, lighten(color));
}

/* The number three times the font's size, with a shadow. */
static void draw_number(Framebuffer *fb, int x0, int value, uint32_t color)
{
    int y0 = SCREEN_H - HUD_PANEL_H + NUMBER_Y;
    text_draw_int(fb, x0 + 1, y0 + 1, value, make_color(0, 0, 0), 3);
    text_draw_int(fb, x0, y0, value, color, 3);
}

/* Health, armor or ammo: the icon and the number, and under them a gauge
 * `frac` full. */
static void draw_stat_box(Framebuffer *fb, int x0, const uint16_t icon[16], uint32_t icon_color,
                          int value, uint32_t number_color, float frac, uint32_t gauge_color)
{
    int top = SCREEN_H - HUD_PANEL_H;
    draw_icon(fb, icon, x0 + BOX_PAD + 1, top + ICON_Y + 1, make_color(0, 0, 0));
    draw_icon(fb, icon, x0 + BOX_PAD, top + ICON_Y, icon_color);
    draw_number(fb, x0 + BOX_PAD + 28, value, number_color);
    fill_rect(fb, x0 + BOX_PAD, top + GAUGE_Y, GAUGE_W, GAUGE_H, WELL_COLOR);
    draw_segment(fb, x0 + BOX_PAD, (int)((float)GAUGE_W * clampf(frac, 0.0f, 1.0f)), gauge_color);
}

/* Tone of weapon k in the panel: the one in hand, one picked up, and the
 * empty gauge for one not. */
static uint32_t weapon_tone(const WeaponSystem *ws, int k)
{
    if (k == ws->current) {
        return make_color(240, 240, 80);
    }
    if (ws->weapons[k].owned) {
        return make_color(170, 170, 130);
    }
    return WELL_COLOR;
}

/* The weapon's box, laid out as the others are: the icon of the weapon in
 * hand and its key, and under them a gauge of a segment a weapon, keys 1-5
 * left to right, each lit in its weapon's tone. Without the icon's file the
 * key stands alone. The file holds a black silhouette, which the panel would
 * swallow, so only its alpha is taken and the tone is the code's. */
static void draw_weapon_box(Framebuffer *fb, int x0, const WeaponSystem *ws, const Assets *a)
{
    int top = SCREEN_H - HUD_PANEL_H;
    uint32_t in_hand = weapon_tone(ws, ws->current);
    const Texture *icon = &a->hud_icon[ws->current];
    int number_x = x0 + BOX_PAD + 28; /* where the other boxes put theirs */
    if (icon->pixels) {
        draw_frame(fb, icon, 0, x0 + BOX_PAD + 1, top + ICON_Y + 1, make_color(0, 0, 0));
        draw_frame(fb, icon, 0, x0 + BOX_PAD, top + ICON_Y, in_hand);
        number_x = x0 + BOX_PAD + icon->fw + 11;
    }
    draw_number(fb, number_x, ws->current + 1, in_hand);

    int gap = 3;
    int seg_w = (GAUGE_W - (WEAPON_COUNT - 1) * gap) / WEAPON_COUNT;
    for (int k = 0; k < WEAPON_COUNT; k++) {
        int x = x0 + BOX_PAD + k * (seg_w + gap);
        uint32_t tone = weapon_tone(ws, k);
        int lit = tone != WELL_COLOR;
        if (lit) {
            draw_segment(fb, x, seg_w, tone);
        } else {
            fill_rect(fb, x, top + GAUGE_Y, seg_w, GAUGE_H, tone);
        }
        text_draw_char(fb, x + (seg_w - TEXT_GLYPH_W) / 2, top + GAUGE_Y + 1, (char)('1' + k),
                       lit ? PANEL_COLOR : make_color(90, 90, 80), 1);
    }
}

/* A sunken line between two boxes. */
static void draw_divider(Framebuffer *fb, int x)
{
    int top = SCREEN_H - HUD_PANEL_H;
    fill_rect(fb, x - 1, top + 4, 1, HUD_PANEL_H - 8, make_color(20, 20, 24));
    fill_rect(fb, x, top + 4, 1, HUD_PANEL_H - 8, make_color(62, 62, 70));
}

void hud_draw(Framebuffer *fb, const PlayerState *pl, const HudFace *face, const Assets *a)
{
    const WeaponSystem *ws = &pl->weapons;
    const Weapon *w = &ws->weapons[ws->current];
    int top = SCREEN_H - HUD_PANEL_H;
    int face_x = 2 * STAT_BOX_W;
    int weapon_x = face_x + FACE_BOX_W;
    int ammo_x = weapon_x + STAT_BOX_W;

    /* A plate lit from above. */
    fill_rect(fb, 0, top, SCREEN_W, HUD_PANEL_H, PANEL_COLOR);
    fill_rect(fb, 0, top, SCREEN_W, 1, make_color(90, 90, 100));
    fill_rect(fb, 0, top + 1, SCREEN_W, 1, make_color(60, 60, 68));
    fill_rect(fb, 0, SCREEN_H - 1, SCREEN_W, 1, make_color(20, 20, 24));
    draw_divider(fb, STAT_BOX_W);
    draw_divider(fb, face_x);
    draw_divider(fb, weapon_x);
    draw_divider(fb, ammo_x);

    draw_stat_box(fb, 0, ICON_HEALTH, make_color(220, 50, 50), (int)pl->hp, make_color(240, 90, 80),
                  pl->hp / 100.0f, make_color(200, 40, 40));
    draw_stat_box(fb, STAT_BOX_W, ICON_ARMOR, make_color(80, 140, 230), (int)pl->armor,
                  make_color(130, 180, 255), pl->armor / 100.0f, make_color(70, 120, 220));

    /* The face in a dark well; the sheet's at its own size, the code's
     * smaller one in the middle. */
    fill_rect(fb, face_x, top + 2, FACE_BOX_W, HUD_PANEL_H - 3, make_color(20, 20, 24));
    if (a->hud_face.pixels) {
        const Texture *t = &a->hud_face;
        draw_frame(fb, t, hud_face_frame(face, pl), face_x + (FACE_BOX_W - t->fw) / 2,
                   SCREEN_H - 1 - t->fh, 0);
    } else {
        hud_draw_face(fb, face_x + (FACE_BOX_W - 22) / 2, top + (HUD_PANEL_H - 22) / 2, 22,
                      clampf(pl->hp / 100.0f, 0.0f, 1.0f));
    }

    draw_weapon_box(fb, weapon_x, ws, a);

    draw_stat_box(fb, ammo_x, ICON_AMMO, make_color(220, 180, 60), w->ammo,
                  make_color(240, 200, 80), (float)w->ammo / (float)w->max_ammo,
                  make_color(220, 140, 40));
    draw_icon(fb, ICON_BULLET, ammo_x + BOX_PAD, top + ICON_Y, make_color(200, 110, 60));
}
