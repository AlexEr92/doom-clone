#ifndef MAP_H
#define MAP_H

#include "config.h"
#include "utils.h"
#include <stddef.h>

#define MAX_MAP_SPRITES 64

/* Where the game keeps its maps, relative to the project root it runs from. A
 * map is named by its file name in here: on the command line, and in
 * MSG_ACCEPT, which each end of a network game resolves against its own copy. */
#define MAP_DIR "assets/maps/"

typedef struct {
    float x, y;
    char marker; /* 'E','S','B','M','A','R' */
} MapSpriteSpawn;

/* Where a player comes into the world: the centre of a free cell, and the way
 * they face on arrival. */
typedef struct {
    float x, y;
    float angle; /* radians, as PlayerState.angle */
} SpawnPoint;

typedef struct Map {
    int cells[MAP_MAX_H][MAP_MAX_W];
    int w, h;         /* the file's longest row and its number of rows, up to MAP_MAX_W/H */
    SpawnPoint start; /* 'P': where a solo game begins, facing -X */
    /* 's': where a multiplayer game puts players, chosen afresh at every
     * spawn by world_pick_spawn(). A point belongs to the map, not to a
     * player. A map with no 's' gets a pool made from its free cells. */
    SpawnPoint spawns[MAX_SPAWNS];
    int spawn_count;
    MapSpriteSpawn sprites[MAX_MAP_SPRITES];
    int sprite_count;
} Map;

int map_load(Map *m, const char *path);

/* The path of the map whose file name is `name`: MAP_DIR and the name, into
 * `out` of `size` bytes. Returns 0, or -1 when it does not fit, and `out` then
 * holds as much of it as did. */
int map_file_path(char *out, size_t size, const char *name);
int map_cell(const Map *m, int x, int y);
int map_is_wall(const Map *m, float x, float y);

/* Forward decl: DoorList defined in door.h */
struct DoorList;

/* Wall test that also respects doors: a closed door blocks, an open one
 * does not. Pass NULL for dl to ignore doors (behaves like map_is_wall). */
int map_is_wall_door(const Map *m, const struct DoorList *dl, float x, float y);

#endif
