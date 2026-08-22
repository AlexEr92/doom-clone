#include "raycast_world.h"
#include "door.h"
#include "enemy.h"
#include "player.h"
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

/* Distance from the ray origin to the near side of a circle of radius r at
 * (cx,cy), or -1 if the ray misses it or leaves it behind. A ray starting
 * inside the circle hits at 0: point blank still counts. */
static float ray_circle(float ox, float oy, float dx, float dy, float cx, float cy, float r)
{
    float fx = ox - cx;
    float fy = oy - cy;
    float b = fx * dx + fy * dy;
    float c = fx * fx + fy * fy - r * r;
    float disc = b * b - c;
    if (disc < 0.0f) {
        return -1.0f;
    }
    float sq = sqrtf(disc);
    float t_near = -b - sq;
    if (t_near >= 0.0f) {
        return t_near;
    }
    if (-b + sq < 0.0f) {
        return -1.0f; /* the whole circle is behind the origin */
    }
    return 0.0f;
}

RayHit world_raycast(const Map *m, const DoorList *dl, const EnemyList *el,
                     const PlayerState *players, int player_count, float ox, float oy, float dx,
                     float dy, float maxdist, int ignore_player)
{
    RayHit h = raycast_walls(m, dl, ox, oy, dx, dy, maxdist);
    /* Everything past the wall is out of reach, and so is everything past
     * maxdist when the ray ran out without finding one. Each entity found
     * closer pulls the limit in, which leaves the nearest one. */
    float limit = (h.kind == HIT_WALL) ? h.dist : maxdist;

    if (el) {
        for (int i = 0; i < el->count; i++) {
            const Enemy *e = &el->items[i];
            if (e->state == ESTATE_DEAD) {
                continue;
            }
            float t = ray_circle(ox, oy, dx, dy, e->x, e->y, ENEMY_HIT_RADIUS);
            if (t < 0.0f || t >= limit) {
                continue;
            }
            limit = t;
            h.kind = HIT_ENEMY;
            h.id = i;
        }
    }
    if (players) {
        for (int i = 0; i < player_count; i++) {
            const PlayerState *pl = &players[i];
            if (i == ignore_player || !pl->alive) {
                continue;
            }
            float t = ray_circle(ox, oy, dx, dy, pl->x, pl->y, PLAYER_HIT_RADIUS);
            if (t < 0.0f || t >= limit) {
                continue;
            }
            limit = t;
            h.kind = HIT_PLAYER;
            h.id = i;
        }
    }

    if (h.kind == HIT_ENEMY || h.kind == HIT_PLAYER) {
        h.dist = limit;
        h.x = ox + dx * limit;
        h.y = oy + dy * limit;
        h.side = 0;
        h.map_x = (int)h.x;
        h.map_y = (int)h.y;
    }
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
