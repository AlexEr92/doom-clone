#include "weapon.h"
#include "player.h"
#include "enemy.h"
#include "sprite.h"
#include "door.h"
#include "raycast_world.h"
#include "audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* How far a pellet flies. Longer than any straight line that fits in the
 * map, so a shot is stopped by geometry rather than by the cutoff. */
#define WEAPON_RANGE 48.0f

void weapon_system_init(WeaponSystem *ws)
{
    memset(ws, 0, sizeof(*ws));
    ws->weapons[WEAPON_PISTOL] = (Weapon){
            .type = WEAPON_PISTOL,
            .ammo = 50,
            .max_ammo = 50,
            .damage = 15.0f,
            .pellets = 1,
            .spread = 0.0f,
            .fire_cd = 0.45f,
            .cooldown = 0.0f,
            .anim = 0.0f,
    };
    ws->weapons[WEAPON_SHOTGUN] = (Weapon){
            .type = WEAPON_SHOTGUN,
            .ammo = 20,
            .max_ammo = 20,
            .damage = 10.0f,
            .pellets = 7,
            .spread = 0.12f,
            .fire_cd = 0.85f,
            .cooldown = 0.0f,
            .anim = 0.0f,
    };
    ws->current = WEAPON_PISTOL;
}

void weapon_switch(PlayerState *p, int idx)
{
    if (idx < 0 || idx >= WEAPON_COUNT) {
        return;
    }
    if (idx == p->weapons.current) {
        return;
    }
    p->weapons.current = idx;
}

void weapon_update(PlayerState *p, double dt)
{
    for (int i = 0; i < WEAPON_COUNT; i++) {
        Weapon *w = &p->weapons.weapons[i];
        if (w->cooldown > 0.0f) {
            w->cooldown -= (float)dt;
        }
        if (w->anim > 0.0f) {
            w->anim -= (float)dt * 4.0f;
            if (w->anim < 0.0f) {
                w->anim = 0.0f;
            }
        }
    }
}

void weapon_try_fire(PlayerState *p, const Map *m, const DoorList *dl, PlayerState *players,
                     int player_count, EnemyList *el, SpriteList *sl, Audio *au)
{
    WeaponSystem *ws = &p->weapons;
    Weapon *w = &ws->weapons[ws->current];
    if (w->cooldown > 0.0f) {
        return;
    }
    if (w->ammo <= 0) {
        if (au) {
            audio_play_volume(au, SND_NO_AMMO, 0.5f);
        }
        return;
    }

    w->ammo--;
    w->cooldown = w->fire_cd;
    w->anim = 1.0f;

    if (au) {
        audio_play_volume(au, ws->current == WEAPON_PISTOL ? SND_PISTOL : SND_SHOTGUN, 0.5f);
    }

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
            enemy_damage(el, sl, h.id, w->damage, au, p->x, p->y);
        } else if (h.kind == HIT_PLAYER) {
            player_damage(&players[h.id], w->damage, p->id);
        }
    }
}
