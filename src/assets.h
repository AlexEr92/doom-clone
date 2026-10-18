#ifndef ASSETS_H
#define ASSETS_H

#include <stddef.h>

#include "config.h"
#include "utils.h"
#include "weapon.h"

#define TEX_SIZE 64

/* A texture is a sheet of frames laid out left to right (columns) and top to
 * bottom (rows); the row stride is the width of the whole image, not of one
 * frame. Columns are the frames of an animation, rows the facings. Everything
 * generated procedurally is one frame — fw == w, fh == h and cols == rows == 1. */
typedef struct {
    int w, h;         /* whole image */
    int fw, fh;       /* one frame */
    int cols, rows;   /* w / fw, h / fh */
    uint32_t *pixels; /* ARGB */
} Texture;

/* The animations every character has, one sheet each: the order is the order
 * of Assets.skin_anim, and each one is loaded from the file of the same name
 * in the character's directory. */
typedef enum { ANIM_IDLE = 0, ANIM_RUN, ANIM_SHOOT, ANIM_PAIN, ANIM_DEATH, ANIM_COUNT } AnimId;

#define SKIN_NAME_MAX 32

typedef struct {
    Texture wall_brick; /* cell type 1 */
    Texture wall_door;  /* cell type 2 */
    Texture floor_tex;
    Texture ceiling_tex;
    Texture sprite_barrel;
    /* Every character, players and enemies alike, with no procedural
     * variant: the skin number is the line of assets/players/skins.txt the
     * character is named on, and a sheet with no file behind it is the
     * checker. */
    Texture skin_anim[MAX_SKINS][ANIM_COUNT];
    char skin_names[MAX_SKINS][SKIN_NAME_MAX]; /* "" from skin_count on */
    int skin_count;
    Texture missing; /* the checker, for a character whose skin is not listed */
    Texture sprite_medkit;
    Texture sprite_ammo;
    Texture sprite_armor;
    /* The weapon in hand, by WeaponType. The pistol and the shotgun have a
     * procedural variant; a weapon without one is the checker when its file
     * is missing, never another weapon. */
    Texture weapon_fp[WEAPON_COUNT];
    /* The status bar. Neither has a procedural variant nor the checker: with
     * no file the slot stays empty (pixels NULL), hud_draw() draws a face of
     * its own and the weapon's box shows its key without the icon. */
    Texture hud_face;               /* frames in the FaceFrame order of hud.h */
    Texture hud_icon[WEAPON_COUNT]; /* by WeaponType; only the alpha is used */
} Assets;

int assets_init(Assets *a);
void assets_shutdown(Assets *a);

/* Load a PNG from path into tex as one frame, freeing the pixels tex held
 * before, so tex must be zeroed or a texture of its own. Returns 0 on success;
 * on failure reports it on stderr, leaves tex alone and returns non-zero. */
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

/* Magenta/black checker, 64x64, one frame: what an entity or a weapon with no
 * procedural variant shows when its file is missing or rejected. */
void assets_gen_missing(Texture *t);

/* Read the list of skins out of <dir>/skins.txt, one name per line in skin
 * number order, blank lines skipped, at most MAX_SKINS of them, and load the
 * sheets of each from <dir>/<name>/<anim>_64x64.png into a->skin_anim. A
 * missing list means no skins; a missing sheet leaves its slot as it was.
 * assets_init() calls it with "assets/players". */
void assets_load_skins(Assets *a, const char *dir);

/* Skin number of the character called `name`, or -1 when the list lacks it. */
int assets_skin_index(const Assets *a, const char *name);

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
