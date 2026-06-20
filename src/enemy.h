#ifndef ENEMY_H
#define ENEMY_H

#include "map.h"
#include "player.h"
#include "sprite.h"

/* FSM states */
typedef enum {
    ESTATE_IDLE = 0,
    ESTATE_ALERT,
    ESTATE_CHASE,
    ESTATE_ATTACK,
    ESTATE_DEAD
} EnemyState;

/* Enemy archetypes */
typedef enum {
    ENEMY_IMP = 0,    /* fast, low hp, melee-ish short range */
    ENEMY_SERG,       /* slower, more hp, higher damage */
    ENEMY_COUNT
} EnemyType;

typedef struct {
    int type;        /* EnemyType */
    EnemyState state;
    float x, y;
    float hp;
    float anim_timer;
    float attack_cooldown;
    int sprite_id;   /* index into SpriteList (kept in sync) */
    float alert_timer; /* time remaining in ALERT before CHASE */
} Enemy;

#define MAX_ENEMIES 32

typedef struct {
    Enemy items[MAX_ENEMIES];
    int count;
} EnemyList;

typedef struct {
    float max_hp;
    float speed;        /* cells/sec */
    float damage;       /* per attack */
    float attack_range; /* cells */
    float attack_cd;    /* seconds between attacks */
    float detect_range; /* cells: line-of-sight within this -> ALERT */
    int sprite_type;    /* SpriteType */
    float sprite_scale;
} EnemyDef;

const EnemyDef *enemy_def(int type);

void enemy_list_init(EnemyList *el);

/* Spawn an enemy; creates a matching sprite in `sl` and stores its id. */
int  enemy_spawn(EnemyList *el, SpriteList *sl, float x, float y, int type);

/* Per-tick update: AI state machine, movement, attacks. Modifies player hp. */
void enemy_update_all(EnemyList *el, SpriteList *sl, const Map *m, Player *pl, double dt);

/* Apply damage to enemy idx; on death switches sprite to corpse. */
void enemy_damage(EnemyList *el, SpriteList *sl, int idx, float dmg);

/* Returns 1 if all enemies are dead. */
int  enemy_all_dead(const EnemyList *el);

#endif