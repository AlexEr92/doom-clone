#include "map.h"
#include "door.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Points made for a map that names none: twice as many as there are players,
 * so that a full server still has a choice of where to put the next one. */
#define MAP_AUTO_SPAWNS (2 * MAX_PLAYERS)

/* The eight ways a spawn point may face, each with its angle. */
static const struct {
    int dx, dy;
    float angle;
} FACINGS[] = {
        {1, 0, 0.0f},
        {1, 1, MATH_PI / 4.0f},
        {0, 1, MATH_PI / 2.0f},
        {-1, 1, 3.0f * MATH_PI / 4.0f},
        {-1, 0, MATH_PI},
        {-1, -1, -3.0f * MATH_PI / 4.0f},
        {0, -1, -MATH_PI / 2.0f},
        {1, -1, -MATH_PI / 4.0f},
};

/* How far one sees from cell (x, y) along (dx, dy) before a wall or a door,
 * in cells. A diagonal step needs the two cells beside it free as well: the
 * corner where two walls meet is not a view. */
static float map_free_run(const Map *m, int x, int y, int dx, int dy)
{
    int steps = 0;
    while (map_cell(m, x + dx, y + dy) == 0 && map_cell(m, x + dx, y) == 0 &&
           map_cell(m, x, y + dy) == 0) {
        x += dx;
        y += dy;
        steps++;
    }
    return (float)steps * (dx != 0 && dy != 0 ? 1.41421356f : 1.0f);
}

/* The facing of a spawn point at cell (x, y): along the longest clear line out
 * of it, so that nobody arrives with their nose to a wall. Every door is shut
 * on a freshly loaded map and counts as one. Ties go to the first in FACINGS. */
static float map_open_angle(const Map *m, int x, int y)
{
    int best = 0;
    float best_run = -1.0f;
    for (int i = 0; i < (int)(sizeof(FACINGS) / sizeof(FACINGS[0])); i++) {
        float run = map_free_run(m, x, y, FACINGS[i].dx, FACINGS[i].dy);
        if (run > best_run) {
            best_run = run;
            best = i;
        }
    }
    return FACINGS[best].angle;
}

/* Take cell (x, y) into the pool and bring every remaining candidate's
 * distance to the nearest point taken up to date. */
static void map_take_spawn(Map *m, int nearest[MAP_MAX_H][MAP_MAX_W], int x, int y)
{
    SpawnPoint *sp = &m->spawns[m->spawn_count++];
    sp->x = x + 0.5f;
    sp->y = y + 0.5f;
    sp->angle = map_open_angle(m, x, y);
    nearest[y][x] = -1;
    for (int cy = 0; cy < m->h; cy++) {
        for (int cx = 0; cx < m->w; cx++) {
            int d = (cx - x) * (cx - x) + (cy - y) * (cy - y);
            if (nearest[cy][cx] > d) {
                nearest[cy][cx] = d;
            }
        }
    }
}

/* A pool for a map with no 's' in it, out of the free cells a player can walk
 * to from the start, doors included: no point in a sealed pocket of the map,
 * and none on a cell an enemy or an item already holds. The start is the
 * first point, and each next one the candidate farthest from all the points
 * taken so far, ties to the first in reading order — the load is as
 * deterministic as a tick, so the start stands in for a random first pick.
 * Without a 'P' the walk begins at the first free cell instead. */
static void map_auto_spawns(Map *m, int have_start)
{
    int sx = (int)m->start.x, sy = (int)m->start.y;
    if (!have_start) {
        sx = -1;
        for (int y = 0; y < m->h && sx < 0; y++) {
            for (int x = 0; x < m->w && sx < 0; x++) {
                if (m->cells[y][x] == 0) {
                    sx = x;
                    sy = y;
                }
            }
        }
        if (sx < 0) {
            return; /* nowhere to stand at all */
        }
    }

    /* Flood fill from the start. A cell's state ends up as -1 for "never a
     * point", or INT_MAX for a candidate nobody is near yet. */
    int nearest[MAP_MAX_H][MAP_MAX_W];
    for (int y = 0; y < MAP_MAX_H; y++) {
        for (int x = 0; x < MAP_MAX_W; x++) {
            nearest[y][x] = -1;
        }
    }
    static const int STEPS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int queue[MAP_MAX_W * MAP_MAX_H];
    int head = 0, tail = 0;
    nearest[sy][sx] = INT_MAX;
    queue[tail++] = sy * MAP_MAX_W + sx;
    while (head < tail) {
        int x = queue[head] % MAP_MAX_W;
        int y = queue[head] / MAP_MAX_W;
        head++;
        for (int i = 0; i < 4; i++) {
            int nx = x + STEPS[i][0], ny = y + STEPS[i][1];
            int c = map_cell(m, nx, ny);
            if ((c == 0 || c == 2) && nearest[ny][nx] < 0) {
                nearest[ny][nx] = INT_MAX;
                queue[tail++] = ny * MAP_MAX_W + nx;
            }
        }
    }
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            if (m->cells[y][x] != 0) {
                nearest[y][x] = -1;
            }
        }
    }
    for (int i = 0; i < m->sprite_count; i++) {
        nearest[(int)m->sprites[i].y][(int)m->sprites[i].x] = -1;
    }

    if (nearest[sy][sx] >= 0) {
        map_take_spawn(m, nearest, sx, sy);
    }
    while (m->spawn_count < MAP_AUTO_SPAWNS) {
        int bx = -1, by = -1, best = 0;
        for (int y = 0; y < m->h; y++) {
            for (int x = 0; x < m->w; x++) {
                if (nearest[y][x] > best) {
                    best = nearest[y][x];
                    bx = x;
                    by = y;
                }
            }
        }
        if (bx < 0) {
            break; /* every reachable cell is a point already */
        }
        map_take_spawn(m, nearest, bx, by);
    }
}

int map_load(Map *m, const char *path)
{
    memset(m, 0, sizeof(*m));
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Cannot open map: %s\n", path);
        return -1;
    }

    char line[256];
    int row = 0;
    int have_start = 0;
    int spawns_dropped = 0;

    /* The map is as wide as its longest row and as tall as its rows. Past a
     * row's end the cells are wall, and so is all of the grid outside the map:
     * map_cell() never reads there, but a loop over the whole grid may. */
    while (row < MAP_MAX_H && fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len > m->w) {
            m->w = len < MAP_MAX_W ? len : MAP_MAX_W;
        }
        for (int col = 0; col < MAP_MAX_W && col < len; col++) {
            char c = line[col];
            switch (c) {
                case '#': m->cells[row][col] = 1; break;
                case 'D': m->cells[row][col] = 2; break;
                case 'P':
                    m->cells[row][col] = 0;
                    m->start.x = col + 0.5f;
                    m->start.y = row + 0.5f;
                    m->start.angle = MATH_PI;
                    have_start = 1;
                    break;
                case 's':
                    m->cells[row][col] = 0;
                    if (m->spawn_count < MAX_SPAWNS) {
                        m->spawns[m->spawn_count].x = col + 0.5f;
                        m->spawns[m->spawn_count].y = row + 0.5f;
                        m->spawn_count++;
                    } else {
                        spawns_dropped++;
                    }
                    break;
                case 'E':
                case 'S':
                case 'B':
                case 'M':
                case 'A':
                case 'R':
                    m->cells[row][col] = 0;
                    if (m->sprite_count < MAX_MAP_SPRITES) {
                        m->sprites[m->sprite_count].x = col + 0.5f;
                        m->sprites[m->sprite_count].y = row + 0.5f;
                        m->sprites[m->sprite_count].marker = c;
                        m->sprite_count++;
                    }
                    break;
                default: m->cells[row][col] = 0; break;
            }
        }
        for (int col = len; col < MAP_MAX_W; col++) {
            m->cells[row][col] = 1;
        }
        row++;
    }
    fclose(f);

    m->h = row;
    while (row < MAP_MAX_H) {
        for (int col = 0; col < MAP_MAX_W; col++) {
            m->cells[row][col] = 1;
        }
        row++;
    }

    /* A facing needs the whole grid, so it comes after the last row. */
    for (int i = 0; i < m->spawn_count; i++) {
        SpawnPoint *sp = &m->spawns[i];
        sp->angle = map_open_angle(m, (int)sp->x, (int)sp->y);
    }
    if (spawns_dropped > 0) {
        fprintf(stderr, "Map %s: %d spawn points past the limit of %d ignored\n", path,
                spawns_dropped, MAX_SPAWNS);
    }
    if (m->spawn_count == 0) {
        map_auto_spawns(m, have_start);
    } else if (m->spawn_count < MAP_AUTO_SPAWNS) {
        fprintf(stderr, "Map %s: %d spawn points, %d players want at least %d\n", path,
                m->spawn_count, MAX_PLAYERS, MAP_AUTO_SPAWNS);
    }
    return 0;
}

int map_file_path(char *out, size_t size, const char *name)
{
    int n = snprintf(out, size, MAP_DIR "%s", name);
    return n >= 0 && (size_t)n < size ? 0 : -1;
}

int map_cell(const Map *m, int x, int y)
{
    if (x < 0 || x >= m->w || y < 0 || y >= m->h) {
        return 1;
    }
    return m->cells[y][x];
}

int map_is_wall(const Map *m, float x, float y)
{
    int mx = (int)x;
    int my = (int)y;
    return map_cell(m, mx, my) != 0;
}

int map_is_wall_door(const Map *m, const struct DoorList *dl, float x, float y)
{
    int mx = (int)x;
    int my = (int)y;
    int c = map_cell(m, mx, my);
    if (c == 0) {
        return 0;
    }
    if (c == 2 && dl) {
        /* door: blocking only if openness < 0.5 */
        return door_is_blocking(dl, mx, my);
    }
    return 1;
}
