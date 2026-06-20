#ifndef PLAYER_H
#define PLAYER_H

#include "map.h"

typedef struct {
    int forward, back, turn_left, turn_right, strafe_left, strafe_right;
    int mouse_dx;
} InputState;

typedef struct {
    float x, y;
    float dirX, dirY;
    float planeX, planeY;
} Player;

void player_init(Player *p, int start_x, int start_y);
void player_update(Player *p, const Map *m, const InputState *in, double dt);

#endif