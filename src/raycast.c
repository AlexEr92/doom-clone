#include "raycast.h"
#include "door.h"
#include "utils.h"
#include <math.h>

float zBuffer[SCREEN_W];

static inline uint32_t sample_nearest(const Texture *t, int u, int v) {
    if (u < 0) u = 0;
    if (v < 0) v = 0;
    if (u >= t->w) u = t->w - 1;
    if (v >= t->h) v = t->h - 1;
    return t->pixels[(size_t)v * t->w + u];
}

static const Texture *wall_texture_for(const Assets *a, int cell) {
    return cell == 2 ? &a->wall_door : &a->wall_brick;
}

void raycast_render(Framebuffer *fb, const Player *p, const Map *m,
                    const Assets *a, DoorList *dl) {
    /* Flat ceiling/floor fills */
    uint32_t ceil_color = make_color(40, 40, 50);
    uint32_t floor_color = make_color(70, 60, 55);
    for (int y = 0; y < SCREEN_H / 2; y++) {
        uint32_t *row = fb->pixels + (size_t)y * SCREEN_W;
        for (int x = 0; x < SCREEN_W; x++) row[x] = ceil_color;
    }
    for (int y = SCREEN_H / 2; y < SCREEN_H; y++) {
        uint32_t *row = fb->pixels + (size_t)y * SCREEN_W;
        for (int x = 0; x < SCREEN_W; x++) row[x] = floor_color;
    }

    for (int x = 0; x < SCREEN_W; x++) {
        float cameraX = 2.0f * (float)x / (float)SCREEN_W - 1.0f;
        float rayDirX = p->dirX + p->planeX * cameraX;
        float rayDirY = p->dirY + p->planeY * cameraX;

        int mapX = (int)p->x;
        int mapY = (int)p->y;

        float deltaDistX = rayDirX == 0.0f ? 1e30f : fabsf(1.0f / rayDirX);
        float deltaDistY = rayDirY == 0.0f ? 1e30f : fabsf(1.0f / rayDirY);

        int stepX, stepY;
        float sideDistX, sideDistY;

        if (rayDirX < 0) {
            stepX = -1;
            sideDistX = (p->x - mapX) * deltaDistX;
        } else {
            stepX = 1;
            sideDistX = (mapX + 1.0f - p->x) * deltaDistX;
        }
        if (rayDirY < 0) {
            stepY = -1;
            sideDistY = (p->y - mapY) * deltaDistY;
        } else {
            stepY = 1;
            sideDistY = (mapY + 1.0f - p->y) * deltaDistY;
        }

        int hit = 0;
        int side = 0;
        int cell = 0;
        float door_openness = 0.0f;
        int guard = 0;
        while (!hit && guard++ < 64) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }
            cell = map_cell(m, mapX, mapY);
            if (cell > 0) {
                if (cell == 2 && dl) {
                    /* door: if not blocking, pass through */
                    int did = door_at(dl, mapX, mapY);
                    if (did >= 0) {
                        door_openness = dl->doors[did].openness;
                        if (!door_is_blocking(dl, mapX, mapY)) {
                            continue; /* open door: ray continues */
                        }
                    }
                }
                hit = 1;
            }
        }

        float perpWallDist;
        if (side == 0) perpWallDist = sideDistX - deltaDistX;
        else           perpWallDist = sideDistY - deltaDistY;
        if (perpWallDist < 0.0001f) perpWallDist = 0.0001f;

        zBuffer[x] = perpWallDist;

        int lineHeight = (int)((float)SCREEN_H / perpWallDist);
        int drawStart = -lineHeight / 2 + SCREEN_H / 2;
        int drawEnd   =  lineHeight / 2 + SCREEN_H / 2;

        /* texture coordinate */
        float wallX;
        if (side == 0) wallX = p->y + perpWallDist * rayDirY;
        else           wallX = p->x + perpWallDist * rayDirX;
        wallX -= floorf(wallX);

        const Texture *tex = wall_texture_for(a, hit ? cell : 1);
        int texW = tex->w;
        int texH = tex->h;
        int texX = (int)(wallX * (float)texW);
        if (side == 0 && rayDirX > 0) texX = texW - texX - 1;
        if (side == 1 && rayDirY < 0) texX = texW - texX - 1;

        /* Door sliding effect: a closing/opening door is drawn shifted upward
         * by openness * lineHeight, revealing the floor/ceiling gap below.
         * Only applies when we actually hit a door cell. */
        int door_shift = 0;
        if (hit && cell == 2 && dl) {
            door_shift = (int)(door_openness * (float)lineHeight);
        }

        int drawStartClamped = drawStart + door_shift;
        int drawEndClamped = drawEnd;
        if (drawStartClamped < 0) drawStartClamped = 0;
        if (drawEndClamped >= SCREEN_H) drawEndClamped = SCREEN_H - 1;

        float shade = (side == 1) ? 0.7f : 1.0f;
        float step = (float)texH / (float)lineHeight;
        float texPos = ((float)drawStartClamped - (float)SCREEN_H / 2.0f
                        + (float)lineHeight / 2.0f - (float)door_shift) * step;

        uint32_t *col_ptr = fb->pixels + (size_t)drawStartClamped * SCREEN_W + x;
        for (int y = drawStartClamped; y <= drawEndClamped; y++) {
            int texY = (int)texPos;
            if (texY < 0) texY = 0;
            if (texY >= texH) texY = texH - 1;
            texPos += step;
            uint32_t c = sample_nearest(tex, texX, texY);
            if (shade < 1.0f) c = shade_color(c, shade);
            *col_ptr = c;
            col_ptr += SCREEN_W;
        }
    }
}