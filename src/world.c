#include "world.h"
#include "raycast_world.h"
#include "weapon.h"
#include <math.h>
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
    memset(w->spawned_at, 0, sizeof(w->spawned_at));

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

/* world_pick_spawn() scores a point in cells of distance. No two points on a
 * map lie farther apart than SPAWN_FAR, and no enemy's penalty comes to more,
 * so the one for being seen outweighs every other term together: a distance,
 * MAX_ENEMIES enemies and a recent spawn. */
#define SPAWN_FAR ((float)(MAP_MAX_W + MAP_MAX_H))
#define SPAWN_SEEN_PENALTY (SPAWN_FAR * (float)(MAX_ENEMIES + 3))
#define SPAWN_RECENT_PENALTY 8.0f /* cells */
#define SPAWN_RECENT_TICKS (10 * TICK_RATE)

/* A player who can be hurt and can shoot back: the test the enemies and the
 * items make as well. */
static int is_live(const PlayerState *p)
{
    return p->active && p->alive && p->hp > 0.0f;
}

/* The score of map.spawns[i] for `player_id`, and in *taken whether a live
 * player stands on it, close enough that whoever came in there would be
 * inside them. */
static float spawn_score(const World *w, int player_id, int i, int *taken)
{
    const SpawnPoint *sp = &w->map.spawns[i];
    float nearest = SPAWN_FAR;
    int seen = 0;
    for (int k = 0; k < w->player_count; k++) {
        const PlayerState *p = &w->players[k];
        if (k == player_id || !is_live(p)) {
            continue;
        }
        float dx = p->x - sp->x, dy = p->y - sp->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < nearest) {
            nearest = d;
        }
        if (!seen && world_line_of_sight(&w->map, &w->doors, p->x, p->y, sp->x, sp->y)) {
            seen = 1;
        }
    }
    *taken = nearest < 2.0f * PLAYER_RADIUS;
    float penalty = seen ? SPAWN_SEEN_PENALTY : 0.0f;
    for (int k = 0; k < w->enemies.count; k++) {
        const Enemy *e = &w->enemies.items[k];
        if (e->state == ESTATE_DEAD) {
            continue;
        }
        float dx = e->x - sp->x, dy = e->y - sp->y;
        float into = enemy_def(e->type)->detect_range - sqrtf(dx * dx + dy * dy);
        if (into > 0.0f) {
            penalty += into < SPAWN_FAR ? into : SPAWN_FAR;
        }
    }
    if (player_id >= 0 && player_id < MAX_PLAYERS) {
        uint32_t at = w->spawned_at[player_id][i];
        if (at != 0 && w->tick - (at - 1) < SPAWN_RECENT_TICKS) {
            penalty += SPAWN_RECENT_PENALTY;
        }
    }
    return nearest - penalty;
}

const SpawnPoint *world_pick_spawn(const World *w, int player_id)
{
    int best = -1, best_taken = 0;
    float best_score = 0.0f;
    for (int i = 0; i < w->map.spawn_count; i++) {
        int taken;
        float score = spawn_score(w, player_id, i, &taken);
        int better = taken == best_taken ? score > best_score : !taken;
        if (best < 0 || better) {
            best = i;
            best_taken = taken;
            best_score = score;
        }
    }
    return best < 0 ? NULL : &w->map.spawns[best];
}

/* Put player `id` on the map at `at`, as player_init() leaves a player: whole,
 * with the pistol alone. `at` NULL is the point world_pick_spawn() finds, and
 * the map's start when the map has no pool. */
static void spawn_player(World *w, int id, const SpawnPoint *at)
{
    const SpawnPoint *sp = at;
    if (!sp) {
        sp = world_pick_spawn(w, id);
        if (sp) {
            w->spawned_at[id][sp - w->map.spawns] = w->tick + 1;
        } else {
            sp = &w->map.start;
        }
    }
    PlayerState *p = &w->players[id];
    player_init(p, (int)sp->x, (int)sp->y);
    p->angle = sp->angle;
    p->id = (uint8_t)id;
}

int world_add_player(World *w, const SpawnPoint *at)
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

    /* Whoever had the slot before took their spawns with them. */
    memset(w->spawned_at[id], 0, sizeof(w->spawned_at[id]));
    spawn_player(w, id, at);
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
        if (is_live(p)) {
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

    /* players: the dead wait out RESPAWN_TIME and come back where
     * world_pick_spawn() finds safest now, protected for SPAWN_PROTECT_TIME;
     * the living move. A dead player's input goes nowhere, in this phase or
     * any after it. */
    for (int i = 0; i < w->player_count; i++) {
        PlayerState *p = &w->players[i];
        if (!p->active) {
            continue;
        }
        if (!p->alive) {
            p->respawn_timer -= (float)dt;
            if (p->respawn_timer <= 0.0f) {
                spawn_player(w, i, NULL);
                p->spawn_protect_timer = SPAWN_PROTECT_TIME;
            }
            continue;
        }
        if (p->spawn_protect_timer > 0.0f) {
            p->spawn_protect_timer = fmaxf(p->spawn_protect_timer - (float)dt, 0.0f);
        }
        player_update(p, &w->map, &w->doors, &tick_input[i], w->players, w->player_count, dt);
    }

    /* doors */
    for (int i = 0; i < w->player_count; i++) {
        PlayerState *p = &w->players[i];
        if (is_live(p) && (tick_input[i].buttons & BTN_USE)) {
            door_try_use(&w->doors, p, &w->map, &w->events);
        }
    }
    DoorOccupant occ[MAX_PLAYERS + MAX_ENEMIES];
    int occ_count = world_door_occupants(w, occ, (int)(sizeof(occ) / sizeof(occ[0])));
    door_update_all(&w->doors, occ, occ_count, dt);

    /* enemies */
    enemy_update_all(&w->enemies, &w->map, &w->doors, w->players, w->player_count, &w->events, dt);

    /* items */
    item_update(&w->items, w->players, w->player_count, &w->events, dt);

    /* weapons: a player shot dead earlier in this phase fires no more */
    for (int i = 0; i < w->player_count; i++) {
        PlayerState *p = &w->players[i];
        if (!is_live(p)) {
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
