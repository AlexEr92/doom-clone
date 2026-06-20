#include "engine.h"
#include "map.h"
#include "player.h"
#include "raycast.h"
#include "input.h"
#include "assets.h"
#include "sprite.h"
#include "utils.h"
#include <SDL.h>
#include <stdio.h>
#include <math.h>

static void hud_draw_weapon(Framebuffer *fb, const Texture *t) {
    if (!t || !t->pixels) return;
    int tw = t->w;
    int th = t->h;
    /* Scale so weapon fits ~40% of screen height, anchored bottom-center */
    int target_h = (int)(SCREEN_H * 0.40f);
    float scale = (float)target_h / (float)th;
    int draw_w = (int)(tw * scale);
    int draw_h = target_h;
    int x0 = SCREEN_W / 2 - draw_w / 2;
    int y0 = SCREEN_H - draw_h;
    if (x0 < 0) x0 = 0;

    for (int y = 0; y < draw_h; y++) {
        int sy = (int)((float)y / scale);
        if (sy < 0 || sy >= th) continue;
        int py = y0 + y;
        if (py < 0 || py >= SCREEN_H) continue;
        for (int x = 0; x < draw_w; x++) {
            int sx = (int)((float)x / scale);
            if (sx < 0 || sx >= tw) continue;
            int px = x0 + x;
            if (px < 0 || px >= SCREEN_W) continue;
            uint32_t c = t->pixels[(size_t)sy * tw + sx];
            if ((c & 0xFF000000u) == 0) continue;
            fb->pixels[(size_t)py * SCREEN_W + px] = c;
        }
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    const char *map_path = "assets/maps/level1.txt";

    Engine eng;
    if (engine_init(&eng) != 0) {
        fprintf(stderr, "Engine init failed\n");
        return 1;
    }

    Assets assets;
    if (assets_init(&assets) != 0) {
        fprintf(stderr, "Assets init failed\n");
        engine_shutdown(&eng);
        return 1;
    }

    Map map;
    if (map_load(&map, map_path) != 0) {
        fprintf(stderr, "Map load failed: %s\n", map_path);
        assets_shutdown(&assets);
        engine_shutdown(&eng);
        return 1;
    }

    Player player;
    player_init(&player, map.start_x, map.start_y);

    SpriteList sprites;
    sprite_init(&sprites);
    for (int i = 0; i < map.sprite_count; i++) {
        int type = SPRITE_BARREL;
        switch (map.sprites[i].marker) {
            case 'E': type = SPRITE_ENEMY;  break;
            case 'B': type = SPRITE_BARREL; break;
            case 'M': type = SPRITE_MEDKIT; break;
            case 'A': type = SPRITE_AMMO;   break;
            case 'R': type = SPRITE_ARMOR; break;
        }
        sprite_add(&sprites, map.sprites[i].x, map.sprites[i].y, type);
    }

    InputState input;
    input_init(&input);

    double prev = get_time_seconds();
    double accumulator = 0.0;
    double fps_timer = 0.0;
    int frames = 0;
    char title[64];

    while (eng.running) {
        double now = get_time_seconds();
        double frame_time = now - prev;
        prev = now;
        if (frame_time > 0.25) frame_time = 0.25;

        fps_timer += frame_time;
        frames++;
        if (fps_timer >= 0.5) {
            eng.fps = (double)frames / fps_timer;
            snprintf(title, sizeof(title), "doom-clone | %.0f FPS", eng.fps);
            SDL_SetWindowTitle(eng.window, title);
            fps_timer = 0.0;
            frames = 0;
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            input_handle_event(&input, &ev, &eng.running);
        }

        accumulator += frame_time;
        while (accumulator >= FIXED_DT) {
            player_update(&player, &map, &input, FIXED_DT);
            input_end_frame(&input);
            accumulator -= FIXED_DT;
        }

        raycast_render(&eng.fb, &player, &map, &assets);
        sprite_render(&eng.fb, &sprites, &player, &assets);
        hud_draw_weapon(&eng.fb, &assets.weapon_pistol);
        engine_present(&eng);
    }

    assets_shutdown(&assets);
    engine_shutdown(&eng);
    return 0;
}