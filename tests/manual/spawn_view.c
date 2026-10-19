/* Where a map puts its players, run by hand from the project root:
 *
 *   spawn_view [map] [players]
 *
 * Loads the map (assets/maps/level1.txt by default) with world_load() and
 * prints its spawn pool: the points drawn on the grid as 0-9 and then a, b, … in
 * the order of map.spawns, which way each one faces, and which other points
 * have a line of sight to it with every door shut, as on a fresh map. A map
 * with no 's' in it shows the pool map_load() makes for it. Then `players`
 * (MAX_PLAYERS by default) join one after another through world_add_player()
 * the way a server lets them in, and each line says at which point, how far
 * from the nearest player already in, and how many of those see it.
 *
 * What a map is checked against: twice as many points as players, none facing
 * a wall, none in sight of another (task 09-07). Nobody moves and no enemy
 * wakes up here, so the joins show where an empty server puts players, not
 * where a respawn mid-game would. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raycast_world.h"
#include "world.h"

#define DEFAULT_MAP "assets/maps/level1.txt"

static World w;

static char point_char(int i)
{
    return (char)(i < 10 ? '0' + i : 'a' + (i - 10));
}

/* E, SE, S, ...: +y runs down the map, so a quarter turn from east is south. */
static const char *facing_name(float angle)
{
    static const char *const NAMES[8] = {"E", "SE", "S", "SW", "W", "NW", "N", "NE"};
    int k = (int)lroundf(angle / (MATH_PI / 4.0f));
    return NAMES[(k % 8 + 8) % 8];
}

static int sees(float x0, float y0, float x1, float y1)
{
    return world_line_of_sight(&w.map, &w.doors, x0, y0, x1, y1);
}

static int count_markers(const char *path)
{
    FILE *f = fopen(path, "r");
    int n = 0, c;
    while (f && (c = fgetc(f)) != EOF) {
        n += c == 's';
    }
    if (f) {
        fclose(f);
    }
    return n;
}

static void print_grid(void)
{
    char grid[MAP_MAX_H][MAP_MAX_W + 1];
    for (int y = 0; y < w.map.h; y++) {
        for (int x = 0; x < w.map.w; x++) {
            int c = map_cell(&w.map, x, y);
            grid[y][x] = c == 1 ? '#' : c == 2 ? 'D' : '.';
        }
        grid[y][w.map.w] = '\0';
    }
    for (int i = 0; i < w.map.sprite_count; i++) {
        grid[(int)w.map.sprites[i].y][(int)w.map.sprites[i].x] = w.map.sprites[i].marker;
    }
    grid[(int)w.map.start.y][(int)w.map.start.x] = 'P';
    for (int i = 0; i < w.map.spawn_count; i++) {
        grid[(int)w.map.spawns[i].y][(int)w.map.spawns[i].x] = point_char(i);
    }
    printf("    ");
    for (int x = 0; x < w.map.w; x++) {
        putchar(x % 10 == 0 ? '0' + x / 10 : ' ');
    }
    printf("\n    ");
    for (int x = 0; x < w.map.w; x++) {
        putchar('0' + x % 10);
    }
    putchar('\n');
    for (int y = 0; y < w.map.h; y++) {
        printf("%3d %s\n", y, grid[y]);
    }
}

static void print_points(void)
{
    int pairs = 0;
    printf("\npoint  cell      faces  seen from\n");
    for (int i = 0; i < w.map.spawn_count; i++) {
        const SpawnPoint *a = &w.map.spawns[i];
        printf("  %c    (%2d,%2d)   %-5s ", point_char(i), (int)a->x, (int)a->y,
               facing_name(a->angle));
        int any = 0;
        for (int k = 0; k < w.map.spawn_count; k++) {
            const SpawnPoint *b = &w.map.spawns[k];
            if (k != i && sees(b->x, b->y, a->x, a->y)) {
                printf(" %c", point_char(k));
                any = 1;
            }
            /* A trace that grazes a corner can get through one way and not
             * the other: a pair counts if either point sees the other. */
            pairs += k > i && (sees(b->x, b->y, a->x, a->y) || sees(a->x, a->y, b->x, b->y));
        }
        printf("%s\n", any ? "" : " -");
    }
    printf("pairs of points in sight of each other, either way: %d\n", pairs);
}

static void print_joins(int players)
{
    printf("\njoin  id  point  nearest  seen by\n");
    for (int n = 0; n < players; n++) {
        int id = world_add_player(&w, NULL);
        if (id < 0) {
            printf("%4d  full\n", n);
            return;
        }
        const PlayerState *p = &w.players[id];
        int point = -1;
        for (int i = 0; i < w.map.spawn_count; i++) {
            if (w.map.spawns[i].x == p->x && w.map.spawns[i].y == p->y) {
                point = i;
            }
        }
        float nearest = -1.0f;
        int seen = 0;
        for (int k = 0; k < w.player_count; k++) {
            const PlayerState *o = &w.players[k];
            if (k == id || !o->active) {
                continue;
            }
            float d = sqrtf((o->x - p->x) * (o->x - p->x) + (o->y - p->y) * (o->y - p->y));
            nearest = nearest < 0.0f || d < nearest ? d : nearest;
            seen += sees(o->x, o->y, p->x, p->y);
        }
        printf("%4d  %2d    %c    ", n, id, point < 0 ? 'P' : point_char(point));
        if (nearest < 0.0f) {
            printf("    -       -\n");
        } else {
            printf("%5.1f  %6d\n", nearest, seen);
        }
    }
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : DEFAULT_MAP;
    int players = argc > 2 ? atoi(argv[2]) : MAX_PLAYERS;
    if (world_load(&w, path) != 0) {
        return 1;
    }
    int markers = count_markers(path);
    printf("%s: %d spawn points, %s\n\n", path, w.map.spawn_count,
           markers > 0 ? "from its 's' markers" : "made by map_load(): the map has no 's'");
    print_grid();
    print_points();
    print_joins(players);
    return 0;
}
