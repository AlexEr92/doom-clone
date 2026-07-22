#include "player.h"
#include "input.h"
#include "door.h"
#include "utils.h"
#include <math.h>

void player_init(Player *p, int start_x, int start_y) {
    p->x = start_x + 0.5f;
    p->y = start_y + 0.5f;
    p->dirX = -1.0f;
    p->dirY = 0.0f;
    p->planeX = 0.0f;
    p->planeY = FOV_PLANE;
    p->hp = 100.0f;
    p->armor = 0.0f;
}

static int blocked(const Map *m, DoorList *dl, float x, float y) {
    return map_is_wall_door(m, dl, x, y);
}

static void try_move(Player *p, const Map *m, DoorList *dl, float nx, float ny) {
    float r = PLAYER_RADIUS;
    if (!blocked(m, dl, nx + (nx > p->x ? r : -r), p->y)) {
        p->x = nx;
    }
    if (!blocked(m, dl, p->x, ny + (ny > p->y ? r : -r))) {
        p->y = ny;
    }
}

void player_update(Player *p, const Map *m, DoorList *dl,
                   const InputState *in, double dt) {
    if (in->forward) {
        try_move(p, m, dl,
                 p->x + p->dirX * MOVE_SPEED * (float)dt,
                 p->y + p->dirY * MOVE_SPEED * (float)dt);
    }
    if (in->back) {
        try_move(p, m, dl,
                 p->x - p->dirX * MOVE_SPEED * (float)dt,
                 p->y - p->dirY * MOVE_SPEED * (float)dt);
    }
    if (in->strafe_left) {
        try_move(p, m, dl,
                 p->x - p->planeX * MOVE_SPEED * (float)dt,
                 p->y - p->planeY * MOVE_SPEED * (float)dt);
    }
    if (in->strafe_right) {
        try_move(p, m, dl,
                 p->x + p->planeX * MOVE_SPEED * (float)dt,
                 p->y + p->planeY * MOVE_SPEED * (float)dt);
    }

    float rot = 0.0f;
    if (in->turn_left)  rot += ROT_SPEED * (float)dt;
    if (in->turn_right) rot -= ROT_SPEED * (float)dt;
    rot -= in->mouse_dx * MOUSE_SENS;

    if (rot != 0.0f) {
        float cosR = cosf(rot);
        float sinR = sinf(rot);
        float oldDirX = p->dirX;
        p->dirX = p->dirX * cosR - p->dirY * sinR;
        p->dirY = oldDirX * sinR + p->dirY * cosR;
        float oldPlaneX = p->planeX;
        p->planeX = p->planeX * cosR - p->planeY * sinR;
        p->planeY = oldPlaneX * sinR + p->planeY * cosR;
    }
}