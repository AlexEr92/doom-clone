#include "player.h"
#include "input.h"
#include "door.h"
#include "utils.h"
#include <math.h>

void player_init(Player *p, int start_x, int start_y)
{
    p->x = start_x + 0.5f;
    p->y = start_y + 0.5f;
    p->dir_x = -1.0f;
    p->dir_y = 0.0f;
    p->plane_x = 0.0f;
    p->plane_y = FOV_PLANE;
    p->hp = 100.0f;
    p->armor = 0.0f;
}

static int blocked(const Map *m, DoorList *dl, float x, float y)
{
    return map_is_wall_door(m, dl, x, y);
}

static void try_move(Player *p, const Map *m, DoorList *dl, float nx, float ny)
{
    float r = PLAYER_RADIUS;
    if (!blocked(m, dl, nx + (nx > p->x ? r : -r), p->y)) {
        p->x = nx;
    }
    if (!blocked(m, dl, p->x, ny + (ny > p->y ? r : -r))) {
        p->y = ny;
    }
}

void player_update(Player *p, const Map *m, DoorList *dl, const InputState *in, double dt)
{
    if (in->forward) {
        try_move(p, m, dl, p->x + p->dir_x * MOVE_SPEED * (float)dt,
                 p->y + p->dir_y * MOVE_SPEED * (float)dt);
    }
    if (in->back) {
        try_move(p, m, dl, p->x - p->dir_x * MOVE_SPEED * (float)dt,
                 p->y - p->dir_y * MOVE_SPEED * (float)dt);
    }
    if (in->strafe_left) {
        try_move(p, m, dl, p->x - p->plane_x * MOVE_SPEED * (float)dt,
                 p->y - p->plane_y * MOVE_SPEED * (float)dt);
    }
    if (in->strafe_right) {
        try_move(p, m, dl, p->x + p->plane_x * MOVE_SPEED * (float)dt,
                 p->y + p->plane_y * MOVE_SPEED * (float)dt);
    }

    float rot = 0.0f;
    if (in->turn_left) {
        rot += ROT_SPEED * (float)dt;
    }
    if (in->turn_right) {
        rot -= ROT_SPEED * (float)dt;
    }
    rot -= in->mouse_dx * MOUSE_SENS;

    if (rot != 0.0f) {
        float cosR = cosf(rot);
        float sinR = sinf(rot);
        float oldDirX = p->dir_x;
        p->dir_x = p->dir_x * cosR - p->dir_y * sinR;
        p->dir_y = oldDirX * sinR + p->dir_y * cosR;
        float oldPlaneX = p->plane_x;
        p->plane_x = p->plane_x * cosR - p->plane_y * sinR;
        p->plane_y = oldPlaneX * sinR + p->plane_y * cosR;
    }
}
