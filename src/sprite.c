#include "sprite.h"
#include "raycast.h"
#include "utils.h"
#include <math.h>
#include <stdlib.h>

void sprite_init(SpriteList *sl) {
    memset(sl, 0, sizeof(*sl));
}

int sprite_add(SpriteList *sl, float x, float y, int type) {
    if (sl->count >= MAX_SPRITES) return -1;
    sl->items[sl->count].x = x;
    sl->items[sl->count].y = y;
    sl->items[sl->count].type = type;
    sl->items[sl->count].active = 1;
    return sl->count++;
}

void sprite_clear(SpriteList *sl) {
    sl->count = 0;
}

static const Texture *sprite_texture(const Assets *a, int type) {
    switch (type) {
        case SPRITE_BARREL:  return &a->sprite_barrel;
        case SPRITE_ENEMY:   return &a->sprite_enemy;
        case SPRITE_MEDKIT:  return &a->sprite_medkit;
        case SPRITE_AMMO:    return &a->sprite_ammo;
        case SPRITE_ARMOR:   return &a->sprite_armor;
        default:             return &a->sprite_barrel;
    }
}

typedef struct {
    int idx;
    float dist;
} SpriteOrder;

static int cmp_order(const void *a, const void *b) {
    const SpriteOrder *sa = (const SpriteOrder *)a;
    const SpriteOrder *sb = (const SpriteOrder *)b;
    /* Sort far-first (larger dist first) so near sprites draw on top. */
    if (sa->dist < sb->dist) return 1;
    if (sa->dist > sb->dist) return -1;
    return 0;
}

void sprite_render(Framebuffer *fb, const SpriteList *sl, const Player *p, const Assets *a) {
    SpriteOrder order[MAX_SPRITES];
    int n = 0;
    for (int i = 0; i < sl->count; i++) {
        if (!sl->items[i].active) continue;
        float dx = sl->items[i].x - p->x;
        float dy = sl->items[i].y - p->y;
        order[n].idx = i;
        order[n].dist = dx * dx + dy * dy;
        n++;
    }
    if (n == 0) return;
    qsort(order, (size_t)n, sizeof(SpriteOrder), cmp_order);

    /* inverse camera: transform world delta to camera space
     * [ camX ] = [ dirY  -dirX ] [ dx ]
     * [ camY ]   [ plane...      ]   ... actually standard lodev:
     * invDet used. We use:
     *   u = (dirY*dx - dirX*dy)
     *   v = (-planeY*dx + planeX*dy)  ... we need determinant. */
    float dirX = p->dirX, dirY = p->dirY;
    float planeX = p->planeX, planeY = p->planeY;
    float det = planeX * dirY - planeY * dirX; /* determinant of [planeX planeY; dirX dirY] */

    for (int s = 0; s < n; s++) {
        const Sprite *sp = &sl->items[order[s].idx];
        float dx = sp->x - p->x;
        float dy = sp->y - p->y;
        float transformX = (dirY * dx - dirX * dy) / det;
        float transformY = (-planeY * dx + planeX * dy) / det;
        if (transformY <= 0.1f) continue; /* behind camera */

        const Texture *tex = sprite_texture(a, sp->type);
        int texW = tex->w;
        int texH = tex->h;

        int spriteScreenX = (int)((SCREEN_W / 2.0f) * (1.0f + transformX / transformY));

        int spriteHeight = (int)fabsf((float)SCREEN_H / transformY);
        int vMoveScreen = 0;
        int drawStartY = -spriteHeight / 2 + SCREEN_H / 2 + vMoveScreen;
        int drawEndY   =  spriteHeight / 2 + SCREEN_H / 2 + vMoveScreen;

        int spriteWidth = spriteHeight; /* square aspect ratio */
        int drawStartX = -spriteWidth / 2 + spriteScreenX;
        int drawEndX   =  spriteWidth / 2 + spriteScreenX;

        if (drawStartY < 0) drawStartY = 0;
        if (drawEndY >= SCREEN_H) drawEndY = SCREEN_H - 1;
        if (drawStartX < 0) drawStartX = 0;
        if (drawEndX >= SCREEN_W) drawEndX = SCREEN_W - 1;

        for (int x = drawStartX; x <= drawEndX; x++) {
            if (transformY >= zBuffer[x]) continue; /* behind wall */
            int texX = (int)((float)(x - (-spriteWidth / 2 + spriteScreenX)) /
                             (float)spriteWidth * (float)texW);
            if (texX < 0 || texX >= texW) continue;

            uint32_t *col_ptr = fb->pixels + (size_t)drawStartY * SCREEN_W + x;
            float stepY = (float)texH / (float)spriteHeight;
            float texPos = ((float)drawStartY - (float)SCREEN_H / 2.0f + (float)spriteHeight / 2.0f - (float)vMoveScreen) * stepY;
            for (int y = drawStartY; y <= drawEndY; y++) {
                int texY = (int)texPos;
                if (texY < 0) { texPos += stepY; continue; }
                if (texY >= texH) { texPos += stepY; continue; }
                texPos += stepY;
                uint32_t c = tex->pixels[(size_t)texY * texW + texX];
                if ((c & 0xFF000000u) == 0) continue; /* transparent */
                *col_ptr = c;
                col_ptr += SCREEN_W;
            }
        }
    }
}