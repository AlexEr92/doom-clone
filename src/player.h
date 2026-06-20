#ifndef PLAYER_H
#define PLAYER_H

#include "map.h"

/* InputState is defined in input.h to avoid a circular dependency. */
struct InputState;

typedef struct {
    float x, y;
    float dirX, dirY;
    float planeX, planeY;
    float hp;
    float armor;
} Player;

void player_init(Player *p, int start_x, int start_y);
void player_update(Player *p, const Map *m, const struct InputState *in, double dt);

#endif