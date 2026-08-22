#ifndef ASSETS_H
#define ASSETS_H

#include <stddef.h>

#include "utils.h"

#define TEX_SIZE 64

/* A texture is a sheet of frames laid out left to right (columns) and top to
 * bottom (rows); the row stride is the width of the whole image, not of one
 * frame. Everything generated procedurally is a single frame, i.e.
 * fw == w, fh == h and cols == rows == 1. */
typedef struct {
    int w, h;         /* whole image */
    int fw, fh;       /* one frame */
    int cols, rows;   /* w / fw, h / fh */
    uint32_t *pixels; /* ARGB */
} Texture;

typedef struct {
    Texture wall_brick; /* cell type 1 */
    Texture wall_door;  /* cell type 2 */
    Texture floor_tex;
    Texture ceiling_tex;
    Texture sprite_barrel;
    Texture sprite_enemy;
    Texture sprite_enemy_serg;
    Texture sprite_enemy_dead;
    Texture sprite_medkit;
    Texture sprite_ammo;
    Texture sprite_armor;
    Texture weapon_pistol;
    Texture weapon_shotgun;
} Assets;

int assets_init(Assets *a);
void assets_shutdown(Assets *a);

/* Load PNG from path into tex (replaces existing). Returns 0 on success. */
int assets_load_png(Texture *tex, const char *path);

/* Load a sheet of fw x fh frames from a PNG: checks the _<fw>x<fh> suffix of
 * the file name against the arguments, checks that the image divides into
 * whole frames, keys #FF00FF out and forces the remaining alpha to 0 or 255.
 * Returns 0 on success; on any mismatch reports it on stderr, leaves t alone
 * and returns non-zero. */
int assets_load_sheet(Texture *t, const char *path, int fw, int fh);

/* Read the _<w>x<h> suffix out of a file name: "run_64x64.png" -> 64, 64.
 * Returns 0 on success, -1 if the name carries no such suffix. */
int assets_parse_frame_size(const char *path, int *fw, int *fh);

/* Magenta/black checker, 64x64, one frame: what an entity with no procedural
 * variant shows when its file is missing or rejected. */
void assets_gen_missing(Texture *t);

/* Allocate w*h pixels and declare the result a single frame. Returns 0 on
 * success, -1 if the allocation failed (the texture is left empty). */
int texture_alloc(Texture *t, int w, int h);

void texture_free(Texture *t);

/* First pixel of frame (col, row). Rows advance by t->fh scanlines of t->w. */
static inline const uint32_t *texture_frame(const Texture *t, int col, int row)
{
    if (col < 0 || col >= t->cols) {
        col = 0;
    }
    if (row < 0 || row >= t->rows) {
        row = 0;
    }
    return t->pixels + (size_t)row * t->fh * t->w + (size_t)col * t->fw;
}

#endif
