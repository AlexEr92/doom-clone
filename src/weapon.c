#include "weapon.h"
#include "player.h"
#include "enemy.h"
#include "sprite.h"
#include "camera.h"
#include "raycast.h"
#include "audio.h"
#include "utils.h"
#include <math.h>
#include <string.h>

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

/* For each enemy, compute its screen-space column band (center x + half width)
 * at the player's current view, plus perpWallDist. Returns 1 if computed.
 * The "ray screen x" is the column where the fire ray would be drawn. */
static int enemy_screen_band(const PlayerState *p, const Camera *cam, const Enemy *e, int *cx_out,
                             int *halfw_out, float *depth_out)
{
    float dx = e->x - p->x;
    float dy = e->y - p->y;
    float det = cam->plane_x * cam->dir_y - cam->plane_y * cam->dir_x;
    if (fabsf(det) < 1e-6f) {
        return 0;
    }
    float transformX = (cam->dir_y * dx - cam->dir_x * dy) / det;
    float transformY = (-cam->plane_y * dx + cam->plane_x * dy) / det;
    if (transformY <= 0.1f) {
        return 0; /* behind camera */
    }
    int screenX = (int)((SCREEN_W / 2.0f) * (1.0f + transformX / transformY));
    int spriteHeight = (int)fabsf((float)SCREEN_H / transformY);
    int halfW = spriteHeight / 2; /* assume square sprite width */
    *cx_out = screenX;
    *halfw_out = halfW;
    *depth_out = transformY;
    return 1;
}

void weapon_try_fire(PlayerState *p, EnemyList *el, SpriteList *sl, Audio *au)
{
    /* Hitscan still works in screen space, so it needs the view basis even
     * though firing is simulation. Rebuilt here rather than taken as an
     * argument: task 05-04 replaces this whole body with a world raycast. */
    Camera cam;
    player_camera(p, &cam);

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

    /* For each pellet, pick a random spread angle around view direction,
     * compute its screen column, find nearest enemy whose band contains it
     * and is in front of the wall (zBuffer). Apply damage to that enemy. */
    for (int pellet = 0; pellet < w->pellets; pellet++) {
        float angle = 0.0f;
        if (w->spread > 0.0f) {
            float r = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
            angle = r * w->spread;
        }
        /* rotate direction by angle to get ray direction */
        float cosA = cosf(angle), sinA = sinf(angle);
        float rayDirX = cam.dir_x * cosA - cam.dir_y * sinA;
        float rayDirY = cam.dir_x * sinA + cam.dir_y * cosA;
        /* compute screen column for this ray (cameraX derived from dir/plane) */
        float det = cam.plane_x * cam.dir_y - cam.plane_y * cam.dir_x;
        if (fabsf(det) < 1e-6f) {
            continue;
        }
        /* cameraX satisfies: rayDir = dir + plane*cameraX => solve for cameraX via dot with
         * plane-perp */
        /* We'll instead reuse the standard formula using transform: */
        float perpDist;
        float camX;
        /* camX such that dir_x + plane_x*camX = rayDirX and dir_y + plane_y*camX = rayDirY */
        /* Solve via least squares using plane (camX = (ray . plane) / (plane . plane)) */
        float pp = cam.plane_x * cam.plane_x + cam.plane_y * cam.plane_y;
        camX = ((rayDirX - cam.dir_x) * cam.plane_x + (rayDirY - cam.dir_y) * cam.plane_y) / pp;
        int screenX = (int)((float)SCREEN_W * (1.0f + camX) * 0.5f);
        (void)perpDist;
        (void)det;
        if (screenX < 0) {
            screenX = 0;
        }
        if (screenX >= SCREEN_W) {
            screenX = SCREEN_W - 1;
        }

        /* wall distance at this screen column (zBuffer) */
        float wallDist = zBuffer[screenX];

        /* find nearest enemy whose band contains screenX and depth < wallDist */
        int best = -1;
        float bestDepth = 1e30f;
        for (int i = 0; i < el->count; i++) {
            Enemy *e = &el->items[i];
            if (e->state == ESTATE_DEAD) {
                continue;
            }
            int cx, halfw;
            float depth;
            if (!enemy_screen_band(p, &cam, e, &cx, &halfw, &depth)) {
                continue;
            }
            if (depth >= wallDist) {
                continue; /* occluded by wall */
            }
            if (screenX < cx - halfw || screenX > cx + halfw) {
                continue;
            }
            if (depth < bestDepth) {
                bestDepth = depth;
                best = i;
            }
        }
        if (best >= 0) {
            enemy_damage(el, sl, best, w->damage, au, p->x, p->y);
        }
    }
}
