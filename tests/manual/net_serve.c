/* net_server.c as a process of its own, run by hand from the project root:
 *
 *   net_serve [--stats] [map] [port]   serve map (assets/maps/level1.txt)
 *                                      on port (27015)
 *
 * The server two `doom-clone --connect 127.0.0.1` play on until the game can
 * host one itself (07-07). It ticks at TICK_RATE and prints every player who
 * joins or leaves the world; with --stats, once a second also how many ticks
 * that second held and what they cost — mean and worst time in server_tick(),
 * and how late the latest one started (requirements.md, P4). Ctrl+C lets the
 * clients go at once; kill -9 leaves them to find out by their timeout. Never
 * ends by itself, which is why ctest does not run it; the unit tests are
 * test_net_server.c and test_net_client.c. */

#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L /* clock_gettime() and nanosleep() under -std=c11 */
#endif

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
#include "net_server.h"
#include "world.h"

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

static void sleep_us(uint64_t us)
{
#ifdef _WIN32
    Sleep((DWORD)(us / 1000));
#else
    struct timespec ts = {(time_t)(us / 1000000), (long)(us % 1000000) * 1000L};
    nanosleep(&ts, NULL);
#endif
}

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
    (void)sig;
    stop = 1;
}

/* One second of ticks, for --stats, printed under the time it ended at. */
typedef struct {
    uint64_t start; /* now_us() it began at */
    int ticks;
    uint64_t busy_us, worst_us, late_worst_us;
} Second;

static void second_print(const Second *sec, uint64_t t0, const World *w)
{
    int players = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        players += w->players[i].active;
    }
    printf("%8.2f s  %d ticks, %d players, tick mean %.3f ms worst %.3f ms, "
           "late worst %.3f ms\n",
           (double)(sec->start + 1000000u - t0) / 1e6, sec->ticks, players,
           sec->ticks ? (double)sec->busy_us / (double)sec->ticks / 1e3 : 0.0,
           (double)sec->worst_us / 1e3, (double)sec->late_worst_us / 1e3);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int stats = argc > 1 && strcmp(argv[1], "--stats") == 0;
    if (stats) {
        argc--;
        argv++;
    }
    const char *map = argc > 1 ? argv[1] : "assets/maps/level1.txt";
    int port = argc > 2 ? atoi(argv[2]) : NET_PORT_DEFAULT;
    Server *s = port > 0 && port <= 65535 ? server_create((uint16_t)port, map) : NULL;
    if (!s) {
        fprintf(stderr, "cannot serve %s on port %d\n", map, port);
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    printf("serving %s on port %d\n", map, port);
    fflush(stdout);

    const World *w = server_world(s);
    int present[MAX_PLAYERS] = {0};
    uint64_t t0 = now_us(), start = t0;
    Second sec = {t0, 0, 0, 0, 0};
    /* Each tick is due at its own multiple of the step, so the rate does not
     * drift; one more than a quarter second late, the schedule starts again
     * rather than catch up in a burst. */
    for (uint64_t ticks = 0; !stop; ticks++) {
        uint64_t due = start + ticks * 1000000u / TICK_RATE;
        uint64_t now = now_us();
        if (due > now) {
            sleep_us(due - now);
        } else if (now - due > 250000u) {
            start = now - ticks * 1000000u / TICK_RATE;
            due = now;
        }
        uint64_t begin = now_us();
        server_tick(s, NULL, FIXED_DT);
        uint64_t took = now_us() - begin;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (w->players[i].active != present[i]) {
                present[i] = w->players[i].active;
                printf("%8.2f s  player %d %s\n", (double)(now_us() - t0) / 1e6, i,
                       present[i] ? "joined" : "left");
                fflush(stdout);
            }
        }

        if (!stats) {
            continue;
        }
        if (begin - sec.start >= 1000000u) {
            second_print(&sec, t0, w);
            sec = (Second){sec.start + 1000000u, 0, 0, 0, 0};
        }
        sec.ticks++;
        sec.busy_us += took;
        sec.worst_us = took > sec.worst_us ? took : sec.worst_us;
        uint64_t late = begin > due ? begin - due : 0;
        sec.late_worst_us = late > sec.late_worst_us ? late : sec.late_worst_us;
    }

    server_destroy(s);
    printf("stopped\n");
    return 0;
}
