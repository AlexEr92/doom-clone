/* net_transport.c between two processes, run by hand:
 *
 *   net_echo server [port]          echo every packet back to its sender
 *   net_echo client [addr] [port]   send, and print what comes back
 *
 * The client sends a reliable message once a second and an unreliable one
 * every tick, from a 60 Hz loop like the game's, and once a second prints
 * the reliable round trip, how many unreliable echoes came back, the loop
 * rate and the slowest net_poll(). Both sides timestamp every connect and
 * disconnect, so a peer killed with kill -9 shows on the other side how long
 * the timeout took. Neither ends by itself, which is why ctest does not run
 * it; the unit tests are test_net_transport.c. */

#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L /* clock_gettime() and nanosleep() under -std=c11 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "config.h"
#include "net_transport.h"

#define TICK_MS (1000 / TICK_RATE)

/* Microseconds on a monotonic clock: a poll takes a few of them, so the
 * milliseconds ENet counts in would show only zeros and ones. */
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

static void sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

static uint64_t start_us;

static double now_s(void)
{
    return (double)(now_us() - start_us) / 1e6;
}

/* 'R' for reliable, 'U' for unreliable: the server echoes each the same way. */
typedef struct {
    char kind;
    uint32_t seq;
    uint64_t sent_us;
} Message;

static int run_server(uint16_t port)
{
    NetHost *h = net_host_server(port, NET_MAX_PEERS);
    if (!h) {
        fprintf(stderr, "cannot listen on port %u\n", port);
        return 1;
    }
    printf("[%8.3f] listening on port %u\n", now_s(), port);
    fflush(stdout);
    for (;;) {
        NetTransportEvent ev;
        while (net_poll(h, &ev)) {
            if (ev.type == NET_EV_CONNECT) {
                printf("[%8.3f] peer %d connected\n", now_s(), ev.peer);
            } else if (ev.type == NET_EV_DISCONNECT) {
                printf("[%8.3f] peer %d disconnected\n", now_s(), ev.peer);
            } else if (ev.type == NET_EV_DATA && ev.size == sizeof(Message)) {
                int reliable = ((const Message *)ev.data)->kind == 'R';
                net_send(h, ev.peer, reliable ? NET_CH_RELIABLE : NET_CH_UNRELIABLE, ev.data,
                         ev.size, reliable);
            }
            fflush(stdout);
        }
        sleep_ms(TICK_MS);
    }
}

static int run_client(const char *addr, uint16_t port)
{
    NetHost *h = net_host_client();
    NetPeerId server = h ? net_connect(h, addr, port) : NET_PEER_NONE;
    if (server == NET_PEER_NONE) {
        fprintf(stderr, "cannot connect to %s:%u\n", addr, port);
        return 1;
    }
    printf("[%8.3f] connecting to %s:%u\n", now_s(), addr, port);
    fflush(stdout);

    int connected = 0;
    uint32_t seq = 0, loops = 0, unreliable_back = 0;
    uint64_t slowest_poll = 0, next_report = now_us() + 1000000;
    for (;;) {
        uint64_t poll_start = now_us();
        NetTransportEvent ev;
        while (net_poll(h, &ev)) {
            if (ev.type == NET_EV_CONNECT) {
                printf("[%8.3f] connected\n", now_s());
                connected = 1;
            } else if (ev.type == NET_EV_DISCONNECT) {
                printf("[%8.3f] disconnected\n", now_s());
                net_destroy(h);
                return 1;
            } else if (ev.type == NET_EV_DATA && ev.size == sizeof(Message)) {
                const Message *m = ev.data;
                if (m->kind == 'R') {
                    printf("[%8.3f] echo %u back in %.1f ms, rtt %.0f ms, loss %.1f%%\n", now_s(),
                           m->seq, (double)(now_us() - m->sent_us) / 1000.0,
                           net_peer_rtt(h, server), 100.0 * net_peer_loss(h, server));
                } else {
                    unreliable_back++;
                }
            }
            fflush(stdout);
        }
        uint64_t poll_time = now_us() - poll_start;
        slowest_poll = poll_time > slowest_poll ? poll_time : slowest_poll;
        loops++;

        if (connected) {
            Message m = {'U', loops, now_us()};
            net_send(h, server, NET_CH_UNRELIABLE, &m, sizeof(m), 0);
        }
        if (now_us() >= next_report) {
            if (connected) {
                Message m = {'R', ++seq, now_us()};
                net_send(h, server, NET_CH_RELIABLE, &m, sizeof(m), 1);
            }
            printf("[%8.3f] loop %u Hz, slowest poll %u us, %u unreliable echoes\n", now_s(), loops,
                   (unsigned)slowest_poll, unreliable_back);
            fflush(stdout);
            loops = unreliable_back = 0;
            slowest_poll = 0;
            next_report += 1000000;
        }
        sleep_ms(TICK_MS);
    }
}

int main(int argc, char **argv)
{
    start_us = now_us();
    if (argc >= 2 && strcmp(argv[1], "server") == 0) {
        return run_server(argc >= 3 ? (uint16_t)atoi(argv[2]) : NET_PORT_DEFAULT);
    }
    if (argc >= 2 && strcmp(argv[1], "client") == 0) {
        return run_client(argc >= 3 ? argv[2] : "127.0.0.1",
                          argc >= 4 ? (uint16_t)atoi(argv[3]) : NET_PORT_DEFAULT);
    }
    fprintf(stderr, "usage: net_echo server [port]\n"
                    "       net_echo client [addr] [port]\n");
    return 2;
}
