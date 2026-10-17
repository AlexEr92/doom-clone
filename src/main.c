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
#include "game.h"
#include "world.h"
#include "config.h"
#include "utils.h"
#ifdef ENABLE_MULTIPLAYER
#include "net_client.h"
#endif
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What the command line asks for. With no server to join the game is the
 * single-player one and opens no socket. */
typedef struct {
    const char *connect; /* the server to join, NULL to play alone */
    uint16_t port;
} Options;

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--connect <address> [--port <n>]]\n"
            "  --connect <address>  join the server at <address> instead of playing alone\n"
            "  --port <n>           the server's port, %d by default\n",
            prog, NET_PORT_DEFAULT);
}

static int parse_options(int argc, char **argv, Options *o)
{
    o->connect = NULL;
    o->port = NET_PORT_DEFAULT;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--connect") != 0 && strcmp(arg, "--port") != 0) {
            fprintf(stderr, "Unknown argument '%s'\n", arg);
            return -1;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "%s needs a value\n", arg);
            return -1;
        }
        const char *value = argv[++i];
        if (strcmp(arg, "--connect") == 0) {
            o->connect = value;
            continue;
        }
        char *end;
        long port = strtol(value, &end, 10);
        if (end == value || *end != '\0' || port < 1 || port > 65535) {
            fprintf(stderr, "--port: '%s' is not a port number\n", value);
            return -1;
        }
        o->port = (uint16_t)port;
    }
#ifndef ENABLE_MULTIPLAYER
    if (o->connect) {
        fprintf(stderr, "--connect: this build has no network game (ENABLE_MULTIPLAYER=OFF)\n");
        return -1;
    }
#endif
    return 0;
}

#ifdef ENABLE_MULTIPLAYER
/* Join the server, or join it again, as a new player, once the last
 * connection has ended. Says why on stderr when it cannot even start. */
static void join(Client *c, const Options *o)
{
    ClientState st = client_state(c);
    if (st == CL_CONNECTING || st == CL_CONNECTED) {
        return;
    }
    if (client_connect(c, o->connect, o->port) != 0) {
        fprintf(stderr, "Cannot join %s: %s\n", o->connect, client_reason(c));
    }
}
#endif

/* The player this process looks through, NULL while it has no world to look
 * at: a client before its first snapshot. */
static PlayerState *local_player(World *w)
{
    return w ? &w->players[w->local_player] : NULL;
}

int main(int argc, char **argv)
{
    Options opt;
    if (parse_options(argc, argv, &opt) != 0) {
        usage(argv[0]);
        return 1;
    }

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

    /* The world this process looks at: its own in a single-player game; in a
     * network one the client's copy of the server's, which is not there until
     * the first snapshot comes. */
    World solo;
    World *world = NULL;
#ifdef ENABLE_MULTIPLAYER
    Client *client = NULL;
    if (opt.connect) {
        client = client_create();
        if (client) {
            join(client, &opt);
        }
        if (!client || client_state(client) == CL_REJECTED) {
            client_destroy(client);
            assets_shutdown(&assets);
            audio_shutdown(&audio);
            engine_shutdown(&eng);
            return 1;
        }
    }
#endif
    if (!opt.connect) {
        if (world_load(&solo, map_path) != 0) {
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
        solo.local_player = world_add_player(&solo, 0);
        world = &solo;
    }

    /* A client always looks through a player of its own; a dedicated server has
     * none, which is what local_player < 0 means, and renders nothing. */
    PlayerState *me = local_player(world);

    /* The client turns from wherever its player spawned facing: the angle in
     * every input is absolute, and a zero would spin the player round on the
     * first tick. A network client learns that angle from its first snapshot. */
    InputState input;
    input_init(&input);
    if (me) {
        input.angle = me->angle;
    }

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
            if (me) {
                const WeaponSystem *ws = &me->weapons;
                snprintf(title, sizeof(title), "doom-clone | %.0f FPS | HP %.0f | ammo %d/%d",
                         eng.fps, me->hp, ws->weapons[ws->current].ammo,
                         ws->weapons[ws->current].max_ammo);
            } else {
                snprintf(title, sizeof(title), "doom-clone | %.0f FPS", eng.fps);
            }
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
         * GSTATE_PLAYING for menu->start). A network game is the server's to
         * restart: all the client does is join again once it has been let go. */
        if (game.restart) {
#ifdef ENABLE_MULTIPLAYER
            if (client) {
                join(client, &opt);
                world = client_world(client);
            }
#endif
            if (!opt.connect) {
                if (world_load(&solo, map_path) != 0) {
                    fprintf(stderr, "Failed to reload map '%s'\n", map_path);
                    eng.running = 0;
                    break;
                }
                solo.local_player = world_add_player(&solo, 0);
            }
            me = local_player(world);
            game.restart = 0;
            input_init(&input);
            if (me) {
                input.angle = me->angle;
            }
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

        if (game.state == GSTATE_PLAYING && me) {
            /* Turning is the client's own, so it follows the frame rather than
             * the tick; each tick below is handed the angle as it stands. */
            input_turn(&input, frame_time);
            accumulator += frame_time;
            while (accumulator >= FIXED_DT) {
                PlayerInput pi = input_sample(&input, world->tick);
                accumulator -= FIXED_DT;
#ifdef ENABLE_MULTIPLAYER
                /* The server runs the tick; the client numbers the input. Its
                 * events come with the snapshots, and play after the poll. */
                if (client) {
                    client_send_input(client, &pi);
                    continue;
                }
#endif
                world_step(world, &pi, 1, FIXED_DT);
                audio_play_events(&audio, world);
            }

            /* Win/Lose transitions */
            if (me->hp <= 0.0f) {
                game.state = GSTATE_DEAD;
            } else if (enemy_all_dead(&world->enemies)) {
                game.state = GSTATE_WIN;
            }
        } else {
            /* Nothing is simulated in this state, so a press made in it has no
             * tick to go to: kept, it would fire on the first tick after. */
            input_clear_edges(&input);
            accumulator = 0.0;
        }
        input_end_frame(&input);

#ifdef ENABLE_MULTIPLAYER
        /* Polled in every state: the server's world goes on under the menu and
         * the pause, and a connection left unpolled times out. After the ticks,
         * so the inputs of this frame leave in it. */
        if (client) {
            ClientState was = client_state(client);
            client_poll(client);
            ClientState is = client_state(client);
            world = client_world(client);
            me = local_player(world);
            if (world) {
                audio_play_events(&audio, world);
            }
            if (is != was && is == CL_CONNECTED) {
                input.angle = me->angle;
                sprite_anim_init(&anim, &assets);
            } else if (is != was) {
                /* No screen says so yet: the reason goes to stderr and the
                 * game to the menu, where Enter joins again. */
                fprintf(stderr, "%s %s: %s\n",
                        is == CL_REJECTED ? "Cannot join" : "Disconnected from", opt.connect,
                        client_reason(client));
                game.state = GSTATE_MENU;
            }
        }
#endif

        game_update(&game, frame_time);

        /* Render world only if playing/paused/dead/win (not menu, where we
         * show a dim background instead). */
        if (game.state != GSTATE_MENU && me) {
            Camera cam;
            player_camera(me, &cam);
            raycast_render(&eng.fb, me, &cam, &world->map, &assets, &world->doors);
            /* A pause stops the animations with the world. Death and victory
             * stop only the world: the last enemy shot still falls, and
             * whoever killed the player finishes the shot. A network game is
             * not paused at all: its world goes on under the overlay. */
            int stopped = game.state == GSTATE_PAUSED && !opt.connect;
            sprite_anim_update(&anim, world, stopped ? 0.0f : (float)frame_time);
            sprites_build_from_world(&sprites, world, world->local_player, &anim, &assets);
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

#ifdef ENABLE_MULTIPLAYER
    client_destroy(client);
#endif
    assets_shutdown(&assets);
    audio_shutdown(&audio);
    engine_shutdown(&eng);
    return 0;
}
