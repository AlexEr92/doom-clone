#include "sprite.h"
#include "raycast.h"
#include "world.h"
#include "utils.h"
#include <math.h>
#include <stdlib.h>

void sprite_init(SpriteList *sl)
{
    memset(sl, 0, sizeof(*sl));
}

int sprite_add(SpriteList *sl, float x, float y, int type)
{
    /* Default per-type scale so entities stand on the floor at a sensible
     * height relative to walls (wall-tall = 1.0). */
    float scale = 1.0f;
    switch (type) {
        case SPRITE_ENEMY: scale = 0.72f; break;
        case SPRITE_ENEMY_SERG: scale = 0.78f; break;
        case SPRITE_ENEMY_DEAD: scale = 0.55f; break;
        case SPRITE_PLAYER: scale = 0.78f; break;
        case SPRITE_BARREL: scale = 0.55f; break;
        /* One scale for every pickup: their sheets are cut out of one
         * picture at one scale, so how big each item is next to the others
         * is already in the frame. */
        case SPRITE_MEDKIT:
        case SPRITE_AMMO:
        case SPRITE_ARMOR: scale = 0.36f; break;
        default: scale = 1.0f; break;
    }
    return sprite_add_full(sl, x, y, type, scale, 0);
}

int sprite_add_full(SpriteList *sl, float x, float y, int type, float scale, int vmove)
{
    if (sl->count >= MAX_SPRITES) {
        return -1;
    }
    sl->items[sl->count].x = x;
    sl->items[sl->count].y = y;
    sl->items[sl->count].type = type;
    sl->items[sl->count].active = 1;
    sl->items[sl->count].scale = scale;
    sl->items[sl->count].vmove = vmove;
    sl->items[sl->count].skin = 0;
    sl->items[sl->count].view = 0;
    return sl->count++;
}

void sprite_clear(SpriteList *sl)
{
    sl->count = 0;
}

/* Which of the four rows of a player sheet the camera sees: the facing of the
 * player measured against the direction from them to the viewer. Row 0 looks
 * back at the camera, row 2 away from it, rows 1 and 3 are the profiles — 1
 * facing the right edge of the screen, 3 the left one. A sheet with fewer rows
 * than that falls back to row 0 inside texture_frame(). */
static int player_view(const PlayerState *p, float cam_x, float cam_y)
{
    float rel = wrap_angle(p->angle - atan2f(cam_y - p->y, cam_x - p->x));
    int q = (int)floorf((rel + MATH_PI / 4.0f) / (MATH_PI / 2.0f));
    return ((q % 4) + 4) % 4;
}

static int enemy_sprite_type(const Enemy *e)
{
    if (e->state == ESTATE_DEAD) {
        return SPRITE_ENEMY_DEAD;
    }
    return (e->type == ENEMY_SERG) ? SPRITE_ENEMY_SERG : SPRITE_ENEMY;
}

/* -1 for a pickup nothing draws. */
static int item_sprite_type(int type)
{
    switch (type) {
        case ITEM_MEDKIT: return SPRITE_MEDKIT;
        case ITEM_AMMO: return SPRITE_AMMO;
        case ITEM_ARMOR: return SPRITE_ARMOR;
        default: return -1;
    }
}

void sprites_build_from_world(SpriteList *sl, const World *w, int local_player)
{
    sprite_clear(sl);

    for (int i = 0; i < w->enemies.count; i++) {
        const Enemy *e = &w->enemies.items[i];
        sprite_add(sl, e->x, e->y, enemy_sprite_type(e));
    }

    for (int i = 0; i < w->items.count; i++) {
        const Item *it = &w->items.items[i];
        int type = item_sprite_type(it->type);
        if (!it->active || type < 0) {
            continue;
        }
        sprite_add(sl, it->x, it->y, type);
    }

    /* Barrels are decor and carry no state of their own, so the map they were
     * placed from is what restores them rather than anything in the world. */
    for (int i = 0; i < w->map.sprite_count; i++) {
        if (w->map.sprites[i].marker == 'B') {
            sprite_add(sl, w->map.sprites[i].x, w->map.sprites[i].y, SPRITE_BARREL);
        }
    }

    /* Everyone but the player the camera looks through: their own body is
     * always at the camera and would fill the screen with itself. */
    const PlayerState *me =
            (local_player >= 0 && local_player < MAX_PLAYERS) ? &w->players[local_player] : NULL;
    for (int i = 0; i < w->player_count; i++) {
        const PlayerState *p = &w->players[i];
        if (i == local_player || !p->active || !p->alive) {
            continue;
        }
        int idx = sprite_add(sl, p->x, p->y, SPRITE_PLAYER);
        if (idx < 0) {
            break;
        }
        /* Colour by id until 10-04 hands out real skins, and the facing so a
         * deathmatch can tell whether it has been noticed. */
        sl->items[idx].skin = p->id;
        sl->items[idx].view = me ? player_view(p, me->x, me->y) : 0;
    }
}

static const Texture *sprite_texture(const Assets *a, const Sprite *sp)
{
    switch (sp->type) {
        case SPRITE_BARREL: return &a->sprite_barrel;
        case SPRITE_ENEMY: return &a->sprite_enemy;
        case SPRITE_ENEMY_SERG: return &a->sprite_enemy_serg;
        case SPRITE_ENEMY_DEAD: return &a->sprite_enemy_dead;
        case SPRITE_MEDKIT: return &a->sprite_medkit;
        case SPRITE_AMMO: return &a->sprite_ammo;
        case SPRITE_ARMOR: return &a->sprite_armor;
        case SPRITE_PLAYER: return &a->sprite_player[clampi(sp->skin, 0, MAX_PLAYERS - 1)];
        default: return &a->sprite_barrel;
    }
}

/* Projection is not isotropic: at unit depth a world unit is SCREEN_H pixels
 * tall but (SCREEN_W / 2) / FOV_PLANE pixels wide, because the horizontal
 * projection divides by the length of the camera plane and that length is the
 * FOV. Deriving the sprite width from its height alone therefore draws a
 * square frame square on the screen and 17.5% too narrow in the world. */
#define SPRITE_ASPECT ((float)SCREEN_W / (2.0f * FOV_PLANE * (float)SCREEN_H))

typedef struct {
    int idx;
    float dist;
} SpriteOrder;

static int cmp_order(const void *a, const void *b)
{
    const SpriteOrder *sa = (const SpriteOrder *)a;
    const SpriteOrder *sb = (const SpriteOrder *)b;
    /* Sort far-first (larger dist first) so near sprites draw on top. */
    if (sa->dist < sb->dist) {
        return 1;
    }
    if (sa->dist > sb->dist) {
        return -1;
    }
    return 0;
}

void sprite_render(Framebuffer *fb, const SpriteList *sl, const PlayerState *p, const Camera *cam,
                   const Assets *a)
{
    SpriteOrder order[MAX_SPRITES];
    int n = 0;
    for (int i = 0; i < sl->count; i++) {
        if (!sl->items[i].active) {
            continue;
        }
        float dx = sl->items[i].x - p->x;
        float dy = sl->items[i].y - p->y;
        order[n].idx = i;
        order[n].dist = dx * dx + dy * dy;
        n++;
    }
    if (n == 0) {
        return;
    }
    qsort(order, (size_t)n, sizeof(SpriteOrder), cmp_order);

    /* inverse camera: transform world delta to camera space
     * [ camX ] = [ dir_y  -dir_x ] [ dx ]
     * [ camY ]   [ plane...      ]   ... actually standard lodev:
     * invDet used. We use:
     *   u = (dir_y*dx - dir_x*dy)
     *   v = (-plane_y*dx + plane_x*dy)  ... we need determinant. */
    float dir_x = cam->dir_x, dir_y = cam->dir_y;
    float plane_x = cam->plane_x, plane_y = cam->plane_y;
    float det =
            plane_x * dir_y - plane_y * dir_x; /* determinant of [plane_x plane_y; dir_x dir_y] */

    for (int s = 0; s < n; s++) {
        const Sprite *sp = &sl->items[order[s].idx];
        float dx = sp->x - p->x;
        float dy = sp->y - p->y;
        float transformX = (dir_y * dx - dir_x * dy) / det;
        float transformY = (-plane_y * dx + plane_x * dy) / det;
        if (transformY <= 0.1f) {
            continue; /* behind camera */
        }

        const Texture *tex = sprite_texture(a, sp);
        /* Rows are the facings the sprite carries; the column stays 0 until
         * 10-03 puts an animation frame in Sprite. A corpse has nothing to
         * play and lies in the last frame of its death sheet. Frames sit side
         * by side in one image, so the row stride stays tex->w. */
        int col = (sp->type == SPRITE_ENEMY_DEAD) ? tex->cols - 1 : 0;
        const uint32_t *frame = texture_frame(tex, col, sp->view);
        int texW = tex->fw;
        int texH = tex->fh;

        int spriteScreenX = (int)((SCREEN_W / 2.0f) * (1.0f + transformX / transformY));

        /* Sprite vertical placement: anchor BOTTOM to the floor line.
         * The floor contact at perpendicular distance transformY projects to
         * the same y as a wall's bottom edge: y = H/2 + (H/transformY)/2.
         * Then a scaled sprite stands on the floor instead of floating. */
        float wallHalf = (float)SCREEN_H / transformY * 0.5f;
        int floorY = (int)((float)SCREEN_H / 2.0f + wallHalf);
        int spriteHeight = (int)(fabsf((float)SCREEN_H / transformY) * sp->scale);
        int vMoveScreen = sp->vmove;
        int drawEndY = floorY + vMoveScreen;
        int drawStartY = drawEndY - spriteHeight;

        /* Keep aspect ratio of the frame (texW:texH), in the world rather
         * than on the screen — hence SPRITE_ASPECT. */
        int spriteWidth = (int)((float)spriteHeight * (float)texW / (float)texH * SPRITE_ASPECT);
        int drawStartX = -spriteWidth / 2 + spriteScreenX;
        int drawEndX = spriteWidth / 2 + spriteScreenX;

        /* Unclipped top, kept so texture sampling starts at the first row
         * on screen rather than at the top of the frame. */
        int spriteTopY = drawStartY;
        if (drawStartY < 0) {
            drawStartY = 0;
        }
        if (drawEndY >= SCREEN_H) {
            drawEndY = SCREEN_H - 1;
        }
        if (drawStartX < 0) {
            drawStartX = 0;
        }
        if (drawEndX >= SCREEN_W) {
            drawEndX = SCREEN_W - 1;
        }

        for (int x = drawStartX; x <= drawEndX; x++) {
            if (transformY >= zBuffer[x]) {
                continue; /* behind wall */
            }
            int texX = (int)((float)(x - (-spriteWidth / 2 + spriteScreenX)) / (float)spriteWidth *
                             (float)texW);
            if (texX < 0 || texX >= texW) {
                continue;
            }

            /* The destination row follows y, not the pixels written: a
             * running pointer advanced only on an opaque texel packs the
             * column's opaque pixels against the top of the sprite and
             * smears whatever is below every transparent run. */
            float stepY = (float)texH / (float)spriteHeight;
            float texPos = (float)(drawStartY - spriteTopY) * stepY;
            for (int y = drawStartY; y <= drawEndY; y++) {
                int texY = (int)texPos;
                texPos += stepY;
                if (texY < 0) {
                    continue;
                }
                if (texY >= texH) {
                    continue;
                }
                uint32_t c = frame[(size_t)texY * tex->w + texX];
                if ((c & 0xFF000000u) == 0) {
                    continue; /* transparent */
                }
                fb->pixels[(size_t)y * SCREEN_W + x] = c;
            }
        }
    }
}
