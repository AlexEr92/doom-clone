#ifndef PLAYER_H
#define PLAYER_H

#include "map.h"

/* InputState is defined in input.h to avoid a circular dependency. */
struct InputState;
struct DoorList;

typedef struct {
    float x, y;
    float dirX, dirY;
    float planeX, planeY;
    float hp;
    float armor;
} Player;

void player_init(Player *p, int start_x, int start_y);

/* dt update. If dl is non-NULL, doors are respected for collision. */
void player_update(Player *p, const Map *m, struct DoorList *dl,
                   const struct InputState *in, double dt);

#endif