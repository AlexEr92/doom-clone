#include "engine.h"
#include "camera.h"
#include "raycast.h"
#include "input.h"
#include "assets.h"
#include "sprite.h"
#include "enemy.h"
#include "weapon.h"
#include "hud.h"
#include "audio.h"
#include "event.h"
#include "game.h"
#include "world.h"
#include "config.h"
#include "utils.h"
#include <SDL.h>
#include <stdio.h>
#include <math.h>

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
    /* A freshly loaded world has every slot free, so this cannot fail. */
    world.local_player = world_add_player(&world, 0);

    /* A client always looks through a player of its own; a dedicated server has
     * none, which is what local_player < 0 means, and renders nothing. */
    PlayerState *me = &world.players[world.local_player];

    /* The client turns from wherever its player spawned facing: the angle in
     * every input is absolute, and a zero would spin the player round on the
     * first tick. */
    InputState input;
    input_init(&input);
    input.angle = me->angle;

    /* The render list belongs to the client and is rebuilt from the world once
     * a frame; the world itself keeps no sprites. */
    SpriteList sprites;
    sprite_init(&sprites);
    SpriteAnim anim;
    sprite_anim_init(&anim, &assets);

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
            const WeaponSystem *ws = &me->weapons;
            snprintf(title, sizeof(title), "doom-clone | %.0f FPS | HP %.0f | ammo %d/%d", eng.fps,
                     me->hp, ws->weapons[ws->current].ammo, ws->weapons[ws->current].max_ammo);
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
            world.local_player = world_add_player(&world, 0);
            me = &world.players[world.local_player];
            game.restart = 0;
            input_init(&input);
            input.angle = me->angle;
            sprite_anim_init(&anim, &assets);
        }

        /* Global edge keys handled regardless of state (except menu). */
        if (game.state == GSTATE_PLAYING) {
            if (input.mute) {
                audio_toggle_mute(&audio);
            }
            if (input.pause_toggle) {
                game.state = GSTATE_PAUSED;
            }
        }

        if (game.state == GSTATE_PLAYING) {
            /* Turning is the client's own, so it follows the frame rather than
             * the tick; each tick below is handed the angle as it stands. */
            input_turn(&input, frame_time);
            accumulator += frame_time;
            while (accumulator >= FIXED_DT) {
                PlayerInput pi = input_sample(&input, world.tick);
                world_step(&world, &pi, 1, FIXED_DT);
                accumulator -= FIXED_DT;
                world_play_events(&audio, &world.events, world.players, world.player_count, me);
            }

            /* Win/Lose transitions */
            if (me->hp <= 0.0f) {
                game.state = GSTATE_DEAD;
            } else if (enemy_all_dead(&world.enemies)) {
                game.state = GSTATE_WIN;
            }
        } else {
            /* Nothing is simulated in this state, so a press made in it has no
             * tick to go to: kept, it would fire on the first tick after. */
            input_clear_edges(&input);
            accumulator = 0.0;
        }
        input_end_frame(&input);

        game_update(&game, frame_time);

        /* Render world only if playing/paused/dead/win (not menu, where we
         * show a dim background instead). */
        if (game.state != GSTATE_MENU) {
            Camera cam;
            player_camera(me, &cam);
            raycast_render(&eng.fb, me, &cam, &world.map, &assets, &world.doors);
            /* A pause stops the animations with the world. Death and victory
             * stop only the world: the last enemy shot still falls, and
             * whoever killed the player keeps at it. */
            sprite_anim_update(&anim, &world,
                               game.state == GSTATE_PAUSED ? 0.0f : (float)frame_time);
            sprites_build_from_world(&sprites, &world, world.local_player, &anim, &assets);
            sprite_render(&eng.fb, &sprites, me, &cam, &assets);
            const WeaponSystem *ws = &me->weapons;
            hud_draw_weapon(&eng.fb,
                            ws->current == WEAPON_PISTOL ? &assets.weapon_pistol
                                                         : &assets.weapon_shotgun,
                            ws->weapons[ws->current].anim);
            hud_draw(&eng.fb, me);
        } else {
            fb_clear(&eng.fb, make_color(15, 10, 20));
        }

        game_draw_overlay(&eng.fb, &game, me);
        engine_present(&eng);
    }

    assets_shutdown(&assets);
    audio_shutdown(&audio);
    engine_shutdown(&eng);
    return 0;
}
