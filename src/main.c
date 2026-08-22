#include "engine.h"
#include "map.h"
#include "player.h"
#include "camera.h"
#include "raycast.h"
#include "input.h"
#include "assets.h"
#include "sprite.h"
#include "enemy.h"
#include "weapon.h"
#include "item.h"
#include "door.h"
#include "hud.h"
#include "audio.h"
#include "event.h"
#include "game.h"
#include "utils.h"
#include <SDL.h>
#include <stdio.h>
#include <math.h>

typedef struct {
    Map map;
    PlayerState player;
    SpriteList sprites;
    EnemyList enemies;
    ItemList items;
    DoorList doors;
    EventQueue events; /* what the last tick produced, drained by the client */
    int loaded;
} World;

/* Collect everyone who should keep a door from closing on them: live
 * players and live enemies. Moves into world_step() with 06-01. */
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

/* Drain the queue the last tick filled. Distance is measured from `me` — the
 * player this client looks through — so the same loop keeps working once the
 * simulation moves to a server and the queue arrives in a snapshot. */
static void world_play_events(Audio *au, const EventQueue *q, const PlayerState *players,
                              int player_count, const PlayerState *me)
{
    for (int i = 0; i < q->count; i++) {
        const GameEvent *e = &q->items[i];
        float dx = e->x - me->x, dy = e->y - me->y;
        float dist = sqrtf(dx * dx + dy * dy);
        switch (e->kind) {
            case EV_SHOT: {
                /* which weapon it was is the shooter's state, not the event's */
                int type = WEAPON_PISTOL;
                if (e->actor < player_count) {
                    type = players[e->actor].weapons.current;
                }
                audio_play_volume(au, type == WEAPON_PISTOL ? SND_PISTOL : SND_SHOTGUN, 0.5f);
                break;
            }
            case EV_NO_AMMO: audio_play_volume(au, SND_NO_AMMO, 0.5f); break;
            case EV_PICKUP: audio_play_volume(au, SND_PICKUP, 0.8f); break;
            case EV_PLAYER_HURT: audio_play(au, SND_PLAYER_HURT, dist, 12.0f); break;
            case EV_ENEMY_HURT: audio_play(au, SND_ENEMY_HURT, dist, 16.0f); break;
            case EV_ENEMY_DEATH: audio_play(au, SND_ENEMY_DEATH, dist, 16.0f); break;
            case EV_DOOR: audio_play(au, SND_DOOR, dist, 16.0f); break;
            default: break;
        }
    }
}

static int world_load(World *w, const char *map_path)
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

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const char *map_path = "assets/maps/level1.txt";

    Engine eng;
    if (engine_init(&eng) != 0) {
        fprintf(stderr, "Engine init failed\n");
        return 1;
    }

    Assets assets;
    if (assets_init(&assets) != 0) {
        fprintf(stderr, "Assets init failed\n");
        engine_shutdown(&eng);
        return 1;
    }

    Audio audio;
    /* Audio is optional: continue even if init fails. */
    if (audio_init(&audio) != 0) {
        fprintf(stderr, "audio: disabled (no SDL_mixer)\n");
        memset(&audio, 0, sizeof(audio));
    }

    Game game;
    game_init(&game, &eng);

    World world;
    if (world_load(&world, map_path) != 0) {
        fprintf(stderr,
                "Failed to load map '%s'. Run from the project root "
                "(assets are resolved relative to the working directory).\n",
                map_path);
        assets_shutdown(&assets);
        audio_shutdown(&audio);
        engine_shutdown(&eng);
        return 1;
    }

    InputState input;
    input_init(&input);

    double prev = get_time_seconds();
    double accumulator = 0.0;
    double fps_timer = 0.0;
    int frames = 0;
    char title[128];

    while (eng.running) {
        double now = get_time_seconds();
        double frame_time = now - prev;
        prev = now;
        if (frame_time > 0.25) {
            frame_time = 0.25;
        }

        fps_timer += frame_time;
        frames++;
        if (fps_timer >= 0.5) {
            eng.fps = (double)frames / fps_timer;
            const WeaponSystem *ws = &world.player.weapons;
            snprintf(title, sizeof(title), "doom-clone | %.0f FPS | HP %.0f | ammo %d/%d", eng.fps,
                     world.player.hp, ws->weapons[ws->current].ammo,
                     ws->weapons[ws->current].max_ammo);
            SDL_SetWindowTitle(eng.window, title);
            fps_timer = 0.0;
            frames = 0;
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (!game_handle_event(&game, &input, &ev)) {
                input_handle_event(&input, &ev, &eng.running);
            }
        }

        /* Restart requested: reload a fresh world. The FSM state is already
         * set by the event handler (GSTATE_MENU for pause/dead/win, or
         * GSTATE_PLAYING for menu->start). */
        if (game.restart) {
            if (world_load(&world, map_path) != 0) {
                fprintf(stderr, "Failed to reload map '%s'\n", map_path);
                eng.running = 0;
                break;
            }
            game.restart = 0;
            input_init(&input);
        }

        /* Global edge keys handled regardless of state (except menu). */
        if (game.state == GSTATE_PLAYING) {
            if (input.mute) {
                audio_toggle_mute(&audio);
            }
            if (input.pause_toggle) {
                game.state = GSTATE_PAUSED;
            }
            if (input.switch1) {
                weapon_switch(&world.player, WEAPON_PISTOL);
            }
            if (input.switch2) {
                weapon_switch(&world.player, WEAPON_SHOTGUN);
            }
        }

        if (game.state == GSTATE_PLAYING) {
            accumulator += frame_time;
            while (accumulator >= FIXED_DT) {
                event_queue_clear(&world.events);
                player_update(&world.player, &world.map, &world.doors, &input, FIXED_DT);
                if (input.use) {
                    door_try_use(&world.doors, &world.player, &world.map, &world.events);
                }
                DoorOccupant occ[1 + MAX_ENEMIES];
                int occ_count = world_door_occupants(&world, occ, 1 + MAX_ENEMIES);
                door_update_all(&world.doors, occ, occ_count, FIXED_DT);
                enemy_update_all(&world.enemies, &world.sprites, &world.map, &world.doors,
                                 &world.player, &world.events, FIXED_DT);
                item_update(&world.items, &world.sprites, &world.player, &world.events);
                weapon_update(&world.player, FIXED_DT);
                if (input.fire) {
                    weapon_try_fire(&world.player, &world.map, &world.doors, &world.player, 1,
                                    &world.enemies, &world.sprites, &world.events);
                }
                input_end_frame(&input);
                accumulator -= FIXED_DT;
                world_play_events(&audio, &world.events, &world.player, 1, &world.player);
            }

            /* Win/Lose transitions */
            if (world.player.hp <= 0.0f) {
                game.state = GSTATE_DEAD;
            } else if (enemy_all_dead(&world.enemies)) {
                game.state = GSTATE_WIN;
            }
        } else {
            /* In non-playing states we still clear edge inputs to avoid
             * carrying stale keys into gameplay after restart. */
            input_end_frame(&input);
            accumulator = 0.0;
        }

        game_update(&game, frame_time);

        /* Render world only if playing/paused/dead/win (not menu, where we
         * show a dim background instead). */
        if (game.state != GSTATE_MENU) {
            Camera cam;
            player_camera(&world.player, &cam);
            raycast_render(&eng.fb, &world.player, &cam, &world.map, &assets, &world.doors);
            sprite_render(&eng.fb, &world.sprites, &world.player, &cam, &assets);
            const WeaponSystem *ws = &world.player.weapons;
            hud_draw_weapon(&eng.fb,
                            ws->current == WEAPON_PISTOL ? &assets.weapon_pistol
                                                         : &assets.weapon_shotgun,
                            0, ws->weapons[ws->current].anim);
            hud_draw(&eng.fb, &world.player);
        } else {
            fb_clear(&eng.fb, make_color(15, 10, 20));
        }

        game_draw_overlay(&eng.fb, &game, &world.player);
        engine_present(&eng);
    }

    assets_shutdown(&assets);
    audio_shutdown(&audio);
    engine_shutdown(&eng);
    return 0;
}
