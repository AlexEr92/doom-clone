#ifndef PLAYER_H
#define PLAYER_H

#include "map.h"
#include "weapon.h"
#include <stdint.h>

/* PlayerInput is defined in input.h to avoid a circular dependency. */
struct PlayerInput;
struct DoorList;
struct EventQueue;

/* Simulation state of one player. Nothing here comes from the renderer:
 * facing is a single angle, which packs into one float on the wire and
 * interpolates along the shortest arc between snapshots. */
typedef struct PlayerState {
    uint8_t id;
    int active; /* the slot is taken: someone joined it and has not left */
    float x, y;
    float angle; /* radians, normalised to [-PI, PI] */
    float hp;
    float armor;
    int alive;
    float respawn_timer;       /* while dead: seconds until world_step() brings them back */
    float spawn_protect_timer; /* seconds of being unhurt left after a respawn */
    int8_t last_attacker;      /* id of the player who damaged this one last, -1 for none */
    WeaponSystem weapons;      /* ammo, cooldowns and selection are per player */
} PlayerState;

void player_init(PlayerState *p, int start_x, int start_y);

/* Take damage: armor absorbs half of it first, but no more armor than is
 * left, and the rest comes off hp, which stops at zero. Every hit, whoever
 * dealt it, is announced as EV_PLAYER_HURT coming from (src_x, src_y) — the
 * enemy's position, or the shooter's. evq may be NULL (event dropped).
 * `attacker_id` is the player who dealt it, recorded in last_attacker so a
 * frag can be attributed; pass -1 for damage that belongs to no player, which
 * leaves last_attacker as it was: a player who wounded the victim keeps the
 * credit when an enemy finishes them off.
 *
 * The hit that takes hp to zero kills: the victim is no longer alive, is due
 * back in RESPAWN_TIME, and EV_PLAYER_DEATH follows the hurt. Dying here and
 * not at the end of the tick is what keeps a player shot dead from firing
 * back in the same tick, and a later shot in it from taking the frag over.
 * A player who is dead already, or still protected after a respawn, is not
 * hurt at all and nothing is announced. */
void player_damage(PlayerState *victim, float dmg, int attacker_id, float src_x, float src_y,
                   struct EventQueue *evq);

/* dt update. The player takes the facing in `in` as given — the client has
 * already turned it — and moves along it by the buttons pressed.
 * If dl is non-NULL, doors are respected for collision.
 * `players` is the array p itself lives in: the others are solid, so nobody
 * walks through anybody. Pass NULL and 0 for a world with no one else in it. */
void player_update(PlayerState *p, const Map *m, struct DoorList *dl, const struct PlayerInput *in,
                   const PlayerState *players, int player_count, double dt);

#endif
