#include "world.h"
#include "weapon.h"
#include <string.h>

/* The button that selects each weapon: key 1 the first in WeaponType, and on. */
static const uint16_t WEAPON_BUTTONS[WEAPON_COUNT] = {
        [WEAPON_PISTOL] = BTN_WEAPON1, [WEAPON_SHOTGUN] = BTN_WEAPON2, [WEAPON_AK47] = BTN_WEAPON3,
        [WEAPON_RIFLE] = BTN_WEAPON4,  [WEAPON_GRENADE] = BTN_WEAPON5,
};

int world_load(World *w, const char *map_path)
{
    if (map_load(&w->map, map_path) != 0) {
        w->loaded = 0;
        return -1;
    }
    memset(w->players, 0, sizeof(w->players));
    w->player_count = 0;
    w->local_player = -1;
    enemy_list_init(&w->enemies);
    item_list_init(&w->items);
    door_list_init(&w->doors);
    event_queue_clear(&w->events);
    w->tick = 0;

    /* Barrels are on the map too, but they are decor with no state of their
     * own: the client puts them back on the screen straight from the map. */
    for (int i = 0; i < w->map.sprite_count; i++) {
        float sx = w->map.sprites[i].x;
        float sy = w->map.sprites[i].y;
        char mk = w->map.sprites[i].marker;
        switch (mk) {
            case 'E': enemy_spawn(&w->enemies, sx, sy, ENEMY_IMP); break;
            case 'S': enemy_spawn(&w->enemies, sx, sy, ENEMY_SERG); break;
            case 'M': item_add(&w->items, sx, sy, ITEM_MEDKIT, 25.0f, 0); break;
            case 'A':
                /* Give ammo to the current "primary" weapon: pistol. */
                item_add(&w->items, sx, sy, ITEM_AMMO, 12.0f, WEAPON_PISTOL);
                break;
            case 'R': item_add(&w->items, sx, sy, ITEM_ARMOR, 50.0f, 0); break;
        }
    }

    door_discover(&w->doors, &w->map);
    w->loaded = 1;
    return 0;
}

/* Where the player joining at `spawn_index` starts out. The map knows a single
 * start cell and nothing else until task 09-01 gives it a spawn pool, so index
 * 0 is that cell and the rest are the free cells that follow it in scan order:
 * enough to put ten players on a map without stacking them on one another.
 * An index past the last free cell falls back to the start. */
static void world_spawn_cell(const World *w, int spawn_index, int *cx, int *cy)
{
    *cx = w->map.start_x;
    *cy = w->map.start_y;
    if (spawn_index <= 0) {
        return;
    }
    int seen = 0;
    for (int y = 0; y < w->map.h; y++) {
        for (int x = 0; x < w->map.w; x++) {
            if (map_cell(&w->map, x, y) != 0) {
                continue;
            }
            if (x == w->map.start_x && y == w->map.start_y) {
                continue;
            }
            if (++seen == spawn_index) {
                *cx = x;
                *cy = y;
                return;
            }
        }
    }
}

int world_add_player(World *w, int spawn_index)
{
    int id = -1;
    for (int i = 0; i < w->player_count; i++) {
        if (!w->players[i].active) {
            id = i;
            break;
        }
    }
    if (id < 0) {
        if (w->player_count >= MAX_PLAYERS) {
            return -1;
        }
        id = w->player_count++;
    }

    int cx, cy;
    world_spawn_cell(w, spawn_index, &cx, &cy);
    PlayerState *p = &w->players[id];
    player_init(p, cx, cy);
    p->id = (uint8_t)id;
    return id;
}

void world_remove_player(World *w, int id)
{
    if (id < 0 || id >= w->player_count) {
        return;
    }
    /* Dead as well as gone: a free slot must be invisible to everything that
     * scans the array — hitscan, collision, doors — and every one of them
     * already skips a player who is not alive. */
    w->players[id].active = 0;
    w->players[id].alive = 0;
    if (w->local_player == id) {
        w->local_player = -1;
    }
}

/* Collect everyone who should keep a door from closing on them: live players
 * and live enemies. */
static int world_door_occupants(const World *w, DoorOccupant *occ, int max)
{
    int n = 0;
    for (int i = 0; i < w->player_count && n < max; i++) {
        const PlayerState *p = &w->players[i];
        if (p->active && p->hp > 0.0f) {
            occ[n].x = p->x;
            occ[n].y = p->y;
            n++;
        }
    }
    for (int i = 0; i < w->enemies.count && n < max; i++) {
        const Enemy *e = &w->enemies.items[i];
        if (e->state != ESTATE_DEAD) {
            occ[n].x = e->x;
            occ[n].y = e->y;
            n++;
        }
    }
    return n;
}

void world_step(World *w, const PlayerInput inputs[], int count, double dt)
{
    event_queue_clear(&w->events);

    /* One input per player, resolved once and read by every phase below. An
     * input is matched to a player by id, which is the index it sits at: the
     * server fills the array by id and leaves a gap where a slot is free.
     * No input for this tick — a dropped packet, or a player who is not being
     * driven at all — means standing still, facing the way they already do. */
    PlayerInput tick_input[MAX_PLAYERS];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const PlayerState *p = &w->players[i];
        PlayerInput idle = {w->tick, 0, p->angle};
        tick_input[i] = (inputs && p->active && p->id < count) ? inputs[p->id] : idle;
    }

    /* The order below is part of the contract, not an accident of how main.c
     * happened to be written: reconciliation replays these ticks and has to
     * land on the same state the server did. Within a phase, players are
     * walked by ascending id for the same reason — who moves first decides
     * which of two players ends up standing where they collided. */

    /* players */
    for (int i = 0; i < w->player_count; i++) {
        PlayerState *p = &w->players[i];
        if (!p->active) {
            continue;
        }
        player_update(p, &w->map, &w->doors, &tick_input[i], w->players, w->player_count, dt);
    }

    /* doors */
    for (int i = 0; i < w->player_count; i++) {
        PlayerState *p = &w->players[i];
        if (p->active && (tick_input[i].buttons & BTN_USE)) {
            door_try_use(&w->doors, p, &w->map, &w->events);
        }
    }
    DoorOccupant occ[MAX_PLAYERS + MAX_ENEMIES];
    int occ_count = world_door_occupants(w, occ, (int)(sizeof(occ) / sizeof(occ[0])));
    door_update_all(&w->doors, occ, occ_count, dt);

    /* enemies */
    enemy_update_all(&w->enemies, &w->map, &w->doors, w->players, w->player_count, &w->events, dt);

    /* items */
    item_update(&w->items, w->players, w->player_count, &w->events);

    /* weapons */
    for (int i = 0; i < w->player_count; i++) {
        PlayerState *p = &w->players[i];
        if (!p->active) {
            continue;
        }
        uint16_t buttons = tick_input[i].buttons;
        /* Two keys in one tick: the later weapon in the table wins, and a
         * weapon not picked up yet is no choice at all. */
        for (int k = 0; k < WEAPON_COUNT; k++) {
            if (buttons & WEAPON_BUTTONS[k]) {
                weapon_switch(p, k);
            }
        }
        weapon_update(p, dt);
        if (buttons & BTN_FIRE) {
            weapon_try_fire(p, &w->map, &w->doors, w->players, w->player_count, &w->enemies,
                            &w->events);
        }
    }

    w->tick++;
}
