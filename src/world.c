#include "world.h"
#include "weapon.h"
#include "utils.h"
#include <string.h>

int world_load(World *w, const char *map_path)
{
    if (map_load(&w->map, map_path) != 0) {
        w->loaded = 0;
        return -1;
    }
    player_init(&w->player, w->map.start_x, w->map.start_y);
    sprite_init(&w->sprites);
    enemy_list_init(&w->enemies);
    item_list_init(&w->items);
    door_list_init(&w->doors);
    event_queue_clear(&w->events);
    w->tick = 0;

    for (int i = 0; i < w->map.sprite_count; i++) {
        float sx = w->map.sprites[i].x;
        float sy = w->map.sprites[i].y;
        char mk = w->map.sprites[i].marker;
        switch (mk) {
            case 'E': enemy_spawn(&w->enemies, &w->sprites, sx, sy, ENEMY_IMP); break;
            case 'S': enemy_spawn(&w->enemies, &w->sprites, sx, sy, ENEMY_SERG); break;
            case 'B': sprite_add(&w->sprites, sx, sy, SPRITE_BARREL); break;
            case 'M': {
                int sid = sprite_add(&w->sprites, sx, sy, SPRITE_MEDKIT);
                item_add(&w->items, sid, ITEM_MEDKIT, 25.0f, 0);
                break;
            }
            case 'A': {
                int sid = sprite_add(&w->sprites, sx, sy, SPRITE_AMMO);
                /* Give ammo to the current "primary" weapon: pistol. */
                item_add(&w->items, sid, ITEM_AMMO, 12.0f, WEAPON_PISTOL);
                break;
            }
            case 'R': {
                int sid = sprite_add(&w->sprites, sx, sy, SPRITE_ARMOR);
                item_add(&w->items, sid, ITEM_ARMOR, 50.0f, 0);
                break;
            }
        }
    }

    door_discover(&w->doors, &w->map);
    w->loaded = 1;
    return 0;
}

/* Collect everyone who should keep a door from closing on them: live players
 * and live enemies. */
static int world_door_occupants(const World *w, DoorOccupant *occ, int max)
{
    int n = 0;
    if (w->player.hp > 0.0f && n < max) {
        occ[n].x = w->player.x;
        occ[n].y = w->player.y;
        n++;
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

/* player_update() still reads the raw InputState, so the buttons of the tick
 * are unpacked back into one. Turning is not among them: the angle in
 * PlayerInput has already replaced the turn keys and the mouse. Task 06-06
 * drops this bridge along with the fields it leaves empty. */
static InputState buttons_to_input_state(uint16_t buttons)
{
    InputState s;
    memset(&s, 0, sizeof(s));
    s.forward = (buttons & BTN_FORWARD) != 0;
    s.back = (buttons & BTN_BACK) != 0;
    s.strafe_left = (buttons & BTN_LEFT) != 0;
    s.strafe_right = (buttons & BTN_RIGHT) != 0;
    return s;
}

void world_step(World *w, const PlayerInput inputs[], int count, double dt)
{
    event_queue_clear(&w->events);

    /* No input for this tick — a dropped packet, or a player who is not being
     * driven at all — means standing still, facing the way they already do. */
    PlayerInput in = {w->tick, 0, w->player.angle};
    if (inputs && count > 0) {
        in = inputs[0];
    }

    /* The order below is part of the contract, not an accident of how main.c
     * happened to be written: reconciliation replays these ticks and has to
     * land on the same state the server did. */

    /* players */
    w->player.angle = wrap_angle(in.angle);
    InputState move = buttons_to_input_state(in.buttons);
    player_update(&w->player, &w->map, &w->doors, &move, dt);

    /* doors */
    if (in.buttons & BTN_USE) {
        door_try_use(&w->doors, &w->player, &w->map, &w->events);
    }
    DoorOccupant occ[MAX_PLAYERS + MAX_ENEMIES];
    int occ_count = world_door_occupants(w, occ, (int)(sizeof(occ) / sizeof(occ[0])));
    door_update_all(&w->doors, occ, occ_count, dt);

    /* enemies */
    enemy_update_all(&w->enemies, &w->sprites, &w->map, &w->doors, &w->player, &w->events, dt);

    /* items */
    item_update(&w->items, &w->sprites, &w->player, &w->events);

    /* weapons */
    if (in.buttons & BTN_WEAPON1) {
        weapon_switch(&w->player, WEAPON_PISTOL);
    }
    if (in.buttons & BTN_WEAPON2) {
        weapon_switch(&w->player, WEAPON_SHOTGUN);
    }
    weapon_update(&w->player, dt);
    if (in.buttons & BTN_FIRE) {
        weapon_try_fire(&w->player, &w->map, &w->doors, &w->player, 1, &w->enemies, &w->sprites,
                        &w->events);
    }

    w->tick++;
}
