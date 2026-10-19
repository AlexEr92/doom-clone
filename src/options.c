#include "options.h"
#include "config.h"
#include "map.h"          /* MAP_DIR; a header alone, nothing of it is linked */
#include "net_protocol.h" /* NET_NAME_MAX, NET_MAP_MAX; likewise */
#include <stdlib.h>
#include <string.h>

void options_usage(FILE *f, const char *prog)
{
    fprintf(f,
            "usage: %s [--map <file>] [--host [--dedicated] [--snaprate <n>] | --connect "
            "<address> [--name <nick>]] [--port <n>]\n"
            "  (no flags)           play alone; no socket is opened\n"
            "  --host               serve a network game and play in it\n"
            "  --dedicated          with --host: serve it with no window and no player\n"
            "  --connect <address>  join the server at <address>\n"
            "  --port <n>           the port to serve on or to join, %d by default\n"
            "  --name <nick>        with --connect: the name to join under, up to %d characters\n"
            "  --snaprate <n>       with --host: snapshots a second, %d to %d, %d by default\n"
            "  --map <file>         the map in %s to play or serve, %s alone and %s\n"
            "                       on a server by default; a client plays the server's\n"
            "  -h, --help           show this and exit\n",
            prog, NET_PORT_DEFAULT, NET_NAME_MAX - 1, SNAPSHOT_RATE_MIN, TICK_RATE, SNAPSHOT_RATE,
            MAP_DIR, OPT_MAP_SOLO, OPT_MAP_SERVER);
}

/* A whole decimal number in lo .. hi, and nothing else: no sign, no spaces, no
 * trailing letters. */
static int parse_int(const char *s, long lo, long hi, long *out)
{
    if (*s < '0' || *s > '9') {
        return -1;
    }
    char *end;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || v < lo || v > hi) {
        return -1;
    }
    *out = v;
    return 0;
}

static int takes_value(const char *arg)
{
    return strcmp(arg, "--connect") == 0 || strcmp(arg, "--port") == 0 ||
           strcmp(arg, "--name") == 0 || strcmp(arg, "--snaprate") == 0 ||
           strcmp(arg, "--map") == 0;
}

int parse_options(int argc, char **argv, Options *out)
{
    Options o = {LAUNCH_SOLO, NULL, NET_PORT_DEFAULT, "", SNAPSHOT_RATE, NULL};
    int host = 0, dedicated = 0, port_given = 0, name_given = 0, rate_given = 0;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            return 1;
        }
        if (strcmp(arg, "--host") == 0) {
            host = 1;
            continue;
        }
        if (strcmp(arg, "--dedicated") == 0) {
            dedicated = 1;
            continue;
        }
        if (!takes_value(arg)) {
            fprintf(stderr, "Unknown argument '%s'\n", arg);
            return -1;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "%s needs a value\n", arg);
            return -1;
        }
        const char *value = argv[++i];
        long n;
        if (strcmp(arg, "--connect") == 0) {
            if (value[0] == '\0') {
                fprintf(stderr, "--connect: the address is empty\n");
                return -1;
            }
            o.connect = value;
        } else if (strcmp(arg, "--port") == 0) {
            if (parse_int(value, 1, 65535, &n) != 0) {
                fprintf(stderr, "--port: '%s' is not a port number, 1 to 65535\n", value);
                return -1;
            }
            o.port = (uint16_t)n;
            port_given = 1;
        } else if (strcmp(arg, "--snaprate") == 0) {
            if (parse_int(value, SNAPSHOT_RATE_MIN, TICK_RATE, &n) != 0) {
                fprintf(stderr, "--snaprate: '%s' is not a rate, %d to %d snapshots a second\n",
                        value, SNAPSHOT_RATE_MIN, TICK_RATE);
                return -1;
            }
            o.snapshot_rate = (int)n;
            rate_given = 1;
        } else if (strcmp(arg, "--map") == 0) {
            /* What MSG_ACCEPT can carry, and what a client takes from it. */
            size_t len = strlen(value);
            if (len == 0 || len >= NET_MAP_MAX || strpbrk(value, "/\\")) {
                fprintf(stderr,
                        "--map: '%s' is not a map, a file name in %s of 1 to %d characters\n",
                        value, MAP_DIR, NET_MAP_MAX - 1);
                return -1;
            }
            o.map = value;
        } else {
            size_t len = strlen(value);
            if (len == 0 || len >= NET_NAME_MAX) {
                fprintf(stderr, "--name: '%s' is not a name, 1 to %d characters\n", value,
                        NET_NAME_MAX - 1);
                return -1;
            }
            o.name = value;
            name_given = 1;
        }
    }

    if (dedicated && !host) {
        fprintf(stderr, "--dedicated needs --host: it is the server that runs with no window\n");
        return -1;
    }
    if (host && o.connect) {
        fprintf(stderr, "--host and --connect: a game is either served here or joined\n");
        return -1;
    }
    if (port_given && !host && !o.connect) {
        fprintf(stderr, "--port needs --host or --connect: a game alone has no port\n");
        return -1;
    }
    if (rate_given && !host) {
        fprintf(stderr, "--snaprate needs --host: it is the server that sends snapshots\n");
        return -1;
    }
    if (o.map && o.connect) {
        fprintf(stderr, "--map and --connect: a client plays the map its server names\n");
        return -1;
    }
    if (name_given && !o.connect) {
        fprintf(stderr, "--name needs --connect: it is the name a client joins under\n");
        return -1;
    }
#ifndef ENABLE_MULTIPLAYER
    if (host || o.connect) {
        fprintf(stderr, "%s: this build has no network game (ENABLE_MULTIPLAYER=OFF)\n",
                host ? "--host" : "--connect");
        return -1;
    }
#endif

    o.mode = dedicated   ? LAUNCH_DEDICATED
             : host      ? LAUNCH_HOST
             : o.connect ? LAUNCH_CLIENT
                         : LAUNCH_SOLO;
    if (!o.map && !o.connect) {
        o.map = host ? OPT_MAP_SERVER : OPT_MAP_SOLO;
    }
    *out = o;
    return 0;
}
