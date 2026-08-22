#include "raycast_world.h"
#include "door.h"
#include <math.h>

/* Cells a single trace may cross before giving up. The map is 24x24, so this
 * is far beyond any line that stays inside it. */
#define RAY_MAX_STEPS 64

RayHit raycast_walls(const Map *m, const DoorList *dl, float ox, float oy, float dx, float dy,
                     float maxdist)
{
    RayHit h;
    h.kind = HIT_NONE;
    h.id = -1;

    int map_x = (int)ox;
    int map_y = (int)oy;

    float delta_x = dx == 0.0f ? 1e30f : fabsf(1.0f / dx);
    float delta_y = dy == 0.0f ? 1e30f : fabsf(1.0f / dy);

    int step_x, step_y;
    float side_dist_x, side_dist_y;

    if (dx < 0) {
        step_x = -1;
        side_dist_x = (ox - map_x) * delta_x;
    } else {
        step_x = 1;
        side_dist_x = (map_x + 1.0f - ox) * delta_x;
    }
    if (dy < 0) {
        step_y = -1;
        side_dist_y = (oy - map_y) * delta_y;
    } else {
        step_y = 1;
        side_dist_y = (map_y + 1.0f - oy) * delta_y;
    }

    int side = 0;
    int steps = 0;
    while (steps++ < RAY_MAX_STEPS) {
        if (side_dist_x < side_dist_y) {
            side_dist_x += delta_x;
            map_x += step_x;
            side = 0;
        } else {
            side_dist_y += delta_y;
            map_y += step_y;
            side = 1;
        }
        float entry = (side == 0) ? side_dist_x - delta_x : side_dist_y - delta_y;
        if (entry > maxdist) {
            break;
        }
        /* Sample the cell centre: the boundary the ray just crossed is not a
         * safe point to classify. An open door is not geometry. */
        if (map_is_wall_door(m, dl, (float)map_x + 0.5f, (float)map_y + 0.5f)) {
            h.kind = HIT_WALL;
            break;
        }
    }

    float dist = (side == 0) ? side_dist_x - delta_x : side_dist_y - delta_y;
    if (dist < 0.0001f) {
        dist = 0.0001f;
    }

    h.dist = dist;
    h.side = side;
    h.map_x = map_x;
    h.map_y = map_y;
    h.x = ox + dx * dist;
    h.y = oy + dy * dist;
    return h;
}

int world_line_of_sight(const Map *m, const DoorList *dl, float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1e-3f) {
        return 1;
    }
    RayHit h = raycast_walls(m, dl, x0, y0, dx / dist, dy / dist, dist);
    return h.kind != HIT_WALL;
}
