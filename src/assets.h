#ifndef ASSETS_H
#define ASSETS_H

#include "utils.h"

#define TEX_SIZE 64

typedef struct {
    int w, h;
    uint32_t *pixels; /* ARGB */
} Texture;

typedef struct {
    Texture wall_brick;     /* cell type 1 */
    Texture wall_door;      /* cell type 2 */
    Texture floor_tex;
    Texture ceiling_tex;
    Texture sprite_barrel;
    Texture sprite_enemy;
    Texture sprite_enemy_serg;
    Texture sprite_enemy_dead;
    Texture sprite_medkit;
    Texture sprite_ammo;
    Texture sprite_armor;
    Texture weapon_pistol;
    Texture weapon_shotgun;
} Assets;

int  assets_init(Assets *a);
void assets_shutdown(Assets *a);

/* Load PNG from path into tex (replaces existing). Returns 0 on success. */
int assets_load_png(Texture *tex, const char *path);

void texture_free(Texture *t);

#endif