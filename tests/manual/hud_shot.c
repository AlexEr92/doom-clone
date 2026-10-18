/* The status bar as hud_draw() draws it, in the states a game reaches only
 * with effort, run by hand from the project root:
 *
 *   hud_shot [out.png] [--no-files]
 *
 * Draws the bottom rows of the screen — the panel and the lower part of the
 * weapon in hand standing on it — once for each state in STATES, one
 * band under another, at the 2x the game window shows, into out.png
 * (hud_shot.png by default), and prints which band is which. The files are
 * those assets_init() finds under assets/; --no-files empties the status
 * bar's slots, which is what a checkout without the graphics shows. Drawing
 * is not unit-tested (tests/README.md): the picture is for eyes. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "hud.h"
#include "player.h"
#include "weapon.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../vendor/stb_image_write.h"

/* Rows of the screen each band shows: the panel and the weapon above it. */
#define BAND_H (HUD_PANEL_H + 48)
#define SCALE 2

typedef struct {
    const char *name;
    float hp;
    float armor;
    int hit;     /* hp has just dropped to `hp`: the grimace */
    int current; /* WeaponType in hand */
    int owned;   /* bit k: weapon k picked up; the pistol always is */
} HudState;

static const HudState STATES[] = {
        {"full health, pistol only", 100.0f, 0.0f, 0, WEAPON_PISTOL, 0},
        {"70 hp, full armor, shotgun in hand", 70.0f, 100.0f, 0, WEAPON_SHOTGUN,
         1 << WEAPON_SHOTGUN},
        {"45 hp, ak47, shotgun owned", 45.0f, 60.0f, 0, WEAPON_AK47,
         1 << WEAPON_SHOTGUN | 1 << WEAPON_AK47},
        {"30 hp, rifle", 30.0f, 25.0f, 0, WEAPON_RIFLE, 1 << WEAPON_RIFLE},
        {"10 hp, everything, grenade launcher", 10.0f, 5.0f, 0, WEAPON_GRENADE, 0x1F},
        {"hit down to 60 hp", 60.0f, 40.0f, 1, WEAPON_PISTOL, 0},
        {"dead", 0.0f, 0.0f, 0, WEAPON_PISTOL, 0},
};

#define STATE_COUNT (sizeof(STATES) / sizeof(STATES[0]))

static uint32_t screen[SCREEN_W * SCREEN_H];

/* One state onto `screen`, the way a frame of main.c draws it. */
static void draw_state(const HudState *s, const Assets *a)
{
    PlayerState p;
    player_init(&p, 2, 2);
    for (int k = 0; k < WEAPON_COUNT; k++) {
        if (s->owned & (1 << k)) {
            weapon_give(&p, k);
        }
    }
    weapon_switch(&p, s->current);

    HudFace face;
    hud_face_init(&face);
    hud_face_update(&face, &p, 0.0f);
    p.hp = s->hp;
    p.armor = s->armor;
    p.alive = s->hp > 0.0f;
    if (!s->hit) {
        hud_face_init(&face);
    }
    hud_face_update(&face, &p, 0.0f);

    /* A flat stand-in for the view behind the weapon. */
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        screen[i] = make_color(70, 60, 55);
    }
    Framebuffer fb = {screen};
    hud_draw_weapon(&fb, &a->weapon_fp[p.weapons.current], 0.0f);
    hud_draw(&fb, &p, &face, a);
}

int main(int argc, char **argv)
{
    const char *out = "hud_shot.png";
    int no_files = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-files") == 0) {
            no_files = 1;
        } else {
            out = argv[i];
        }
    }

    static Assets a;
    if (assets_init(&a) != 0) {
        fprintf(stderr, "hud_shot: assets_init() failed\n");
        return 1;
    }
    if (no_files) {
        texture_free(&a.hud_face);
        for (int k = 0; k < WEAPON_COUNT; k++) {
            texture_free(&a.hud_icon[k]);
        }
    }
    printf("face: %s, icons:", a.hud_face.pixels ? "file" : "code");
    for (int k = 0; k < WEAPON_COUNT; k++) {
        printf(" %s", a.hud_icon[k].pixels ? "file" : "-");
    }
    printf("\n");

    int w = SCREEN_W * SCALE;
    int h = (int)STATE_COUNT * BAND_H * SCALE;
    unsigned char *rgb = (unsigned char *)malloc((size_t)w * h * 3);
    if (!rgb) {
        assets_shutdown(&a);
        return 1;
    }
    for (size_t s = 0; s < STATE_COUNT; s++) {
        draw_state(&STATES[s], &a);
        printf("band %zu: %s\n", s, STATES[s].name);
        for (int y = 0; y < BAND_H * SCALE; y++) {
            const uint32_t *row = screen + (size_t)(SCREEN_H - BAND_H + y / SCALE) * SCREEN_W;
            unsigned char *dst = rgb + ((s * BAND_H * SCALE + (size_t)y) * (size_t)w) * 3;
            for (int x = 0; x < w; x++) {
                uint32_t c = row[x / SCALE]; /* make_color(): red in the low byte */
                dst[x * 3 + 0] = (unsigned char)(c & 0xFF);
                dst[x * 3 + 1] = (unsigned char)((c >> 8) & 0xFF);
                dst[x * 3 + 2] = (unsigned char)((c >> 16) & 0xFF);
            }
        }
    }
    int ok = stbi_write_png(out, w, h, 3, rgb, w * 3);
    free(rgb);
    assets_shutdown(&a);
    if (!ok) {
        fprintf(stderr, "hud_shot: cannot write %s\n", out);
        return 1;
    }
    printf("%s\n", out);
    return 0;
}
