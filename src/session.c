#include "session.h"
#include "world.h"
#ifdef ENABLE_MULTIPLAYER
#include "net_client.h"
#include "net_server.h"
#endif
#include <stdio.h>
#include <stdlib.h>

static SessionChange no_change(void *ctx)
{
    (void)ctx;
    return SESS_SAME;
}

/* ---- solo: the world is this process's own ---- */

typedef struct {
    World world;
    const char *map_path;
} Solo;

static World *solo_world(void *ctx)
{
    return &((Solo *)ctx)->world;
}

/* The local player is the only one, id 0, and the inputs world_step() takes
 * are indexed by id. */
static void solo_tick(void *ctx, const PlayerInput *local, double dt)
{
    world_step(solo_world(ctx), local, local ? 1 : 0, dt);
}

static int solo_local_player(void *ctx)
{
    return solo_world(ctx)->local_player;
}

/* A fresh world. It has every slot free, so the player always gets one. */
static int solo_restart(void *ctx)
{
    Solo *s = ctx;
    if (world_load(&s->world, s->map_path) != 0) {
        fprintf(stderr,
                "Failed to load map '%s'. Run from the project root "
                "(assets are resolved relative to the working directory).\n",
                s->map_path);
        return -1;
    }
    s->world.local_player = world_add_player(&s->world, 0);
    return 0;
}

static void solo_close(void *ctx)
{
    free(ctx);
}

static int solo_open(GameSession *gs, const char *map_path)
{
    Solo *s = calloc(1, sizeof(*s));
    if (!s) {
        fprintf(stderr, "Out of memory\n");
        return -1;
    }
    s->map_path = map_path;
    if (solo_restart(s) != 0) {
        free(s);
        return -1;
    }
    *gs = (GameSession){
            solo_world, solo_tick, solo_local_player, no_change, solo_restart, solo_close, s, 0, 0};
    return 0;
}

#ifdef ENABLE_MULTIPLAYER

/* ---- host: the world is a server's, run here ---- */

static World *host_world(void *ctx)
{
    return server_world(ctx);
}

static void host_tick(void *ctx, const PlayerInput *local, double dt)
{
    server_tick(ctx, local, dt);
}

static int host_local_player(void *ctx)
{
    return server_local_player(ctx);
}

/* The world is everyone's: one player wanting to start over does not start it
 * over for the rest. */
static int host_restart(void *ctx)
{
    (void)ctx;
    return 0;
}

static void host_close(void *ctx)
{
    server_destroy(ctx);
}

static int host_open(GameSession *gs, const Options *o, const char *map_path)
{
    Server *s = server_create(o->port, map_path);
    if (!s) {
        fprintf(stderr,
                "Cannot serve '%s' on port %u: the map does not load, or the port is taken\n",
                map_path, (unsigned)o->port);
        return -1;
    }
    server_set_snapshot_rate(s, o->snapshot_rate);
    /* A server nobody has joined yet has every slot free. */
    if (o->mode == LAUNCH_HOST) {
        server_add_local_player(s);
    }
    *gs = (GameSession){
            host_world, host_tick, host_local_player, no_change, host_restart, host_close, s, 1, 0};
    return 0;
}

/* ---- client: the world is a server's elsewhere ---- */

typedef struct {
    Client *client;
    const char *addr;
    uint16_t port;
    const char *name;
} Remote;

/* Join the server, or join it again, as a new player, once the last
 * connection has ended. Says why on stderr when it cannot even start. */
static void remote_join(Remote *r)
{
    ClientState st = client_state(r->client);
    if (st == CL_CONNECTING || st == CL_CONNECTED) {
        return;
    }
    if (client_connect(r->client, r->addr, r->port, r->name) != 0) {
        fprintf(stderr, "Cannot join %s: %s\n", r->addr, client_reason(r->client));
    }
}

static World *remote_world(void *ctx)
{
    return client_world(((Remote *)ctx)->client);
}

/* The server runs the tick; the client numbers the input. */
static void remote_tick(void *ctx, const PlayerInput *local, double dt)
{
    (void)dt;
    if (local) {
        client_send_input(((Remote *)ctx)->client, local);
    }
}

static int remote_local_player(void *ctx)
{
    return client_local_player(((Remote *)ctx)->client);
}

static SessionChange remote_poll(void *ctx)
{
    Remote *r = ctx;
    ClientState was = client_state(r->client);
    client_poll(r->client);
    ClientState is = client_state(r->client);
    if (is == was) {
        return SESS_SAME;
    }
    if (is == CL_CONNECTED) {
        return SESS_JOINED;
    }
    /* No screen says so yet: the reason goes to stderr. */
    fprintf(stderr, "%s %s: %s\n", is == CL_REJECTED ? "Cannot join" : "Disconnected from", r->addr,
            client_reason(r->client));
    return SESS_LOST;
}

/* A network game is the server's to restart: all the client does is join
 * again once it has been let go. */
static int remote_restart(void *ctx)
{
    remote_join(ctx);
    return 0;
}

static void remote_close(void *ctx)
{
    Remote *r = ctx;
    client_destroy(r->client);
    free(r);
}

static int remote_open(GameSession *gs, const Options *o)
{
    Remote *r = calloc(1, sizeof(*r));
    if (!r || !(r->client = client_create())) {
        fprintf(stderr, "Out of memory\n");
        free(r);
        return -1;
    }
    r->addr = o->connect;
    r->port = o->port;
    r->name = o->name;
    remote_join(r);
    if (client_state(r->client) == CL_REJECTED) {
        remote_close(r);
        return -1;
    }
    *gs = (GameSession){remote_world,
                        remote_tick,
                        remote_local_player,
                        remote_poll,
                        remote_restart,
                        remote_close,
                        r,
                        1,
                        1};
    return 0;
}

#endif

int session_open(GameSession *s, const Options *o, const char *map_path)
{
    switch (o->mode) {
        case LAUNCH_SOLO: return solo_open(s, map_path);
#ifdef ENABLE_MULTIPLAYER
        case LAUNCH_HOST:
        case LAUNCH_DEDICATED: return host_open(s, o, map_path);
        case LAUNCH_CLIENT: return remote_open(s, o);
#endif
        default: break;
    }
    fprintf(stderr, "This build has no network game (ENABLE_MULTIPLAYER=OFF)\n");
    return -1;
}
