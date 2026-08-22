#include "player.h"
#include "input.h"
#include "door.h"
#include "utils.h"
#include <math.h>

/* M_PI is not standard C11 and the build asks for -std=c11 with extensions
 * off, so it is not declared by math.h here. */
#define PLAYER_PI 3.14159265358979323846f

/* Strafing used to follow the camera plane vector directly, and that vector is
 * FOV_PLANE long rather than 1 — so sideways movement has always been that
 * much slower than forward. Almost certainly unintended, but preserved here to
 * keep this refactor free of gameplay changes. */
#define STRAFE_SPEED (MOVE_SPEED * FOV_PLANE)

void player_init(PlayerState *p, int start_x, int start_y)
{
    p->id = 0;
    p->x = start_x + 0.5f;
    p->y = start_y + 0.5f;
    p->angle = PLAYER_PI; /* facing -X */
    p->hp = 100.0f;
    p->armor = 0.0f;
    p->alive = 1;
    p->respawn_timer = 0.0f;
}

/* Fold an angle back into [-PI, PI]. Without this the angle drifts over a
 * long session until float precision degrades, and shortest-arc
 * interpolation between snapshots takes the long way round. */
static float wrap_angle(float a)
{
    while (a > PLAYER_PI) {
        a -= 2.0f * PLAYER_PI;
    }
    while (a < -PLAYER_PI) {
        a += 2.0f * PLAYER_PI;
    }
    return a;
}

static int blocked(const Map *m, DoorList *dl, float x, float y)
{
    return map_is_wall_door(m, dl, x, y);
}

static void try_move(PlayerState *p, const Map *m, DoorList *dl, float nx, float ny)
{
    float r = PLAYER_RADIUS;
    if (!blocked(m, dl, nx + (nx > p->x ? r : -r), p->y)) {
        p->x = nx;
    }
    if (!blocked(m, dl, p->x, ny + (ny > p->y ? r : -r))) {
        p->y = ny;
    }
}

void player_update(PlayerState *p, const Map *m, DoorList *dl, const InputState *in, double dt)
{
    float ca = cosf(p->angle);
    float sa = sinf(p->angle);
    float fwd = MOVE_SPEED * (float)dt;
    float side = STRAFE_SPEED * (float)dt;

    if (in->forward) {
        try_move(p, m, dl, p->x + ca * fwd, p->y + sa * fwd);
    }
    if (in->back) {
        try_move(p, m, dl, p->x - ca * fwd, p->y - sa * fwd);
    }
    if (in->strafe_left) {
        try_move(p, m, dl, p->x - sa * side, p->y + ca * side);
    }
    if (in->strafe_right) {
        try_move(p, m, dl, p->x + sa * side, p->y - ca * side);
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
        p->angle = wrap_angle(p->angle + rot);
    }
}
