#ifndef SPRITE_H
#define SPRITE_H

#include "engine.h"
#include "player.h"
#include "map.h"
#include "assets.h"

typedef enum {
    SPRITE_BARREL = 0,
    SPRITE_ENEMY,      /* imp (green) */
    SPRITE_MEDKIT,
    SPRITE_AMMO,
    SPRITE_ARMOR,
    SPRITE_ENEMY_SERG, /* sergeant (brown) */
    SPRITE_ENEMY_DEAD, /* corpse */
} SpriteType;

typedef struct {
    float x, y;
    int type;     /* SpriteType */
    int active;   /* 1 = visible/rendered */
    float scale;  /* size multiplier (default 1.0) */
    int vmove;    /* vertical screen shift in px (positive = down) */
} Sprite;

#define MAX_SPRITES 96

typedef struct {
    Sprite items[MAX_SPRITES];
    int count;
} SpriteList;

void sprite_init(SpriteList *sl);
int  sprite_add(SpriteList *sl, float x, float y, int type);
int  sprite_add_full(SpriteList *sl, float x, float y, int type, float scale, int vmove);
void sprite_clear(SpriteList *sl);

/* Render all sprites with Z-buffer test against zBuffer. Must be called
 * AFTER raycast_render (which fills zBuffer). */
void sprite_render(Framebuffer *fb, const SpriteList *sl, const Player *p, const Assets *a);

#endif