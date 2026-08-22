#ifndef PLAYER_H
#define PLAYER_H

#include "map.h"
#include "weapon.h"
#include <stdint.h>

/* InputState is defined in input.h to avoid a circular dependency. */
struct InputState;
struct DoorList;

/* Simulation state of one player. Nothing here comes from the renderer:
 * facing is a single angle, which packs into one float on the wire and
 * interpolates along the shortest arc between snapshots. */
typedef struct PlayerState {
    uint8_t id;
    float x, y;
    float angle; /* radians, normalised to [-PI, PI] */
    float hp;
    float armor;
    int alive;
    float respawn_timer;
    int8_t last_attacker; /* id of the player who damaged this one last, -1 for none */
    WeaponSystem weapons; /* ammo, cooldowns and selection are per player */
} PlayerState;

void player_init(PlayerState *p, int start_x, int start_y);

/* Take damage: armor absorbs half of it first, the rest comes off hp, which
 * stops at zero. `attacker_id` is the player who dealt it, recorded in
 * last_attacker so a frag can be attributed; pass -1 for damage that belongs
 * to no player. */
void player_damage(PlayerState *victim, float dmg, int attacker_id);

/* dt update. If dl is non-NULL, doors are respected for collision. */
void player_update(PlayerState *p, const Map *m, struct DoorList *dl, const struct InputState *in,
                   double dt);

#endif
