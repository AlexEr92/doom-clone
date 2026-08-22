#ifndef WEAPON_H
#define WEAPON_H

/* Nothing is included here on purpose: player.h includes this header to embed
 * WeaponSystem in PlayerState, so the dependency must not point back. */
struct PlayerState;
struct EnemyList;
struct SpriteList;
struct EventQueue;
struct Map;
struct DoorList;

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

/* Try to fire p's current weapon. One world ray per pellet, cast from the
 * shooter along its facing plus the weapon's spread, stopped by walls and
 * closed doors; whatever live entity it reaches first takes the damage.
 * `players` is the array p itself lives in, so a shot cannot hit its owner.
 * el and sl may be NULL for a world with no enemies; evq may be NULL.
 * Also triggers the weapon animation and cooldown.
 */
void weapon_try_fire(struct PlayerState *p, const struct Map *m, const struct DoorList *dl,
                     struct PlayerState *players, int player_count, struct EnemyList *el,
                     struct SpriteList *sl, struct EventQueue *evq);

/* Per-tick update of p's cooldowns / animation. */
void weapon_update(struct PlayerState *p, double dt);

#endif
