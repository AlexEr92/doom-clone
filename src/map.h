#ifndef MAP_H
#define MAP_H

#include "utils.h"

typedef struct {
    int cells[MAP_MAX_H][MAP_MAX_W];
    int w, h;
    int start_x, start_y;
} Map;

int map_load(Map *m, const char *path);
int map_cell(const Map *m, int x, int y);
int map_is_wall(const Map *m, float x, float y);

#endif