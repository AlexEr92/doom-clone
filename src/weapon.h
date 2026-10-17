#ifndef WEAPON_H
#define WEAPON_H

/* Nothing is included here on purpose: player.h includes this header to embed
 * WeaponSystem in PlayerState, so the dependency must not point back. */
struct PlayerState;
struct EnemyList;
struct EventQueue;
struct Map;
struct DoorList;

/* The order is the order of the keys 1-5 and of the weapon names in
 * docs/assets-spec.md. */
typedef enum {
    WEAPON_PISTOL = 0,
    WEAPON_SHOTGUN,
    WEAPON_AK47,
    WEAPON_RIFLE,
    WEAPON_GRENADE,
    WEAPON_COUNT
} WeaponType;

/* How long a shot shows, in seconds: a muzzle flash, whatever the weapon. It
 * is not fire_cd — a flash stretched over a slow weapon's cooldown plays in
 * slow motion; the weapon rests for whatever is left of it. */
#define WEAPON_SHOT_TIME 0.15f

typedef struct {
    int type;
    int owned; /* picked up: only an owned weapon can be switched to */
    int ammo;  /* current ammo, kept per weapon whether owned or not */
    int max_ammo;
    float damage;   /* per pellet/bullet */
    int pellets;    /* 1 for pistol, >1 for shotgun */
    float spread;   /* radians of random spread (per pellet) */
    float fire_cd;  /* seconds between shots */
    float cooldown; /* current cooldown remaining */
    float anim;     /* shot animation: 1 = just fired, down to 0 over WEAPON_SHOT_TIME */
} Weapon;

typedef struct {
    Weapon weapons[WEAPON_COUNT];
    int current;       /* current weapon index */
    float fire_button; /* 0/1 from input */
} WeaponSystem;

/* Called by player_init(): a WeaponSystem only ever exists inside a player.
 * The player owns the pistol alone; every other weapon is to be picked up,
 * and comes with no ammo until then. */
void weapon_system_init(WeaponSystem *ws);

/* Switch p's weapon by index (0..WEAPON_COUNT-1). A weapon p does not own is
 * not switched to. */
void weapon_switch(struct PlayerState *p, int idx);

/* Try to fire p's current weapon. One world ray per pellet, cast from the
 * shooter along its facing plus the weapon's spread, stopped by walls and
 * closed doors; whatever live entity it reaches first takes the damage.
 * `players` is the array p itself lives in, so a shot cannot hit its owner.
 * el may be NULL for a world with no enemies; evq may be NULL.
 * Also triggers the weapon animation and cooldown.
 * The grenade launcher fires a projectile, not a ray, and there are no
 * projectiles before task 10-07: with it in hand nothing happens at all.
 */
void weapon_try_fire(struct PlayerState *p, const struct Map *m, const struct DoorList *dl,
                     struct PlayerState *players, int player_count, struct EnemyList *el,
                     struct EventQueue *evq);

/* Per-tick update of p's cooldowns / animation. */
void weapon_update(struct PlayerState *p, double dt);

#endif
