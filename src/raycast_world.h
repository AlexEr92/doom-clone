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

/* Hit radii for the entity trace. Entities are cylinders: a hitscan has no
 * vertical component, so a radius is the whole of their shape. */
#define ENEMY_HIT_RADIUS 0.30f
#define PLAYER_HIT_RADIUS 0.20f /* = PLAYER_RADIUS */

struct EnemyList;
struct PlayerState;

/* Level geometry plus the live entities standing in it, nearest hit wins.
 * This is what a hitscan weapon fires: the answer comes out of the world, so
 * it is the same on a headless server as on a client, and does not depend on
 * a frame having been rendered.
 *
 * (dx,dy) must be a unit vector here, so `dist` and `maxdist` are true
 * distances rather than multiples of it. `el` may be NULL and `player_count`
 * 0 for a world without either. `ignore_player` is an index into `players`
 * that the trace skips: the shooter, so a shot cannot start inside its own
 * hitbox. Pass -1 to skip nobody. */
RayHit world_raycast(const Map *m, const struct DoorList *dl, const struct EnemyList *el,
                     const struct PlayerState *players, int player_count, float ox, float oy,
                     float dx, float dy, float maxdist, int ignore_player);

/* Is there an unobstructed straight line between the two points? */
int world_line_of_sight(const Map *m, const struct DoorList *dl, float x0, float y0, float x1,
                        float y1);

#endif
