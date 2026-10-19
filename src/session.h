#ifndef SESSION_H
#define SESSION_H

/* Where the world the frame loop shows comes from, and where the local
 * player's input goes. One loop serves every launch mode; what differs between
 * them sits behind this table, in three kinds:
 *
 *   solo    the world is this process's own: a tick is world_step();
 *   host    the world is a server's run here: a tick is server_tick(), with
 *           the local player's input, or none on a dedicated server;
 *   client  the world is a server's elsewhere: a tick sends the input, the
 *           world is whatever the newest snapshot says, and everyone else is
 *           drawn a moment behind it.
 *
 * Nothing here reaches SDL, a framebuffer or the sound: a dedicated server
 * runs it too. */

#include "options.h"

struct World;
struct PlayerInput;

/* What poll() found has happened to the game since the last call. */
typedef enum {
    SESS_SAME,   /* nothing the loop has to act on */
    SESS_JOINED, /* the local player is in a world that was not there before */
    SESS_LOST,   /* the game ended under the local player; stderr says why */
} SessionChange;

typedef struct GameSession {
    /* The world to show, NULL while there is none: a client before its first
     * snapshot. */
    struct World *(*world)(void *ctx);
    /* The players and enemies a frame draws, once a frame: world() itself,
     * but in a client the others are a moment in the past, moving smoothly
     * between snapshots (client_view()). NULL while world() is. */
    struct World *(*view)(void *ctx);
    /* One fixed step. `local` is the local player's input, NULL while nobody
     * at this machine plays: a dedicated server, a player in a menu. */
    void (*tick)(void *ctx, const struct PlayerInput *local, double dt);
    /* The id of the player at this machine, -1 when there is none. */
    int (*local_player)(void *ctx);
    /* Once a frame, after its ticks, in every game state. */
    SessionChange (*poll)(void *ctx);
    /* The player asked to play again. -1 when the world cannot be had back,
     * which ends the game; the reason is on stderr. */
    int (*restart)(void *ctx);
    void (*close)(void *ctx);
    void *ctx;
    /* Other players are in the world, so it goes on under a menu or the pause:
     * the loop keeps ticking it, with no local input. */
    int shared;
    /* The world is run elsewhere: tick() only sends the input, and the events
     * in World.events come with poll() rather than tick(). */
    int remote;
} GameSession;

/* Open the session `o` asks for on the map at `map_path`, which a client does
 * not read — it opens the one the server names. Returns 0, or -1 with the
 * reason on stderr. */
int session_open(GameSession *s, const Options *o, const char *map_path);

#endif
