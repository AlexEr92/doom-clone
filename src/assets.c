#include "assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#include "../vendor/stb_image.h"

void texture_free(Texture *t)
{
    if (t->pixels) {
        free(t->pixels);
        t->pixels = NULL;
    }
    t->w = t->h = 0;
    t->fw = t->fh = 0;
    t->cols = t->rows = 0;
}

int texture_alloc(Texture *t, int w, int h)
{
    t->pixels = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!t->pixels) {
        t->w = t->h = t->fw = t->fh = t->cols = t->rows = 0;
        return -1;
    }
    t->w = t->fw = w;
    t->h = t->fh = h;
    t->cols = t->rows = 1;
    return 0;
}

static inline uint32_t col(uint8_t r, uint8_t g, uint8_t b)
{
    return make_color(r, g, b);
}

/* ---- Procedural generators ---- */

static void gen_brick(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int row = y / 16;
            int off = (row % 2) * 8;
            int bx = (x + off) % 32;
            int by = y % 16;
            uint8_t r, g, b;
            if (bx < 2 || by < 2) {
                r = 50;
                g = 50;
                b = 55; /* mortar */
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

static void gen_door(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            uint8_t r, g, b;
            if (x < 4 || x > TEX_SIZE - 5 || y < 4 || y > TEX_SIZE - 5) {
                r = 40;
                g = 30;
                b = 20; /* frame */
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

static void gen_floor(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int gx = x / 16, gy = y / 16;
            uint8_t base = (gx + gy) % 2 ? 90 : 70;
            int n = ((x * 3 + y * 5) % 7) - 3;
            t->pixels[y * TEX_SIZE + x] =
                    col((uint8_t)(base + n), (uint8_t)(base - 5 + n), (uint8_t)(base - 15 + n));
        }
    }
}

static void gen_ceiling(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int n = ((x * 11 + y * 7) % 9) - 4;
            t->pixels[y * TEX_SIZE + x] =
                    col((uint8_t)(45 + n), (uint8_t)(45 + n), (uint8_t)(55 + n));
        }
    }
}

static void gen_barrel(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            /* transparent border; draw barrel in central band */
            int dx = x - TEX_SIZE / 2;
            uint32_t c;
            if (y < 4 || y > TEX_SIZE - 5 || abs(dx) > 26) {
                c = 0; /* transparent */
            } else {
                int band = (y / 8) % 2;
                if (band) {
                    c = col(160, 90, 20);
                } else {
                    c = col(200, 120, 30);
                }
                if (abs(dx) > 24) {
                    c = col(80, 50, 10); /* edge */
                }
            }
            t->pixels[y * TEX_SIZE + x] = c;
        }
    }
}

static void gen_enemy(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
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
            } else {
                c = 0;
            }
            t->pixels[y * TEX_SIZE + x] = c;
        }
    }
}

static void gen_enemy_serg(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
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
            } else {
                c = 0;
            }
            t->pixels[y * TEX_SIZE + x] = c;
        }
    }
}

static void gen_enemy_dead(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
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

static void gen_medkit(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            uint32_t c;
            if (y < 8 || y > TEX_SIZE - 9 || x < 12 || x > TEX_SIZE - 13) {
                c = 0;
            } else {
                int dx = x - TEX_SIZE / 2;
                int dy = y - TEX_SIZE / 2;
                if (abs(dx) < 4 && dy < 8 && dy > -16) {
                    c = col(220, 40, 40);
                } else if (abs(dy) < 3 && dx < 12 && dx > -12) {
                    c = col(220, 40, 40);
                } else {
                    c = col(240, 240, 240);
                }
            }
            t->pixels[y * TEX_SIZE + x] = c;
        }
    }
}

static void gen_ammo(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
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

static void gen_armor(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
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

static void gen_weapon(Texture *t)
{
    /* wide aspect pistol at bottom: w=128 h=128 */
    if (texture_alloc(t, 128, 128) != 0) {
        return;
    }
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

static void gen_weapon_shotgun(Texture *t)
{
    if (texture_alloc(t, 128, 128) != 0) {
        return;
    }
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

void assets_gen_missing(Texture *t)
{
    if (texture_alloc(t, TEX_SIZE, TEX_SIZE) != 0) {
        return;
    }
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int dark = (((x / 8) + (y / 8)) % 2) == 0;
            t->pixels[y * TEX_SIZE + x] = dark ? col(0, 0, 0) : col(255, 0, 255);
        }
    }
}

/* ---- File-backed sheets ---- */

int assets_load_png(Texture *tex, const char *path)
{
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
        px[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) |
                (uint32_t)p[0];
    }
    stbi_image_free(data);
    tex->w = tex->fw = w;
    tex->h = tex->fh = h;
    tex->cols = tex->rows = 1;
    tex->pixels = px;
    return 0;
}

/* Background colour of every prepared sheet, in the make_color() byte order.
 * Kept in step with KEY_R/G/B in tools/sheet_common.h by hand: the tools are
 * built separately from the game and share no header with it. */
#define KEY_COLOR 0x00FF00FFu

int assets_parse_frame_size(const char *path, int *fw, int *fh)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    if (!dot) {
        return -1;
    }
    for (const char *p = dot - 1; p > base; p--) {
        if (*p != '_') {
            continue;
        }
        int w = 0, h = 0, n = 0;
        if (sscanf(p + 1, "%dx%d%n", &w, &h, &n) == 2 && p + 1 + n == dot && w > 0 && h > 0) {
            *fw = w;
            *fh = h;
            return 0;
        }
    }
    return -1;
}

int assets_load_sheet(Texture *t, const char *path, int fw, int fh)
{
    int name_fw = 0, name_fh = 0;
    if (fw <= 0 || fh <= 0) {
        fprintf(stderr, "assets: %s: frame size %dx%d requested\n", path, fw, fh);
        return -1;
    }
    if (assets_parse_frame_size(path, &name_fw, &name_fh) != 0) {
        fprintf(stderr, "assets: %s: name carries no _<w>x<h> frame size\n", path);
        return -1;
    }
    if (name_fw != fw || name_fh != fh) {
        fprintf(stderr, "assets: %s: name says %dx%d, code expects %dx%d\n", path, name_fw, name_fh,
                fw, fh);
        return -1;
    }

    Texture tmp;
    memset(&tmp, 0, sizeof(tmp));
    if (assets_load_png(&tmp, path) != 0) {
        return -1;
    }
    if (tmp.w % fw != 0 || tmp.h % fh != 0) {
        fprintf(stderr, "assets: %s: %dx%d is not a whole number of %dx%d frames\n", path, tmp.w,
                tmp.h, fw, fh);
        texture_free(&tmp);
        return -1;
    }

    /* Binary transparency. The renderer skips a pixel only at alpha 0 and
     * draws every other one fully opaque, so a soft edge from the model would
     * come out as a rim and the key colour as a pink border. */
    size_t n = (size_t)tmp.w * (size_t)tmp.h;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = tmp.pixels[i];
        uint32_t rgb = c & 0x00FFFFFFu;
        if (rgb == KEY_COLOR) {
            tmp.pixels[i] = rgb;
            continue;
        }
        tmp.pixels[i] = rgb | (((c >> 24) < 128u) ? 0u : 0xFF000000u);
    }

    tmp.fw = fw;
    tmp.fh = fh;
    tmp.cols = tmp.w / fw;
    tmp.rows = tmp.h / fh;
    texture_free(t);
    *t = tmp;
    return 0;
}

/* One file-backed slot: the name the sheet is looked for under (without the
 * extension), the frame size the code expects it in, and the field it takes
 * over. A missing file is a normal state — the slot keeps whatever the
 * generators put there and nothing is printed. */
typedef struct {
    const char *path;
    int fw, fh;
    size_t field; /* offset into Assets */
} SheetSlot;

static const SheetSlot SHEETS[] = {
        {"assets/weapons/pistol_fp_128x128", 128, 128, offsetof(Assets, weapon_pistol)},
        {"assets/weapons/shotgun_fp_128x128", 128, 128, offsetof(Assets, weapon_shotgun)},
        {"assets/players/marine/idle_64x64", 64, 64, offsetof(Assets, sprite_enemy)},
        {"assets/players/swat/idle_64x64", 64, 64, offsetof(Assets, sprite_enemy_serg)},
        {"assets/players/marine/death_64x64", 64, 64, offsetof(Assets, sprite_enemy_dead)},
};

static void load_sheets(Assets *a)
{
    for (size_t i = 0; i < sizeof(SHEETS) / sizeof(SHEETS[0]); i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s.png", SHEETS[i].path);
        FILE *f = fopen(path, "rb");
        if (!f) {
            continue;
        }
        fclose(f);
        assets_load_sheet((Texture *)((char *)a + SHEETS[i].field), path, SHEETS[i].fw,
                          SHEETS[i].fh);
    }
}

int assets_init(Assets *a)
{
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
    Texture *all[] = {&a->wall_brick,        &a->wall_door,         &a->floor_tex,
                      &a->ceiling_tex,       &a->sprite_barrel,     &a->sprite_enemy,
                      &a->sprite_enemy_serg, &a->sprite_enemy_dead, &a->sprite_medkit,
                      &a->sprite_ammo,       &a->sprite_armor,      &a->weapon_pistol,
                      &a->weapon_shotgun};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        if (!all[i]->pixels) {
            fprintf(stderr, "assets: procedural texture %zu failed\n", i);
            return -1;
        }
    }

    /* Files win over the generators where they exist; a rejected file leaves
     * the procedural texture in place, so this cannot fail the init. */
    load_sheets(a);
    return 0;
}

void assets_shutdown(Assets *a)
{
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
