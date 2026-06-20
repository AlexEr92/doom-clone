#include "map.h"
#include <stdio.h>
#include <string.h>

int map_load(Map *m, const char *path) {
    memset(m, 0, sizeof(*m));
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Cannot open map: %s\n", path);
        return -1;
    }

    char line[256];
    int row = 0;
    m->w = MAP_MAX_W;
    m->h = MAP_MAX_H;

    while (row < MAP_MAX_H && fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        for (int col = 0; col < MAP_MAX_W && col < len; col++) {
            char c = line[col];
            switch (c) {
                case '#': m->cells[row][col] = 1; break;
                case 'D': m->cells[row][col] = 2; break;
                case 'P':
                    m->cells[row][col] = 0;
                    m->start_x = col;
                    m->start_y = row;
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
                default:  m->cells[row][col] = 0; break;
            }
        }
        for (int col = len; col < MAP_MAX_W; col++) {
            m->cells[row][col] = 1;
        }
        row++;
    }
    fclose(f);

    while (row < MAP_MAX_H) {
        for (int col = 0; col < MAP_MAX_W; col++) m->cells[row][col] = 1;
        row++;
    }
    return 0;
}

int map_cell(const Map *m, int x, int y) {
    if (x < 0 || x >= m->w || y < 0 || y >= m->h) return 1;
    return m->cells[y][x];
}

int map_is_wall(const Map *m, float x, float y) {
    int mx = (int)x;
    int my = (int)y;
    return map_cell(m, mx, my) != 0;
}