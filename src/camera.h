#ifndef CAMERA_H
#define CAMERA_H

#include "player.h"
#include "utils.h"
#include <math.h>

/* View basis derived from PlayerState.angle. A rendering artifact — the plane
 * length is the FOV — so it lives outside the simulation and is built on
 * demand rather than stored and kept in sync. */
typedef struct {
    float dir_x, dir_y;
    float plane_x, plane_y;
} Camera;

/* Build the view basis for p: dir = (cos a, sin a), plane the FOV-scaled
 * perpendicular pointing towards the right edge of the screen. */
static inline void player_camera(const PlayerState *p, Camera *c)
{
    float ca = cosf(p->angle);
    float sa = sinf(p->angle);
    c->dir_x = ca;
    c->dir_y = sa;
    c->plane_x = FOV_PLANE * sa;
    c->plane_y = FOV_PLANE * -ca;
}

#endif
