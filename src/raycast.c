#include "raycast.h"
#include "utils.h"
#include <math.h>

float zBuffer[SCREEN_W];

static const uint32_t wall_colors[3] = {
    0xFF000000u,
    0xFF3C3CB4u,
    0xFFC85050u,
};

static uint32_t color_for_cell(int cell, int side) {
    if (cell <= 0 || cell >= (int)(sizeof(wall_colors) / sizeof(wall_colors[0]))) {
        return make_color(200, 200, 200);
    }
    return side == 1 ? shade_color(wall_colors[cell], 0.7f) : wall_colors[cell];
}

void raycast_render(Framebuffer *fb, const Player *p, const Map *m) {
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
            if (cell > 0) hit = 1;
        }

        float perpWallDist;
        if (side == 0) perpWallDist = (sideDistX - deltaDistX);
        else           perpWallDist = (sideDistY - deltaDistY);
        if (perpWallDist < 0.0001f) perpWallDist = 0.0001f;

        zBuffer[x] = perpWallDist;

        int lineHeight = (int)((float)SCREEN_H / perpWallDist);
        int drawStart = -lineHeight / 2 + SCREEN_H / 2;
        int drawEnd =   lineHeight / 2 + SCREEN_H / 2;

        uint32_t c = color_for_cell(hit ? cell : 1, side);
        fb_vline(fb, x, drawStart, drawEnd, c);
    }
}