/* Players with no window, over net_client.h, run by hand from the project
 * root (the map MSG_ACCEPT names is loaded from assets/maps/, as the game
 * loads it):
 *
 *   net_bot [--addr A] [--port N] look [--rate SECONDS]
 *   net_bot [--addr A] [--port N] run STEP...
 *   net_bot [--addr A] [--port N] swarm COUNT [SECONDS]
 *
 * look   joins, and prints the world of its first snapshot: a line an
 *        entity, for grep and awk. With --rate it watches for SECONDS first
 *        and adds what a second brought: ticks, snapshots, payload.
 * run    plays the steps in order, then prints the world as look does:
 *          go X Y     walk to the point, steering by its own world
 *          face X Y   turn towards the point
 *          kill N     aim at enemy N and fire until it is dead
 *          use        press E once
 *          fire       fire once
 *          wait S     stand for S seconds
 *        A step that does not finish within STEP_TIMEOUT, or that the bot
 *        dies in, ends the run with status 1 and names the step.
 * swarm  COUNT players in one process walk about and fire, for load. Once a
 *        second it prints how many are in and what one of them sends and
 *        receives, until SECONDS have passed (0: until Ctrl+C). Status 1 when
 *        any was turned away or lost.
 *
 * The address is 127.0.0.1 and the port NET_PORT_DEFAULT unless given. Each
 * bot is a real player: it takes a slot, and enemies go for it — there is no
 * spectator in the protocol. Status 2 is a command line it does not read.
 * Never ends in ctest: see tests/CMakeLists.txt. */

#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L /* clock_gettime() and nanosleep() under -std=c11 */
#endif

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "config.h"
#include "net_client.h"
#include "utils.h"
#include "world.h"

#define STEP_TIMEOUT 20.0 /* seconds a step may take */
#define ARRIVED 0.2f      /* cells from the point that count as there */
#define MAX_SWARM 32

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
    (void)sig;
    stop = 1;
}

static uint64_t now_us(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (uint64_t)(count.QuadPart / freq.QuadPart * 1000000 +
                      count.QuadPart % freq.QuadPart * 1000000 / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
#endif
}

static double now_s(void)
{
    return (double)now_us() / 1e6;
}

/* Sleep until the next tick of a 60 Hz schedule that started at the first
 * call: each tick is due at its own multiple of the step, as in serve() of
 * main.c, the dedicated server. */
static void pace(void)
{
    static uint64_t start, ticks;
    if (ticks == 0) {
        start = now_us();
    }
    uint64_t due = start + ++ticks * 1000000u / TICK_RATE;
    uint64_t now = now_us();
    if (due <= now) {
        return;
    }
#ifdef _WIN32
    Sleep((DWORD)((due - now) / 1000));
#else
    struct timespec ts = {(time_t)((due - now) / 1000000), (long)((due - now) % 1000000) * 1000L};
    nanosleep(&ts, NULL);
#endif
}

/* ---- one bot ---- */

typedef struct {
    Client *c;
    float angle; /* where it faces, sent with every input */
} Bot;

static const char *addr = "127.0.0.1";
static uint16_t port = NET_PORT_DEFAULT;

static const PlayerState *bot_me(Bot *b)
{
    const World *w = client_world(b->c);
    return &w->players[w->local_player];
}

static void bot_send(Bot *b, uint16_t buttons)
{
    PlayerInput in = {0, buttons, b->angle};
    client_send_input(b->c, &in);
}

/* Start joining; client_poll() carries it on. */
static int bot_connect(Bot *b)
{
    b->c = client_create();
    if (!b->c) {
        fprintf(stderr, "out of memory\n");
        return -1;
    }
    if (client_connect(b->c, addr, port, "bot") != 0) {
        fprintf(stderr, "cannot join %s:%u: %s\n", addr, (unsigned)port, client_reason(b->c));
        return -1;
    }
    return 0;
}

/* Join and wait for the first snapshot, which client_poll() gives up on after
 * NET_CONNECT_TIMEOUT_MS. */
static int bot_join(Bot *b)
{
    if (bot_connect(b) != 0) {
        return -1;
    }
    while (client_state(b->c) == CL_CONNECTING && !stop) {
        client_poll(b->c);
        pace();
    }
    if (client_state(b->c) != CL_CONNECTED) {
        fprintf(stderr, "cannot join %s:%u: %s\n", addr, (unsigned)port,
                stop ? "interrupted" : client_reason(b->c));
        return -1;
    }
    b->angle = bot_me(b)->angle;
    return 0;
}

static const char *name_of(const char *const *names, int count, int i)
{
    return i >= 0 && i < count ? names[i] : "?";
}

static void print_world(const World *w)
{
    static const char *const enemy_type[] = {"imp", "serg"};
    static const char *const enemy_state[] = {"idle", "alert", "chase", "attack", "dead"};
    static const char *const door_state[] = {"closed", "opening", "open", "closing"};
    static const char *const item_type[] = {"none", "medkit", "ammo", "armor"};

    printf("tick %u\n", w->tick);
    printf("me %d\n", w->local_player);
    for (int i = 0; i < w->player_count; i++) {
        const PlayerState *p = &w->players[i];
        printf("player %d active %d alive %d x %.2f y %.2f angle %.2f hp %.0f armor %.0f\n", i,
               p->active, p->alive, p->x, p->y, p->angle, p->hp, p->armor);
    }
    for (int i = 0; i < w->enemies.count; i++) {
        const Enemy *e = &w->enemies.items[i];
        printf("enemy %d type %s state %s x %.2f y %.2f hp %.0f\n", i,
               name_of(enemy_type, 2, e->type), name_of(enemy_state, 5, (int)e->state), e->x, e->y,
               e->hp);
    }
    for (int i = 0; i < w->doors.count; i++) {
        const Door *d = &w->doors.doors[i];
        printf("door %d cell %d %d state %s openness %.2f\n", i, d->cellx, d->celly,
               name_of(door_state, 4, (int)d->state), d->openness);
    }
    for (int i = 0; i < w->items.count; i++) {
        const Item *it = &w->items.items[i];
        printf("item %d type %s active %d x %.2f y %.2f\n", i, name_of(item_type, 4, (int)it->type),
               it->active, it->x, it->y);
    }
    fflush(stdout);
}

/* ---- look ---- */

static int run_look(double seconds)
{
    Bot b;
    if (bot_join(&b) != 0) {
        client_destroy(b.c);
        return 1;
    }
    if (seconds > 0.0) {
        uint32_t tick0 = client_world(b.c)->tick;
        ClientStats s0 = client_stats(b.c);
        double t0 = now_s();
        while (now_s() - t0 < seconds && !stop && client_state(b.c) == CL_CONNECTED) {
            bot_send(&b, 0);
            client_poll(b.c);
            pace();
        }
        double t = now_s() - t0;
        ClientStats s = client_stats(b.c);
        printf("rate %.1f ticks/s, %.1f snapshots/s, %.2f KB/s down, %.2f KB/s up\n",
               (double)(client_world(b.c)->tick - tick0) / t,
               (double)(s.snapshots - s0.snapshots) / t,
               (double)(s.bytes_in - s0.bytes_in) / t / 1024.0,
               (double)(s.bytes_out - s0.bytes_out) / t / 1024.0);
    }
    int ok = client_state(b.c) == CL_CONNECTED;
    if (ok) {
        print_world(client_world(b.c));
    } else {
        fprintf(stderr, "left the game: %s\n", stop ? "interrupted" : client_reason(b.c));
    }
    client_destroy(b.c);
    return ok ? 0 : 1;
}

/* ---- run ---- */

typedef enum { STEP_GO, STEP_FACE, STEP_KILL, STEP_USE, STEP_FIRE, STEP_WAIT } StepKind;

typedef struct {
    StepKind kind;
    float x, y; /* go, face: the point; wait: x is the seconds */
    int enemy;  /* kill */
    const char *text;
} Step;

static float angle_to(const PlayerState *p, float x, float y)
{
    return atan2f(y - p->y, x - p->x);
}

/* What the bot does this tick for step `s`, and whether the step is done. */
static int step_tick(Bot *b, const Step *s, double since)
{
    const World *w = client_world(b->c);
    const PlayerState *me = bot_me(b);
    switch (s->kind) {
        case STEP_GO: {
            float dx = s->x - me->x, dy = s->y - me->y;
            if (dx * dx + dy * dy <= ARRIVED * ARRIVED) {
                bot_send(b, 0);
                return 1;
            }
            b->angle = angle_to(me, s->x, s->y);
            bot_send(b, BTN_FORWARD);
            return 0;
        }
        case STEP_FACE:
            b->angle = angle_to(me, s->x, s->y);
            bot_send(b, 0);
            return fabsf(wrap_angle(me->angle - b->angle)) < 0.01f;
        case STEP_KILL: {
            const Enemy *e = &w->enemies.items[s->enemy];
            if (e->state == ESTATE_DEAD) {
                bot_send(b, 0);
                return 1;
            }
            b->angle = angle_to(me, e->x, e->y);
            bot_send(b, BTN_FIRE);
            return 0;
        }
        case STEP_USE:
        case STEP_FIRE:
            /* One press, then a quarter of a second for the server to act on
             * it and for the snapshot that shows it. */
            bot_send(b, since == 0.0 ? (s->kind == STEP_USE ? BTN_USE : BTN_FIRE) : 0);
            return since >= 0.25;
        case STEP_WAIT: bot_send(b, 0); return since >= s->x;
    }
    return 1;
}

static int run_steps(const Step *steps, int count)
{
    Bot b;
    if (bot_join(&b) != 0) {
        client_destroy(b.c);
        return 1;
    }
    const char *failed = NULL, *why = NULL;
    for (int i = 0; i < count && !failed; i++) {
        const Step *s = &steps[i];
        if (s->kind == STEP_KILL && s->enemy >= client_world(b.c)->enemies.count) {
            failed = s->text;
            why = "no such enemy";
            break;
        }
        double t0 = now_s();
        for (int first = 1;; first = 0) {
            if (stop) {
                failed = s->text;
                why = "interrupted";
                break;
            }
            if (client_state(b.c) != CL_CONNECTED) {
                failed = s->text;
                why = client_reason(b.c);
                break;
            }
            if (bot_me(&b)->hp <= 0.0f) {
                failed = s->text;
                why = "the bot is dead";
                break;
            }
            if (step_tick(&b, s, first ? 0.0 : now_s() - t0)) {
                break;
            }
            if (now_s() - t0 > STEP_TIMEOUT) {
                failed = s->text;
                why = "timed out";
                break;
            }
            client_poll(b.c);
            pace();
        }
        if (!failed) {
            printf("done %s\n", s->text);
            fflush(stdout);
        }
    }
    if (client_state(b.c) == CL_CONNECTED) {
        print_world(client_world(b.c));
    }
    if (failed) {
        fprintf(stderr, "step '%s' failed: %s\n", failed, why);
    }
    client_destroy(b.c);
    return failed ? 1 : 0;
}

/* Read the steps from the command line into `steps`, each with its text for
 * the messages. Returns how many, or -1 at one it cannot read. */
static int parse_steps(int argc, char **argv, Step *steps, char texts[][48])
{
    int n = 0;
    for (int i = 0; i < argc; i++, n++) {
        Step *s = &steps[n];
        const char *word = argv[i];
        int args = 0;
        memset(s, 0, sizeof(*s));
        if (strcmp(word, "go") == 0 || strcmp(word, "face") == 0) {
            s->kind = word[0] == 'g' ? STEP_GO : STEP_FACE;
            args = 2;
        } else if (strcmp(word, "kill") == 0) {
            s->kind = STEP_KILL;
            args = 1;
        } else if (strcmp(word, "use") == 0) {
            s->kind = STEP_USE;
        } else if (strcmp(word, "fire") == 0) {
            s->kind = STEP_FIRE;
        } else if (strcmp(word, "wait") == 0) {
            s->kind = STEP_WAIT;
            args = 1;
        } else {
            fprintf(stderr, "unknown step '%s'\n", word);
            return -1;
        }
        if (i + args >= argc) {
            fprintf(stderr, "step '%s' needs %d number%s\n", word, args, args > 1 ? "s" : "");
            return -1;
        }
        float v[2] = {0.0f, 0.0f};
        for (int a = 0; a < args; a++) {
            char *end;
            v[a] = strtof(argv[i + 1 + a], &end);
            if (end == argv[i + 1 + a] || *end != '\0' || !isfinite(v[a]) || v[a] < 0.0f) {
                fprintf(stderr, "step '%s': '%s' is not a number\n", word, argv[i + 1 + a]);
                return -1;
            }
        }
        if (s->kind == STEP_KILL && (v[0] >= (float)MAX_ENEMIES || v[0] != floorf(v[0]))) {
            fprintf(stderr, "step 'kill': '%s' is not an enemy number\n", argv[i + 1]);
            return -1;
        }
        s->x = v[0];
        s->y = v[1];
        s->enemy = (int)v[0];
        snprintf(texts[n], 48, "%s", word);
        for (int a = 0; a < args; a++) {
            size_t len = strlen(texts[n]);
            snprintf(texts[n] + len, 48 - len, " %s", argv[i + 1 + a]);
        }
        s->text = texts[n];
        i += args;
    }
    return n;
}

/* ---- swarm ---- */

static int run_swarm(int count, double seconds)
{
    static Bot bots[MAX_SWARM];
    int in = 0;
    for (int i = 0; i < count; i++) {
        if (bot_connect(&bots[i]) != 0) {
            client_destroy(bots[i].c);
            bots[i].c = NULL;
        }
    }
    for (int waiting = 1; waiting && !stop;) {
        waiting = 0;
        for (int i = 0; i < count; i++) {
            if (bots[i].c) {
                client_poll(bots[i].c);
                waiting |= client_state(bots[i].c) == CL_CONNECTING;
            }
        }
        pace();
    }
    int rejected = 0;
    for (int i = 0; i < count; i++) {
        if (bots[i].c && client_state(bots[i].c) == CL_CONNECTED) {
            bots[i].angle = bot_me(&bots[i])->angle;
            printf("bot %d is player %d\n", i, client_local_player(bots[i].c));
            in++;
        } else {
            if (bots[i].c) {
                printf("bot %d cannot join: %s\n", i, client_reason(bots[i].c));
            }
            client_destroy(bots[i].c);
            bots[i].c = NULL;
            rejected++;
        }
    }
    fflush(stdout);

    /* Each turns at its own rate and fires about once a second, all at
     * different ticks, so the load is not one burst. */
    ClientStats before[MAX_SWARM];
    for (int i = 0; i < count; i++) {
        before[i] = bots[i].c ? client_stats(bots[i].c) : (ClientStats){0, 0, 0};
    }
    double t0 = now_s(), second = t0;
    for (uint32_t tick = 0; in > 0 && !stop && (seconds <= 0.0 || now_s() - t0 < seconds); tick++) {
        for (int i = 0; i < count; i++) {
            Bot *b = &bots[i];
            if (!b->c || client_state(b->c) != CL_CONNECTED) {
                continue;
            }
            b->angle += (0.3f + 0.1f * (float)(i % 5)) * (i % 2 ? 1.0f : -1.0f) / (float)TICK_RATE;
            uint16_t buttons = BTN_FORWARD;
            if ((tick + (uint32_t)i * 7) % TICK_RATE == 0) {
                buttons |= BTN_FIRE;
            }
            bot_send(b, buttons);
            client_poll(b->c);
        }
        if (now_s() - second >= 1.0) {
            double t = now_s() - second;
            int now_in = 0;
            uint64_t snaps = 0, down = 0, up = 0;
            for (int i = 0; i < count; i++) {
                if (!bots[i].c || client_state(bots[i].c) != CL_CONNECTED) {
                    continue;
                }
                ClientStats s = client_stats(bots[i].c);
                snaps += s.snapshots - before[i].snapshots;
                down += s.bytes_in - before[i].bytes_in;
                up += s.bytes_out - before[i].bytes_out;
                before[i] = s;
                now_in++;
            }
            double per = now_in > 0 ? (double)now_in * t : t;
            printf("%7.1f s  %d of %d in; a player: %.1f snapshots/s, %.2f KB/s down, "
                   "%.2f KB/s up; all: %.1f KB/s down\n",
                   now_s() - t0, now_in, count, (double)snaps / per, (double)down / per / 1024.0,
                   (double)up / per / 1024.0, (double)down / t / 1024.0);
            fflush(stdout);
            second = now_s();
        }
        pace();
    }

    int stayed = 0;
    for (int i = 0; i < count; i++) {
        if (bots[i].c && client_state(bots[i].c) == CL_CONNECTED) {
            stayed++;
        } else if (bots[i].c) {
            printf("bot %d left the game: %s\n", i, client_reason(bots[i].c));
        }
        client_destroy(bots[i].c);
    }
    printf("%d of the %d who joined stayed in the game, %d could not join\n", stayed, in, rejected);
    return rejected == 0 && stayed == in ? 0 : 1;
}

/* ---- command line ---- */

static int usage(void)
{
    fprintf(stderr, "usage: net_bot [--addr A] [--port N] look [--rate SECONDS]\n"
                    "       net_bot [--addr A] [--port N] run STEP...\n"
                    "       net_bot [--addr A] [--port N] swarm COUNT [SECONDS]\n"
                    "steps: go X Y | face X Y | kill N | use | fire | wait S\n");
    return 2;
}

int main(int argc, char **argv)
{
    int i = 1;
    for (; i + 1 < argc && strncmp(argv[i], "--", 2) == 0; i += 2) {
        if (strcmp(argv[i], "--addr") == 0) {
            addr = argv[i + 1];
        } else if (strcmp(argv[i], "--port") == 0) {
            long p = strtol(argv[i + 1], NULL, 10);
            if (p < 1 || p > 65535) {
                return usage();
            }
            port = (uint16_t)p;
        } else {
            return usage();
        }
    }
    if (i >= argc) {
        return usage();
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    const char *mode = argv[i++];
    if (strcmp(mode, "look") == 0) {
        double seconds = 0.0;
        if (i < argc && (strcmp(argv[i], "--rate") != 0 || i + 2 != argc ||
                         (seconds = strtod(argv[i + 1], NULL)) <= 0.0)) {
            return usage();
        }
        return run_look(seconds);
    }
    if (strcmp(mode, "run") == 0) {
        int n = argc - i;
        Step *steps = calloc((size_t)(n > 0 ? n : 1), sizeof(*steps));
        char(*texts)[48] = calloc((size_t)(n > 0 ? n : 1), sizeof(*texts));
        int count = steps && texts ? parse_steps(n, argv + i, steps, texts) : -1;
        int status = count > 0 ? run_steps(steps, count) : usage();
        free(steps);
        free(texts);
        return status;
    }
    if (strcmp(mode, "swarm") == 0) {
        int count = i < argc ? atoi(argv[i]) : 0;
        double seconds = i + 1 < argc ? strtod(argv[i + 1], NULL) : 0.0;
        if (count < 1 || count > MAX_SWARM || seconds < 0.0) {
            return usage();
        }
        return run_swarm(count, seconds);
    }
    return usage();
}
