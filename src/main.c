#include "engine.h"
#include "map.h"
#include "player.h"
#include "raycast.h"
#include "input.h"
#include "assets.h"
#include "sprite.h"
#include "enemy.h"
#include "weapon.h"
#include "utils.h"
#include <SDL.h>
#include <stdio.h>
#include <math.h>

static void hud_draw_weapon(Framebuffer *fb, const Texture *base, float anim) {
    if (!base || !base->pixels) return;
    int tw = base->w;
    int th = base->h;
    int target_h = (int)(SCREEN_H * 0.40f);
    float scale = (float)target_h / (float)th;
    int draw_w = (int)(tw * scale);
    int draw_h = target_h;
    int x0 = SCREEN_W / 2 - draw_w / 2;
    int y0 = SCREEN_H - draw_h;
    /* recoil: shift down by up to 18px based on anim (1.0 -> 18, 0 -> 0) */
    int recoil = (int)(18.0f * anim);
    y0 += recoil;
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
            uint32_t c = base->pixels[(size_t)sy * tw + sx];
            if ((c & 0xFF000000u) == 0) continue;
            fb->pixels[(size_t)py * SCREEN_W + px] = c;
        }
    }
}

/* Simple HUD: HP bar (top-left) and ammo/weapon text-like indicator (bottom-right).
 * We can't draw text without a font, so use colored bars + a small numeric
 * representation via block digits. For Week 3 we just show HP bar and ammo bar. */
static void hud_draw(Framebuffer *fb, const Player *pl, const WeaponSystem *ws) {
    /* HP bar */
    int bar_w = 160, bar_h = 10;
    int bx = 8, by = 8;
    uint32_t bg = make_color(20, 20, 20);
    uint32_t hp_col = make_color(200, 40, 40);
    for (int y = by; y < by + bar_h; y++)
        for (int x = bx; x < bx + bar_w; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = bg;
    int hpw = (int)((float)bar_w * clampf(pl->hp / 100.0f, 0.0f, 1.0f));
    for (int y = by; y < by + bar_h; y++)
        for (int x = bx; x < bx + hpw; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = hp_col;

    /* Armor bar (yellow) below HP */
    uint32_t ar_col = make_color(220, 200, 60);
    int ar_by = by + bar_h + 2;
    for (int y = ar_by; y < ar_by + bar_h; y++)
        for (int x = bx; x < bx + bar_w; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = bg;
    int arw = (int)((float)bar_w * clampf(pl->armor / 100.0f, 0.0f, 1.0f));
    for (int y = ar_by; y < ar_by + bar_h; y++)
        for (int x = bx; x < bx + arw; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = ar_col;

    /* Ammo bar (bottom-right) — width encodes ammo fraction */
    const Weapon *w = &ws->weapons[ws->current];
    int abx = SCREEN_W - 8 - bar_w;
    int aby = SCREEN_H - 8 - bar_h;
    uint32_t am_col = ws->current == WEAPON_PISTOL ? make_color(220, 220, 60)
                                                   : make_color(220, 140, 40);
    for (int y = aby; y < aby + bar_h; y++)
        for (int x = abx; x < abx + bar_w; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = bg;
    int amw = (int)((float)bar_w * clampf((float)w->ammo / (float)w->max_ammo, 0.0f, 1.0f));
    for (int y = aby; y < aby + bar_h; y++)
        for (int x = abx; x < abx + amw; x++)
            fb->pixels[(size_t)y * SCREEN_W + x] = am_col;
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
    EnemyList enemies;
    enemy_list_init(&enemies);

    for (int i = 0; i < map.sprite_count; i++) {
        float sx = map.sprites[i].x;
        float sy = map.sprites[i].y;
        switch (map.sprites[i].marker) {
            case 'E': enemy_spawn(&enemies, &sprites, sx, sy, ENEMY_IMP); break;
            case 'S': enemy_spawn(&enemies, &sprites, sx, sy, ENEMY_SERG); break;
            case 'B': sprite_add(&sprites, sx, sy, SPRITE_BARREL); break;
            case 'M': sprite_add(&sprites, sx, sy, SPRITE_MEDKIT); break;
            case 'A': sprite_add(&sprites, sx, sy, SPRITE_AMMO); break;
            case 'R': sprite_add(&sprites, sx, sy, SPRITE_ARMOR); break;
        }
    }

    WeaponSystem ws;
    weapon_system_init(&ws);

    InputState input;
    input_init(&input);

    double prev = get_time_seconds();
    double accumulator = 0.0;
    double fps_timer = 0.0;
    int frames = 0;
    char title[128];

    while (eng.running) {
        double now = get_time_seconds();
        double frame_time = now - prev;
        prev = now;
        if (frame_time > 0.25) frame_time = 0.25;

        fps_timer += frame_time;
        frames++;
        if (fps_timer >= 0.5) {
            eng.fps = (double)frames / fps_timer;
            snprintf(title, sizeof(title), "doom-clone | %.0f FPS | HP %.0f | ammo %d/%d",
                     eng.fps, player.hp, ws.weapons[ws.current].ammo,
                     ws.weapons[ws.current].max_ammo);
            SDL_SetWindowTitle(eng.window, title);
            fps_timer = 0.0;
            frames = 0;
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            input_handle_event(&input, &ev, &eng.running);
        }

        /* weapon switching (edge) */
        if (input.switch1) weapon_switch(&ws, WEAPON_PISTOL);
        if (input.switch2) weapon_switch(&ws, WEAPON_SHOTGUN);

        accumulator += frame_time;
        while (accumulator >= FIXED_DT) {
            player_update(&player, &map, &input, FIXED_DT);
            enemy_update_all(&enemies, &sprites, &map, &player, FIXED_DT);
            weapon_update(&ws, FIXED_DT);
            /* fire: held fire triggers continuous fire subject to cooldown */
            if (input.fire) {
                weapon_try_fire(&ws, &player, &enemies, &sprites);
            }
            input_end_frame(&input);
            accumulator -= FIXED_DT;
        }

        if (player.hp <= 0.0f) {
            /* Player is dead: stop, but keep rendering a final frame. */
            eng.running = 0;
        }

        raycast_render(&eng.fb, &player, &map, &assets);
        sprite_render(&eng.fb, &sprites, &player, &assets);
        hud_draw_weapon(&eng.fb,
                        ws.current == WEAPON_PISTOL ? &assets.weapon_pistol : &assets.weapon_shotgun,
                        ws.weapons[ws.current].anim);
        hud_draw(&eng.fb, &player, &ws);
        engine_present(&eng);
    }

    if (player.hp <= 0.0f) {
        printf("You died.\n");
    } else if (enemy_all_dead(&enemies)) {
        printf("All enemies eliminated.\n");
    }

    assets_shutdown(&assets);
    engine_shutdown(&eng);
    return 0;
}