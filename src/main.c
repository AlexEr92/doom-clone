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
#include "options.h"
#include "session.h"
#include "config.h"
#include "utils.h"
#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The player this process looks through, NULL while it has none: a client
 * before its first snapshot, a dedicated server always. */
static PlayerState *local_player(const GameSession *s)
{
    World *w = s->world(s->ctx);
    int id = s->local_player(s->ctx);
    return w && id >= 0 ? &w->players[id] : NULL;
}

/* ---- --host --dedicated: the server alone ---- */

static volatile sig_atomic_t stop_serving;

static void on_stop_signal(int sig)
{
    (void)sig;
    stop_serving = 1;
}

/* One second of a dedicated server's ticks, printed when it is over. */
typedef struct {
    double start; /* get_time_seconds() it began at */
    int ticks;
    double busy, worst, late_worst; /* seconds */
} ServerSecond;

static void server_second_print(const ServerSecond *sec, double t0, const World *w)
{
    int players = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        players += w->players[i].active;
    }
    printf("%8.2f s  %d ticks, %d players, tick mean %.3f ms worst %.3f ms, "
           "late worst %.3f ms\n",
           sec->start + 1.0 - t0, sec->ticks, players,
           sec->ticks ? sec->busy / sec->ticks * 1e3 : 0.0, sec->worst * 1e3,
           sec->late_worst * 1e3);
    fflush(stdout);
}

/* No window, no sound, no graphics: a machine with no display can serve, and
 * of assets/ only maps/ is read. The server ticks at TICK_RATE and prints
 * every player who joins or leaves and, once a second, how many ticks that
 * second held and what they cost — mean and worst time in a tick, and how
 * late the latest one started (requirements.md, P4). SIGINT or SIGTERM stops
 * it, and every client is told the game is closed. */
static int serve(const Options *o, const char *map_path)
{
    /* SDL for its clock alone, which needs no display. */
    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    GameSession sess;
    if (session_open(&sess, o, map_path) != 0) {
        SDL_Quit();
        return 1;
    }
    signal(SIGINT, on_stop_signal);
    signal(SIGTERM, on_stop_signal);
    printf("serving %s on port %u, %d snapshots a second\n", map_path, (unsigned)o->port,
           o->snapshot_rate);
    fflush(stdout);

    const World *w = sess.world(sess.ctx);
    int present[MAX_PLAYERS] = {0};
    double t0 = get_time_seconds(), start = t0;
    ServerSecond sec = {t0, 0, 0.0, 0.0, 0.0};
    /* Each tick is due at its own multiple of the step, so the rate does not
     * drift; one more than a quarter second late, the schedule starts again
     * rather than catch up in a burst. */
    for (uint64_t ticks = 0; !stop_serving; ticks++) {
        double due = start + (double)ticks * FIXED_DT;
        double now = get_time_seconds();
        if (now - due > 0.25) {
            start = now - (double)ticks * FIXED_DT;
            due = now;
        }
        /* SDL_Delay() sleeps whole milliseconds, so it is asked for the whole
         * ones left and the last fraction is waited out here. */
        while (now < due && !stop_serving) {
            SDL_Delay((Uint32)((due - now) * 1000.0));
            now = get_time_seconds();
        }
        double begin = get_time_seconds();
        sess.tick(sess.ctx, NULL, FIXED_DT);
        double took = get_time_seconds() - begin;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (w->players[i].active != present[i]) {
                present[i] = w->players[i].active;
                printf("%8.2f s  player %d %s\n", get_time_seconds() - t0, i,
                       present[i] ? "joined" : "left");
                fflush(stdout);
            }
        }

        if (begin - sec.start >= 1.0) {
            server_second_print(&sec, t0, w);
            sec = (ServerSecond){sec.start + 1.0, 0, 0.0, 0.0, 0.0};
        }
        sec.ticks++;
        sec.busy += took;
        sec.worst = took > sec.worst ? took : sec.worst;
        double late = begin > due ? begin - due : 0.0;
        sec.late_worst = late > sec.late_worst ? late : sec.late_worst;
    }

    sess.close(sess.ctx);
    printf("stopped\n");
    SDL_Quit();
    return 0;
}

int main(int argc, char **argv)
{
    Options opt;
    int parsed = parse_options(argc, argv, &opt);
    if (parsed != 0) {
        options_usage(parsed < 0 ? stderr : stdout, argv[0]);
        return parsed < 0 ? 1 : 0;
    }

    const char *map_path = "assets/maps/level1.txt";
    if (opt.mode == LAUNCH_DEDICATED) {
        return serve(&opt, map_path);
    }

    /* Before the window: a map that does not load, a port that is taken or an
     * address that does not resolve is said on stderr with nothing opened. */
    GameSession sess;
    if (session_open(&sess, &opt, map_path) != 0) {
        return 1;
    }

    Engine eng;
    if (engine_init(&eng) != 0) {
        fprintf(stderr, "Engine init failed\n");
        sess.close(sess.ctx);
        return 1;
    }

    Assets assets;
    if (assets_init(&assets) != 0) {
        fprintf(stderr, "Assets init failed\n");
        sess.close(sess.ctx);
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
     * network one the server's — here, or the client's copy of it, which is not
     * there until the first snapshot comes. */
    World *world = sess.world(sess.ctx);
    PlayerState *me = local_player(&sess);

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
    HudFace face;
    hud_face_init(&face);

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

        /* Restart requested: a fresh world in a game of one's own, the same one
         * in a network game, which a client joins again if it has been let go.
         * The FSM state is already set by the event handler (GSTATE_MENU for
         * pause/dead/win, or GSTATE_PLAYING for menu->start). */
        if (game.restart) {
            if (sess.restart(sess.ctx) != 0) {
                eng.running = 0;
                break;
            }
            world = sess.world(sess.ctx);
            me = local_player(&sess);
            game.restart = 0;
            input_init(&input);
            if (me) {
                input.angle = me->angle;
            }
            sprite_anim_init(&anim, &assets);
            hud_face_init(&face);
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

        int playing = game.state == GSTATE_PLAYING && me;
        if (playing) {
            /* Turning is the client's own, so it follows the frame rather than
             * the tick; each tick below is handed the angle as it stands. */
            input_turn(&input, frame_time);
        } else {
            /* The player is in no tick in this state, so a press made in it has
             * nowhere to go: kept, it would fire on the first tick after. */
            input_clear_edges(&input);
        }
        /* A world of one's own stops with the player; one shared with others
         * goes on, with nobody playing at this machine. */
        if (playing || sess.shared) {
            accumulator += frame_time;
            while (accumulator >= FIXED_DT) {
                PlayerInput pi;
                if (playing) {
                    pi = input_sample(&input, world->tick);
                }
                accumulator -= FIXED_DT;
                sess.tick(sess.ctx, playing ? &pi : NULL, FIXED_DT);
                /* A remote world's events come with the snapshots, and play
                 * after the poll. */
                if (!sess.remote) {
                    audio_play_events(&audio, world);
                }
            }
        } else {
            accumulator = 0.0;
        }

        /* Win/Lose transitions */
        if (playing) {
            if (me->hp <= 0.0f) {
                game.state = GSTATE_DEAD;
            } else if (enemy_all_dead(&world->enemies)) {
                game.state = GSTATE_WIN;
            }
        }
        input_end_frame(&input);

        /* Polled in every state: a server's world goes on under the menu and
         * the pause, and a connection left unpolled times out. After the ticks,
         * so the inputs of this frame leave in it. */
        SessionChange change = sess.poll(sess.ctx);
        world = sess.world(sess.ctx);
        me = local_player(&sess);
        if (sess.remote && world) {
            audio_play_events(&audio, world);
        }
        if (change == SESS_JOINED) {
            input.angle = me->angle;
            sprite_anim_init(&anim, &assets);
            hud_face_init(&face);
        } else if (change == SESS_LOST) {
            /* The menu, where Enter joins again. */
            game.state = GSTATE_MENU;
        }

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
            int stopped = game.state == GSTATE_PAUSED && !sess.shared;
            float anim_dt = stopped ? 0.0f : (float)frame_time;
            /* Characters as the session shows them, which for a network
             * client is a moment behind the newest snapshot; the camera, the
             * walls and the HUD stay with the world itself. */
            const World *view = sess.view(sess.ctx);
            sprite_anim_update(&anim, view, anim_dt);
            sprites_build_from_world(&sprites, view, view->local_player, &anim, &assets);
            sprite_render(&eng.fb, &sprites, me, &cam, &assets);
            const WeaponSystem *ws = &me->weapons;
            hud_draw_weapon(&eng.fb, &assets.weapon_fp[ws->current], ws->weapons[ws->current].anim);
            hud_face_update(&face, me, anim_dt);
            hud_draw(&eng.fb, me, &face, &assets);
        } else {
            fb_clear(&eng.fb, make_color(15, 10, 20));
        }

        game_draw_overlay(&eng.fb, &game, me);
        engine_present(&eng);
    }

    sess.close(sess.ctx);
    assets_shutdown(&assets);
    audio_shutdown(&audio);
    engine_shutdown(&eng);
    return 0;
}
