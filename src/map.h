#ifndef MAP_H
#define MAP_H

#include "utils.h"

#define MAX_MAP_SPRITES 64

typedef struct {
    float x, y;
    char marker; /* 'E','S','B','M','A','R' */
} MapSpriteSpawn;

typedef struct {
    int cells[MAP_MAX_H][MAP_MAX_W];
    int w, h;
    int start_x, start_y;
    MapSpriteSpawn sprites[MAX_MAP_SPRITES];
    int sprite_count;
} Map;

int map_load(Map *m, const char *path);
int map_cell(const Map *m, int x, int y);
int map_is_wall(const Map *m, float x, float y);

/* Forward decl: DoorList defined in door.h */
struct DoorList;

/* Wall test that also respects doors: a closed door blocks, an open one
 * does not. Pass NULL for dl to ignore doors (behaves like map_is_wall). */
int map_is_wall_door(const Map *m, struct DoorList *dl, float x, float y);

#endif