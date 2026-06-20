#ifndef SPRITE_H
#define SPRITE_H

#include "engine.h"
#include "player.h"
#include "map.h"
#include "assets.h"

typedef enum {
    SPRITE_BARREL = 0,
    SPRITE_ENEMY,
    SPRITE_MEDKIT,
    SPRITE_AMMO,
    SPRITE_ARMOR
} SpriteType;

typedef struct {
    float x, y;
    int type;     /* SpriteType */
    int active;   /* 1 = visible/rendered */
} Sprite;

#define MAX_SPRITES 64

typedef struct {
    Sprite items[MAX_SPRITES];
    int count;
} SpriteList;

void sprite_init(SpriteList *sl);
int  sprite_add(SpriteList *sl, float x, float y, int type);
void sprite_clear(SpriteList *sl);

/* Render all sprites with Z-buffer test against zBuffer. Must be called
 * AFTER raycast_render (which fills zBuffer). */
void sprite_render(Framebuffer *fb, const SpriteList *sl, const Player *p, const Assets *a);

#endif