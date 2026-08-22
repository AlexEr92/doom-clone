#ifndef PLAYER_H
#define PLAYER_H

#include "map.h"
#include <stdint.h>

/* InputState is defined in input.h to avoid a circular dependency. */
struct InputState;
struct DoorList;

/* Simulation state of one player. Nothing here comes from the renderer:
 * facing is a single angle, which packs into one float on the wire and
 * interpolates along the shortest arc between snapshots. */
typedef struct {
    uint8_t id;
    float x, y;
    float angle; /* radians, normalised to [-PI, PI] */
    float hp;
    float armor;
    int alive;
    float respawn_timer;
} PlayerState;

void player_init(PlayerState *p, int start_x, int start_y);

/* dt update. If dl is non-NULL, doors are respected for collision. */
void player_update(PlayerState *p, const Map *m, struct DoorList *dl, const struct InputState *in,
                   double dt);

#endif
