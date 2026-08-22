#ifndef WEAPON_H
#define WEAPON_H

/* Nothing is included here on purpose: player.h includes this header to embed
 * WeaponSystem in PlayerState, so the dependency must not point back. */
struct PlayerState;
struct EnemyList;
struct SpriteList;
struct Audio;

typedef enum { WEAPON_PISTOL = 0, WEAPON_SHOTGUN, WEAPON_COUNT } WeaponType;

typedef struct {
    int type;
    int ammo; /* current ammo */
    int max_ammo;
    float damage;   /* per pellet/bullet */
    int pellets;    /* 1 for pistol, >1 for shotgun */
    float spread;   /* radians of random spread (per pellet) */
    float fire_cd;  /* seconds between shots */
    float cooldown; /* current cooldown remaining */
    float anim;     /* animation timer (0..1; 1 = just fired, decays to 0) */
} Weapon;

typedef struct {
    Weapon weapons[WEAPON_COUNT];
    int current;       /* current weapon index */
    float fire_button; /* 0/1 from input */
} WeaponSystem;

/* Called by player_init(): a WeaponSystem only ever exists inside a player. */
void weapon_system_init(WeaponSystem *ws);

/* Switch p's weapon by index (0..WEAPON_COUNT-1). */
void weapon_switch(struct PlayerState *p, int idx);

/* Try to fire p's current weapon. Performs hitscan against enemies:
 * - ray(s) from player along view dir (+ spread)
 * - finds nearest enemy whose screen AABB contains the ray's screen column
 *   and is not occluded by a wall (zBuffer check)
 * Applies damage and triggers weapon anim + cooldown.
 * au may be NULL (no audio).
 */
void weapon_try_fire(struct PlayerState *p, struct EnemyList *el, struct SpriteList *sl,
                     struct Audio *au);

/* Per-tick update of p's cooldowns / animation. */
void weapon_update(struct PlayerState *p, double dt);

#endif
