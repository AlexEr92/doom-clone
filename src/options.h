#ifndef OPTIONS_H
#define OPTIONS_H

/* The command line: which of the four launch modes, and how it is set up.
 * Reading it opens nothing — no window, no socket — and reaches no other
 * module of the game, so it is tested on its own. */

#include <stdint.h>
#include <stdio.h>

typedef enum {
    LAUNCH_SOLO,      /* no flags: the single-player game, which opens no socket */
    LAUNCH_HOST,      /* --host: a server, with a player at this machine on it */
    LAUNCH_DEDICATED, /* --host --dedicated: a server alone, with no window and no player */
    LAUNCH_CLIENT,    /* --connect: a player on a server elsewhere */
} LaunchMode;

typedef struct {
    LaunchMode mode;
    const char *connect; /* the server to join; NULL unless LAUNCH_CLIENT */
    uint16_t port;       /* to serve on or to join, NET_PORT_DEFAULT unless given */
    const char *name;    /* what the client joins under, "" unless given */
    int snapshot_rate;   /* what a server sends a second, SNAPSHOT_RATE unless given */
} Options;

/* Read argv into *o. Returns 0 when it describes a launch; 1 when it asks for
 * the help; -1 when it is wrong — an unknown argument, a value missing or out
 * of range, flags that do not go together, a network flag in a build without
 * the network game — and then the reason is on stderr. *o is written on 0
 * alone. The strings in it point into argv. */
int parse_options(int argc, char **argv, Options *o);

/* What the command line takes, for `prog` (argv[0]). */
void options_usage(FILE *f, const char *prog);

#endif
