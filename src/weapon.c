#include "weapon.h"
#include "player.h"
#include "enemy.h"
#include "door.h"
#include "raycast_world.h"
#include "event.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* How far a pellet flies. Longer than any straight line that fits in the
 * map, so a shot is stopped by geometry rather than by the cutoff. */
#define WEAPON_RANGE 48.0f

/* Balance by eye, to be tuned on the deathmatch map (task 09-05). Only the
 * pistol is owned at the start, and only it has ammo: the rest comes with
 * the weapon when it is picked up — a third of a full load or so, enough to
 * use it straight away — or with ammo found before it. */
void weapon_system_init(WeaponSystem *ws)
{
    memset(ws, 0, sizeof(*ws));
    ws->weapons[WEAPON_PISTOL] = (Weapon){
            .type = WEAPON_PISTOL,
            .owned = 1,
            .ammo = 50,
            .max_ammo = 50,
            .pickup_ammo = 15,
            .damage = 15.0f,
            .pellets = 1,
            .spread = 0.0f,
            .fire_cd = 0.45f,
    };
    ws->weapons[WEAPON_SHOTGUN] = (Weapon){
            .type = WEAPON_SHOTGUN,
            .max_ammo = 20,
            .pickup_ammo = 8,
            .damage = 10.0f,
            .pellets = 7,
            .spread = 0.12f,
            .fire_cd = 0.85f,
    };
    /* fast and light, and it wanders */
    ws->weapons[WEAPON_AK47] = (Weapon){
            .type = WEAPON_AK47,
            .max_ammo = 120,
            .pickup_ammo = 40,
            .damage = 8.0f,
            .pellets = 1,
            .spread = 0.06f,
            .fire_cd = 0.12f,
    };
    /* slow and heavy, and goes where it is aimed */
    ws->weapons[WEAPON_RIFLE] = (Weapon){
            .type = WEAPON_RIFLE,
            .max_ammo = 15,
            .pickup_ammo = 5,
            .damage = 60.0f,
            .pellets = 1,
            .spread = 0.005f,
            .fire_cd = 1.2f,
    };
    ws->weapons[WEAPON_GRENADE] = (Weapon){
            .type = WEAPON_GRENADE,
            .max_ammo = 8,
            .pickup_ammo = 3,
            .damage = 100.0f,
            .pellets = 1,
            .spread = 0.0f,
            .fire_cd = 1.5f,
    };
    ws->current = WEAPON_PISTOL;
}

void weapon_switch(PlayerState *p, int idx)
{
    if (idx < 0 || idx >= WEAPON_COUNT) {
        return;
    }
    if (!p->weapons.weapons[idx].owned) {
        return;
    }
    if (idx == p->weapons.current) {
        return;
    }
    p->weapons.current = idx;
}

int weapon_give(PlayerState *p, int idx)
{
    if (idx < 0 || idx >= WEAPON_COUNT) {
        return 0;
    }
    Weapon *w = &p->weapons.weapons[idx];
    if (w->owned && w->ammo >= w->max_ammo) {
        return 0;
    }
    w->owned = 1;
    w->ammo += w->pickup_ammo;
    if (w->ammo > w->max_ammo) {
        w->ammo = w->max_ammo;
    }
    return 1;
}

void weapon_update(PlayerState *p, double dt)
{
    for (int i = 0; i < WEAPON_COUNT; i++) {
        Weapon *w = &p->weapons.weapons[i];
        if (w->cooldown > 0.0f) {
            w->cooldown -= (float)dt;
        }
        if (w->anim > 0.0f) {
            w->anim -= (float)dt / WEAPON_SHOT_TIME;
            if (w->anim < 0.0f) {
                w->anim = 0.0f;
            }
        }
    }
}

void weapon_try_fire(PlayerState *p, const Map *m, const DoorList *dl, PlayerState *players,
                     int player_count, EnemyList *el, EventQueue *evq)
{
    WeaponSystem *ws = &p->weapons;
    Weapon *w = &ws->weapons[ws->current];
    if (ws->current == WEAPON_GRENADE) {
        return;
    }
    if (w->cooldown > 0.0f) {
        return;
    }
    if (w->ammo <= 0) {
        event_push(evq, EV_NO_AMMO, p->id, p->x, p->y);
        return;
    }

    w->ammo--;
    w->cooldown = w->fire_cd;
    w->anim = 1.0f;

    event_push(evq, EV_SHOT, p->id, p->x, p->y);

    float dir_x = cosf(p->angle);
    float dir_y = sinf(p->angle);

    for (int pellet = 0; pellet < w->pellets; pellet++) {
        float angle = 0.0f;
        if (w->spread > 0.0f) {
            float r = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
            angle = r * w->spread;
        }
        /* rotate the facing by the spread angle to get this pellet's ray */
        float cosA = cosf(angle), sinA = sinf(angle);
        float ray_x = dir_x * cosA - dir_y * sinA;
        float ray_y = dir_x * sinA + dir_y * cosA;

        RayHit h = world_raycast(m, dl, el, players, player_count, p->x, p->y, ray_x, ray_y,
                                 WEAPON_RANGE, p->id);
        if (h.kind == HIT_ENEMY) {
            enemy_damage(el, h.id, w->damage, evq, p->id, p->x, p->y);
        } else if (h.kind == HIT_PLAYER) {
            player_damage(&players[h.id], w->damage, p->id);
        }
    }
}
