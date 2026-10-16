#include "player.h"
#include "input.h"
#include "door.h"
#include "utils.h"
#include <math.h>

/* Strafing used to follow the camera plane vector directly, and that vector is
 * FOV_PLANE long rather than 1 — so sideways movement has always been that
 * much slower than forward. Almost certainly unintended, but preserved here to
 * keep this refactor free of gameplay changes. */
#define STRAFE_SPEED (MOVE_SPEED * FOV_PLANE)

void player_init(PlayerState *p, int start_x, int start_y)
{
    p->id = 0;
    p->active = 1;
    p->x = start_x + 0.5f;
    p->y = start_y + 0.5f;
    p->angle = MATH_PI; /* facing -X */
    p->hp = 100.0f;
    p->armor = 0.0f;
    p->alive = 1;
    p->respawn_timer = 0.0f;
    p->last_attacker = -1;
    weapon_system_init(&p->weapons);
}

void player_damage(PlayerState *victim, float dmg, int attacker_id)
{
    if (victim->armor > 0.0f) {
        float absorbed = dmg * 0.5f;
        if (absorbed > victim->armor) {
            absorbed = victim->armor;
        }
        victim->armor -= absorbed;
        dmg -= absorbed;
    }
    victim->hp -= dmg;
    if (victim->hp < 0.0f) {
        victim->hp = 0.0f;
    }
    victim->last_attacker = (int8_t)attacker_id;
}

static int blocked(const Map *m, DoorList *dl, float x, float y)
{
    return map_is_wall_door(m, dl, x, y);
}

/* Bodies are cylinders of PLAYER_RADIUS, so two of them touch at twice that
 * and (nx,ny) is refused while it would put p any closer.
 *
 * Refused only while it does not increase the distance, because two players
 * can start out overlapping — spawned on the same point, or dropped into an
 * occupied slot — and a flat refusal would then freeze both of them where
 * they stand instead of letting them walk apart. */
static int player_blocked(const PlayerState *p, const PlayerState *players, int player_count,
                          float nx, float ny)
{
    const float touch = 2.0f * PLAYER_RADIUS;
    for (int i = 0; i < player_count; i++) {
        const PlayerState *o = &players[i];
        if (o == p || !o->active || !o->alive) {
            continue;
        }
        float ndx = nx - o->x, ndy = ny - o->y;
        float nd2 = ndx * ndx + ndy * ndy;
        if (nd2 >= touch * touch) {
            continue;
        }
        float odx = p->x - o->x, ody = p->y - o->y;
        if (nd2 <= odx * odx + ody * ody) {
            return 1;
        }
    }
    return 0;
}

static void try_move(PlayerState *p, const Map *m, DoorList *dl, const PlayerState *players,
                     int player_count, float nx, float ny)
{
    float r = PLAYER_RADIUS;
    if (!blocked(m, dl, nx + (nx > p->x ? r : -r), p->y) &&
        !player_blocked(p, players, player_count, nx, p->y)) {
        p->x = nx;
    }
    if (!blocked(m, dl, p->x, ny + (ny > p->y ? r : -r)) &&
        !player_blocked(p, players, player_count, p->x, ny)) {
        p->y = ny;
    }
}

void player_update(PlayerState *p, const Map *m, DoorList *dl, const PlayerInput *in,
                   const PlayerState *players, int player_count, double dt)
{
    p->angle = wrap_angle(in->angle);
    float ca = cosf(p->angle);
    float sa = sinf(p->angle);
    float fwd = MOVE_SPEED * (float)dt;
    float side = STRAFE_SPEED * (float)dt;

    if (in->buttons & BTN_FORWARD) {
        try_move(p, m, dl, players, player_count, p->x + ca * fwd, p->y + sa * fwd);
    }
    if (in->buttons & BTN_BACK) {
        try_move(p, m, dl, players, player_count, p->x - ca * fwd, p->y - sa * fwd);
    }
    if (in->buttons & BTN_LEFT) {
        try_move(p, m, dl, players, player_count, p->x - sa * side, p->y + ca * side);
    }
    if (in->buttons & BTN_RIGHT) {
        try_move(p, m, dl, players, player_count, p->x + sa * side, p->y - ca * side);
    }
}
