#ifndef RAYCAST_WORLD_H
#define RAYCAST_WORLD_H

#include "map.h"

struct DoorList;

/* What a ray ran into. Enemy/player hits are not produced by
 * raycast_walls(); they exist for the entity-aware traces built on top. */
typedef enum { HIT_NONE = 0, HIT_WALL, HIT_ENEMY, HIT_PLAYER } HitKind;

typedef struct {
    HitKind kind;
    int id;           /* enemy/player index; unset for a wall */
    float dist;       /* perpendicular distance along the ray */
    float x, y;       /* hit point */
    int side;         /* 0 = X face, 1 = Y face (for rendering) */
    int map_x, map_y; /* cell the ray stopped in */
} RayHit;

/* Level geometry only: walls and closed doors. Nothing here knows about the
 * screen, the framebuffer or SDL.
 *
 * (dx,dy) need not be normalised: `dist` is measured in units of that vector,
 * so a camera ray (dir + plane*cameraX) yields the fish-eye-corrected
 * perpendicular distance directly, and a unit vector yields a true distance.
 * The trace stops at `maxdist` (in the same units) or after RAY_MAX_STEPS
 * cells, whichever comes first; then kind is HIT_NONE. */
RayHit raycast_walls(const Map *m, const struct DoorList *dl, float ox, float oy, float dx,
                     float dy, float maxdist);

/* Is there an unobstructed straight line between the two points? */
int world_line_of_sight(const Map *m, const struct DoorList *dl, float x0, float y0, float x1,
                        float y1);

#endif
