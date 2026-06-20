#include "assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#include "../vendor/stb_image.h"

void texture_free(Texture *t) {
    if (t->pixels) {
        free(t->pixels);
        t->pixels = NULL;
    }
    t->w = t->h = 0;
}

/* Allocate a square TEX_SIZE texture with ARGB pixels. */
static int tex_alloc(Texture *t) {
    t->w = TEX_SIZE;
    t->h = TEX_SIZE;
    t->pixels = (uint32_t *)malloc((size_t)TEX_SIZE * TEX_SIZE * sizeof(uint32_t));
    if (!t->pixels) return -1;
    return 0;
}

static inline uint32_t col(uint8_t r, uint8_t g, uint8_t b) {
    return make_color(r, g, b);
}

/* ---- Procedural generators ---- */

static void gen_brick(Texture *t) {
    tex_alloc(t);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int row = y / 16;
            int off = (row % 2) * 8;
            int bx = (x + off) % 32;
            int by = y % 16;
            uint8_t r, g, b;
            if (bx < 2 || by < 2) {
                r = 50; g = 50; b = 55; /* mortar */
            } else {
                int n = ((bx * 7 + by * 13) % 5) - 2;
                r = (uint8_t)(150 + n * 6);
                g = (uint8_t)(70 + n * 4);
                b = (uint8_t)(55 + n * 3);
            }
            t->pixels[y * TEX_SIZE + x] = col(r, g, b);
        }
    }
}

static void gen_door(Texture *t) {
    tex_alloc(t);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            uint8_t r, g, b;
            if (x < 4 || x > TEX_SIZE - 5 || y < 4 || y > TEX_SIZE - 5) {
                r = 40; g = 30; b = 20; /* frame */
            } else {
                int v = ((x / 8) % 2) * 20;
                r = (uint8_t)(120 + v);
                g = (uint8_t)(80 + v / 2);
                b = 40;
            }
            t->pixels[y * TEX_SIZE + x] = col(r, g, b);
        }
    }
}

static void gen_floor(Texture *t) {
    tex_alloc(t);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int gx = x / 16, gy = y / 16;
            uint8_t base = (gx + gy) % 2 ? 90 : 70;
            int n = ((x * 3 + y * 5) % 7) - 3;
            t->pixels[y * TEX_SIZE + x] = col((uint8_t)(base + n), (uint8_t)(base - 5 + n), (uint8_t)(base - 15 + n));
        }
    }
}

static void gen_ceiling(Texture *t) {
    tex_alloc(t);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int n = ((x * 11 + y * 7) % 9) - 4;
            t->pixels[y * TEX_SIZE + x] = col((uint8_t)(45 + n), (uint8_t)(45 + n), (uint8_t)(55 + n));
        }
    }
}

static void gen_barrel(Texture *t) {
    tex_alloc(t);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            /* transparent border; draw barrel in central band */
            int dx = x - TEX_SIZE / 2;
            uint32_t c;
            if (y < 4 || y > TEX_SIZE - 5 || abs(dx) > 26) {
                c = 0; /* transparent */
            } else {
                int band = (y / 8) % 2;
                if (band) c = col(160, 90, 20);
                else      c = col(200, 120, 30);
                if (abs(dx) > 24) c = col(80, 50, 10); /* edge */
            }
            t->pixels[y * TEX_SIZE + x] = c;
        }
    }
}

static void gen_enemy(Texture *t) {
    tex_alloc(t);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int dx = x - TEX_SIZE / 2;
            int dy = y - TEX_SIZE / 2;
            uint32_t c;
            /* head */
            if (dx * dx + (dy + 14) * (dy + 14) <= 64) {
                c = col(120, 200, 120);
            }
            /* body */
            else if (y > 20 && y < 52 && abs(dx) < 14) {
                c = col(60, 160, 60);
            }
            /* legs */
            else if (y >= 52 && y < 60 && abs(dx) < (y < 56 ? 10 : 8)) {
                c = col(40, 100, 40);
            }
            else c = 0;
            t->pixels[y * TEX_SIZE + x] = c;
        }
    }
}

static void gen_enemy_serg(Texture *t) {
    tex_alloc(t);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int dx = x - TEX_SIZE / 2;
            int dy = y - TEX_SIZE / 2;
            uint32_t c;
            /* head (human) */
            if (dx * dx + (dy + 14) * (dy + 14) <= 60) {
                c = col(200, 170, 140);
            }
            /* body armor brown */
            else if (y > 20 && y < 52 && abs(dx) < 15) {
                c = col(110, 80, 50);
            }
            /* legs */
            else if (y >= 52 && y < 60 && abs(dx) < (y < 56 ? 11 : 9)) {
                c = col(70, 50, 30);
            }
            else c = 0;
            t->pixels[y * TEX_SIZE + x] = c;
        }
    }
}

static void gen_enemy_dead(Texture *t) {
    tex_alloc(t);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int dx = x - TEX_SIZE / 2;
            int dy = y - (TEX_SIZE - 12);
            uint32_t c;
            /* flat corpse blob near bottom */
            if (dx * dx * 1 + dy * dy * 4 <= 360 && y > 36) {
                int band = (x / 8) % 2;
                c = band ? col(70, 30, 30) : col(110, 50, 50);
            } else {
                c = 0;
            }
            t->pixels[y * TEX_SIZE + x] = c;
        }
    }
}

static void gen_medkit(Texture *t) {
    tex_alloc(t);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            uint32_t c;
            if (y < 8 || y > TEX_SIZE - 9 || x < 12 || x > TEX_SIZE - 13) {
                c = 0;
            } else {
                int dx = x - TEX_SIZE / 2;
                int dy = y - TEX_SIZE / 2;
                if (abs(dx) < 4 && dy < 8 && dy > -16) c = col(220, 40, 40);
                else if (abs(dy) < 3 && dx < 12 && dx > -12) c = col(220, 40, 40);
                else c = col(240, 240, 240);
            }
            t->pixels[y * TEX_SIZE + x] = c;
        }
    }
}

static void gen_ammo(Texture *t) {
    tex_alloc(t);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            uint32_t c;
            if (y < 16 || y > TEX_SIZE - 17 || x < 16 || x > TEX_SIZE - 17) {
                c = 0;
            } else {
                int band = (y / 6) % 2;
                c = band ? col(220, 200, 60) : col(180, 160, 40);
            }
            t->pixels[y * TEX_SIZE + x] = c;
        }
    }
}

static void gen_armor(Texture *t) {
    tex_alloc(t);
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int dx = x - TEX_SIZE / 2;
            int dy = y - TEX_SIZE / 2;
            uint32_t c;
            if (y > 12 && y < 52 && abs(dx) < (20 - abs(dy) / 2)) {
                c = col(60, 120, 200);
            } else {
                c = 0;
            }
            t->pixels[y * TEX_SIZE + x] = c;
        }
    }
}

static void gen_weapon(Texture *t) {
    /* wide aspect pistol at bottom: w=128 h=128 */
    t->w = 128;
    t->h = 128;
    t->pixels = (uint32_t *)malloc(128 * 128 * sizeof(uint32_t));
    if (!t->pixels) return;
    memset(t->pixels, 0, 128 * 128 * sizeof(uint32_t));
    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            int dx = x - 64;
            uint32_t c = 0;
            /* slide/barrel */
            if (y < 56 && y > 40 && abs(dx) < 30) {
                c = col(90, 90, 95);
            }
            /* grip */
            else if (y >= 56 && y < 100 && abs(dx) < (40 - (y - 56) / 3)) {
                c = col(50, 45, 40);
            }
            /* trigger guard */
            else if (y >= 70 && y < 86 && abs(dx) < 14) {
                c = col(70, 65, 60);
            }
            /* hands */
            else if (y >= 90 && abs(dx) < 50) {
                c = col(180, 140, 100);
            }
            t->pixels[y * 128 + x] = c;
        }
    }
}

static void gen_weapon_shotgun(Texture *t) {
    t->w = 128;
    t->h = 128;
    t->pixels = (uint32_t *)malloc(128 * 128 * sizeof(uint32_t));
    if (!t->pixels) return;
    memset(t->pixels, 0, 128 * 128 * sizeof(uint32_t));
    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            int dx = x - 64;
            uint32_t c = 0;
            /* double barrel */
            if (y > 36 && y < 56 && abs(dx) < 40) {
                int bore = (abs(dx) < 18) ? col(20, 20, 20) : col(70, 60, 45);
                c = bore;
            }
            /* stock / grip */
            else if (y >= 56 && y < 100 && abs(dx) < (44 - (y - 56) / 3)) {
                c = col(90, 60, 30);
            }
            /* hands */
            else if (y >= 90 && abs(dx) < 56) {
                c = col(180, 140, 100);
            }
            t->pixels[y * 128 + x] = c;
        }
    }
}

int assets_load_png(Texture *tex, const char *path) {
    int w, h, ch;
    unsigned char *data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) {
        fprintf(stderr, "assets: failed to load %s: %s\n", path, stbi_failure_reason());
        return -1;
    }
    uint32_t *px = (uint32_t *)malloc((size_t)w * h * sizeof(uint32_t));
    if (!px) {
        stbi_image_free(data);
        return -1;
    }
    for (int i = 0; i < w * h; i++) {
        unsigned char *p = data + i * 4;
        /* source: RGBA bytes; store as ARGB (make_color layout: 0xFF.. | b<<16 | g<<8 | r) */
        px[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[0];
    }
    stbi_image_free(data);
    tex->w = w;
    tex->h = h;
    tex->pixels = px;
    return 0;
}

int assets_init(Assets *a) {
    memset(a, 0, sizeof(*a));
    gen_brick(&a->wall_brick);
    gen_door(&a->wall_door);
    gen_floor(&a->floor_tex);
    gen_ceiling(&a->ceiling_tex);
    gen_barrel(&a->sprite_barrel);
    gen_enemy(&a->sprite_enemy);
    gen_enemy_serg(&a->sprite_enemy_serg);
    gen_enemy_dead(&a->sprite_enemy_dead);
    gen_medkit(&a->sprite_medkit);
    gen_ammo(&a->sprite_ammo);
    gen_armor(&a->sprite_armor);
    gen_weapon(&a->weapon_pistol);
    gen_weapon_shotgun(&a->weapon_shotgun);

    /* Verify all sprite/wall textures allocated */
    Texture *all[] = { &a->wall_brick, &a->wall_door, &a->floor_tex, &a->ceiling_tex,
                      &a->sprite_barrel, &a->sprite_enemy, &a->sprite_enemy_serg,
                      &a->sprite_enemy_dead, &a->sprite_medkit, &a->sprite_ammo,
                      &a->sprite_armor, &a->weapon_pistol, &a->weapon_shotgun };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        if (!all[i]->pixels) {
            fprintf(stderr, "assets: procedural texture %zu failed\n", i);
            return -1;
        }
    }
    return 0;
}

void assets_shutdown(Assets *a) {
    texture_free(&a->wall_brick);
    texture_free(&a->wall_door);
    texture_free(&a->floor_tex);
    texture_free(&a->ceiling_tex);
    texture_free(&a->sprite_barrel);
    texture_free(&a->sprite_enemy);
    texture_free(&a->sprite_enemy_serg);
    texture_free(&a->sprite_enemy_dead);
    texture_free(&a->sprite_medkit);
    texture_free(&a->sprite_ammo);
    texture_free(&a->sprite_armor);
    texture_free(&a->weapon_pistol);
    texture_free(&a->weapon_shotgun);
}