#include "engine.h"
#include "map.h"
#include "player.h"
#include "raycast.h"
#include "input.h"
#include "utils.h"
#include <SDL.h>
#include <stdio.h>
#include <math.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    const char *map_path = "assets/maps/level1.txt";

    Engine eng;
    if (engine_init(&eng) != 0) {
        fprintf(stderr, "Engine init failed\n");
        return 1;
    }

    Map map;
    if (map_load(&map, map_path) != 0) {
        fprintf(stderr, "Map load failed: %s\n", map_path);
        engine_shutdown(&eng);
        return 1;
    }

    Player player;
    player_init(&player, map.start_x, map.start_y);

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

        raycast_render(&eng.fb, &player, &map);
        engine_present(&eng);
    }

    engine_shutdown(&eng);
    return 0;
}