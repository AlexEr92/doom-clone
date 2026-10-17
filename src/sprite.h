#ifndef SPRITE_H
#define SPRITE_H

/* The render list, and nothing else: a client rebuilds it out of the world
 * every frame with sprites_build_from_world() and draws it. The simulation
 * never touches it — a dedicated server has no sprites at all — which is why
 * this header is reached from main.c and sprite.c only. */

#include "config.h"
#include "utils.h"
#include "player.h"
#include "enemy.h"
#include "camera.h"
#include "map.h"
#include "assets.h"

struct World;

typedef enum {
    SPRITE_BARREL = 0,
    SPRITE_ENEMY, /* imp */
    SPRITE_MEDKIT,
    SPRITE_AMMO,
    SPRITE_ARMOR,
    SPRITE_ENEMY_SERG, /* sergeant */
    SPRITE_PLAYER,     /* another player */
} SpriteType;

typedef struct {
    float x, y;
    int type;    /* SpriteType */
    int active;  /* 1 = visible/rendered */
    float scale; /* size multiplier (default 1.0) */
    int vmove;   /* vertical screen shift in px (positive = down) */
    int skin;    /* character: skin number, -1 for one skins.txt lacks; 0 elsewhere */
    int anim;    /* character: AnimId, which sheet of the skin; 0 elsewhere */
    int frame;   /* column of the sheet: the frame of the animation */
    int view;    /* row of the sheet: which way the entity faces, 0 = at the camera */
} Sprite;

typedef struct SpriteList {
    Sprite items[MAX_SPRITES];
    int count;
} SpriteList;

/* What the client remembers of one character between frames: the animation
 * it is playing and what it looked like last frame, which is how the client
 * tells running, getting hurt and firing from standing — the world has
 * neither a velocity nor a record of damage or of shots. */
typedef struct {
    int anim;        /* AnimId being played */
    float phase;     /* seconds since it started */
    float pain_left; /* seconds of ANIM_PAIN the last drop in hp is still owed */
    float run_left;  /* seconds of ANIM_RUN the last move is still owed */
    float shot_left; /* seconds of the last shot's flash still to show */
    float x, y, hp;  /* as the last frame saw them */
    float reload;    /* likewise: what goes up when the character fires */
    int seen;        /* 0 until a frame has seen the character: nothing to compare */
} AnimClock;

/* Animation belongs to the client, kept by entity id rather than in Sprite,
 * which is rebuilt every frame: a player who leaves the view and comes back
 * carries on where they were. */
typedef struct {
    AnimClock players[MAX_PLAYERS];
    AnimClock enemies[MAX_ENEMIES];
    int enemy_skin[ENEMY_COUNT]; /* skin number of each EnemyType, -1 if not listed */
} SpriteAnim;

void sprite_init(SpriteList *sl);
int sprite_add(SpriteList *sl, float x, float y, int type);
int sprite_add_full(SpriteList *sl, float x, float y, int type, float scale, int vmove);
void sprite_clear(SpriteList *sl);

/* Resolve the skin of each enemy type against the list `a` loaded, and stop
 * every clock. Called again when the world is reloaded, so that nothing of
 * the last one carries over. */
void sprite_anim_init(SpriteAnim *sa, const Assets *a);

/* Advance every clock by a frame of `dt` seconds and pick each character's
 * animation from what the world shows now against what it showed last frame.
 * A dt of 0 holds every animation where it is. */
void sprite_anim_update(SpriteAnim *sa, const struct World *w, float dt);

/* Rebuild the list out of the world: enemies (alive or corpses), pickups still
 * on the map, the barrels the map places and every player but `local_player`,
 * dead ones included. Characters get their skin, and the animation and frame
 * from `sa`, counted against the length of the sheet in `a`. Called once a
 * frame after sprite_anim_update() and before sprite_render(); `local_player`
 * is the player the camera looks through and is left out of the list. */
void sprites_build_from_world(SpriteList *sl, const struct World *w, int local_player,
                              const SpriteAnim *sa, const Assets *a);

/* Render all sprites with Z-buffer test against zBuffer. Must be called
 * AFTER raycast_render (which fills zBuffer). */
void sprite_render(Framebuffer *fb, const SpriteList *sl, const PlayerState *p, const Camera *cam,
                   const Assets *a);

#endif
