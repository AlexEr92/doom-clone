/* What a frame of the walls costs on a map, run by hand from the project root:
 *
 *   render_bench [map] [repeats]
 *
 * Loads the map (assets/maps/level1.txt by default) and the textures
 * assets_init() finds, and times raycast_render() from the centre of every
 * free cell, facing each of eight ways, `repeats` times a view (3 by default).
 * Prints the mean time of a frame and the worst view, with its cell. Every
 * door is shut, as on a fresh map.
 *
 * The FPS in the window title answers another question: the whole frame,
 * sprites, status bar, upscale and present included, of which the walls are a
 * small part — and vsync holds it at the display's rate unless the game runs
 * with SDL_RENDER_VSYNC=0. What this measures is the column loop alone, the
 * DDA of each ray and the texture fill, so two maps or two builds are
 * compared on that and nothing else. */

#define _POSIX_C_SOURCE 199309L /* clock_gettime() under -std=c11 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "assets.h"
#include "camera.h"
#include "door.h"
#include "map.h"
#include "raycast.h"

#define DEFAULT_MAP "assets/maps/level1.txt"

static Map map;
static DoorList doors;
static Assets assets;
static uint32_t pixels[SCREEN_W * SCREEN_H];

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : DEFAULT_MAP;
    int repeats = argc > 2 ? atoi(argv[2]) : 3;
    if (repeats < 1) {
        fprintf(stderr, "usage: %s [map] [repeats]\n", argv[0]);
        return 2;
    }
    if (map_load(&map, path) != 0) {
        return 1;
    }
    door_list_init(&doors);
    door_discover(&doors, &map);
    if (assets_init(&assets) != 0) {
        return 1;
    }

    Framebuffer fb = {pixels};
    int cells = 0, views = 0, worst_x = 0, worst_y = 0;
    double total = 0.0, worst = 0.0;
    for (int y = 0; y < map.h; y++) {
        for (int x = 0; x < map.w; x++) {
            if (map_cell(&map, x, y) != 0) {
                continue;
            }
            cells++;
            for (int k = 0; k < 8; k++) {
                /* Off the eight directions a little, so that no ray of the
                 * middle column runs exactly along a grid line. */
                PlayerState p = {0};
                p.x = (float)x + 0.5f;
                p.y = (float)y + 0.5f;
                p.angle = (float)k * MATH_PI / 4.0f + 0.1f;
                Camera cam;
                player_camera(&p, &cam);
                double start = now();
                for (int r = 0; r < repeats; r++) {
                    raycast_render(&fb, &p, &cam, &map, &assets, &doors);
                }
                double frame = (now() - start) / repeats;
                total += frame;
                views++;
                if (frame > worst) {
                    worst = frame;
                    worst_x = x;
                    worst_y = y;
                }
            }
        }
    }
    assets_shutdown(&assets);

    if (views == 0) {
        printf("%s: %dx%d, no free cell to stand in\n", path, map.w, map.h);
        return 1;
    }
    printf("%s: %dx%d, %d free cells, %d views: mean %.3f ms, worst %.3f ms at (%d, %d)\n", path,
           map.w, map.h, cells, views, total / views * 1e3, worst * 1e3, worst_x, worst_y);
    return 0;
}
