#include "weapon.h"
#include "raycast.h"
#include "utils.h"
#include <math.h>
#include <string.h>

void weapon_system_init(WeaponSystem *ws) {
    memset(ws, 0, sizeof(*ws));
    ws->weapons[WEAPON_PISTOL] = (Weapon){
        .type = WEAPON_PISTOL, .ammo = 50, .max_ammo = 50,
        .damage = 15.0f, .pellets = 1, .spread = 0.0f,
        .fire_cd = 0.45f, .cooldown = 0.0f, .anim = 0.0f,
    };
    ws->weapons[WEAPON_SHOTGUN] = (Weapon){
        .type = WEAPON_SHOTGUN, .ammo = 20, .max_ammo = 20,
        .damage = 10.0f, .pellets = 7, .spread = 0.12f,
        .fire_cd = 0.85f, .cooldown = 0.0f, .anim = 0.0f,
    };
    ws->current = WEAPON_PISTOL;
}

void weapon_switch(WeaponSystem *ws, int idx) {
    if (idx < 0 || idx >= WEAPON_COUNT) return;
    if (idx == ws->current) return;
    ws->current = idx;
}

void weapon_update(WeaponSystem *ws, double dt) {
    for (int i = 0; i < WEAPON_COUNT; i++) {
        if (ws->weapons[i].cooldown > 0.0f)
            ws->weapons[i].cooldown -= (float)dt;
        if (ws->weapons[i].anim > 0.0f) {
            ws->weapons[i].anim -= (float)dt * 4.0f;
            if (ws->weapons[i].anim < 0.0f) ws->weapons[i].anim = 0.0f;
        }
    }
}

/* For each enemy, compute its screen-space column band (center x + half width)
 * at the player's current view, plus perpWallDist. Returns 1 if computed.
 * The "ray screen x" is the column where the fire ray would be drawn. */
static int enemy_screen_band(const Player *p, const Enemy *e, int *cx_out, int *halfw_out, float *depth_out) {
    float dx = e->x - p->x;
    float dy = e->y - p->y;
    float det = p->planeX * p->dirY - p->planeY * p->dirX;
    if (fabsf(det) < 1e-6f) return 0;
    float transformX = (p->dirY * dx - p->dirX * dy) / det;
    float transformY = (-p->planeY * dx + p->planeX * dy) / det;
    if (transformY <= 0.1f) return 0; /* behind camera */
    int screenX = (int)((SCREEN_W / 2.0f) * (1.0f + transformX / transformY));
    int spriteHeight = (int)fabsf((float)SCREEN_H / transformY);
    int halfW = spriteHeight / 2; /* assume square sprite width */
    *cx_out = screenX;
    *halfw_out = halfW;
    *depth_out = transformY;
    return 1;
}

void weapon_try_fire(WeaponSystem *ws, const Player *p, EnemyList *el, SpriteList *sl) {
    Weapon *w = &ws->weapons[ws->current];
    if (w->cooldown > 0.0f) return;
    if (w->ammo <= 0) return;

    w->ammo--;
    w->cooldown = w->fire_cd;
    w->anim = 1.0f;

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
        float rayDirX = p->dirX * cosA - p->dirY * sinA;
        float rayDirY = p->dirX * sinA + p->dirY * cosA;
        /* compute screen column for this ray (cameraX derived from dir/plane) */
        float det = p->planeX * p->dirY - p->planeY * p->dirX;
        if (fabsf(det) < 1e-6f) continue;
        /* cameraX satisfies: rayDir = dir + plane*cameraX => solve for cameraX via dot with plane-perp */
        /* We'll instead reuse the standard formula using transform: */
        float perpDist;
        float camX;
        /* camX such that dirX + planeX*camX = rayDirX and dirY + planeY*camX = rayDirY */
        /* Solve via least squares using plane (camX = (ray . plane) / (plane . plane)) */
        float pp = p->planeX * p->planeX + p->planeY * p->planeY;
        camX = ((rayDirX - p->dirX) * p->planeX + (rayDirY - p->dirY) * p->planeY) / pp;
        int screenX = (int)((float)SCREEN_W * (1.0f + camX) * 0.5f);
        (void)perpDist;
        (void)det;
        if (screenX < 0) screenX = 0;
        if (screenX >= SCREEN_W) screenX = SCREEN_W - 1;

        /* wall distance at this screen column (zBuffer) */
        float wallDist = zBuffer[screenX];

        /* find nearest enemy whose band contains screenX and depth < wallDist */
        int best = -1;
        float bestDepth = 1e30f;
        for (int i = 0; i < el->count; i++) {
            Enemy *e = &el->items[i];
            if (e->state == ESTATE_DEAD) continue;
            int cx, halfw;
            float depth;
            if (!enemy_screen_band(p, e, &cx, &halfw, &depth)) continue;
            if (depth >= wallDist) continue; /* occluded by wall */
            if (screenX < cx - halfw || screenX > cx + halfw) continue;
            if (depth < bestDepth) {
                bestDepth = depth;
                best = i;
            }
        }
        if (best >= 0) {
            enemy_damage(el, sl, best, w->damage);
        }
    }
}