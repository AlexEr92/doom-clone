#ifndef SPRITE_H
#define SPRITE_H

/* The render list, and nothing else: a client rebuilds it out of the world
 * every frame with sprites_build_from_world() and draws it. The simulation
 * never touches it — a dedicated server has no sprites at all — which is why
 * this header is reached from main.c and sprite.c only. */

#include "config.h"
#include "utils.h"
#include "player.h"
#include "camera.h"
#include "map.h"
#include "assets.h"

struct World;

typedef enum {
    SPRITE_BARREL = 0,
    SPRITE_ENEMY, /* imp (green) */
    SPRITE_MEDKIT,
    SPRITE_AMMO,
    SPRITE_ARMOR,
    SPRITE_ENEMY_SERG,      /* sergeant (brown) */
    SPRITE_ENEMY_DEAD,      /* imp corpse */
    SPRITE_ENEMY_SERG_DEAD, /* sergeant corpse */
    SPRITE_PLAYER,          /* another player, coloured by id */
} SpriteType;

typedef struct {
    float x, y;
    int type;    /* SpriteType */
    int active;  /* 1 = visible/rendered */
    float scale; /* size multiplier (default 1.0) */
    int vmove;   /* vertical screen shift in px (positive = down) */
    int skin;    /* which sheet of the type: the player's colour, 0 elsewhere */
    int view;    /* row of the sheet: which way the entity faces, 0 = at the camera */
} Sprite;

typedef struct SpriteList {
    Sprite items[MAX_SPRITES];
    int count;
} SpriteList;

void sprite_init(SpriteList *sl);
int sprite_add(SpriteList *sl, float x, float y, int type);
int sprite_add_full(SpriteList *sl, float x, float y, int type, float scale, int vmove);
void sprite_clear(SpriteList *sl);

/* Rebuild the list out of the world: enemies (alive or corpses), pickups still
 * on the map, the barrels the map places and every player but `local_player`.
 * Called once a frame, before sprite_render(); `local_player` is the player the
 * camera looks through and is left out of the list. */
void sprites_build_from_world(SpriteList *sl, const struct World *w, int local_player);

/* Render all sprites with Z-buffer test against zBuffer. Must be called
 * AFTER raycast_render (which fills zBuffer). */
void sprite_render(Framebuffer *fb, const SpriteList *sl, const PlayerState *p, const Camera *cam,
                   const Assets *a);

#endif
