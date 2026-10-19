#ifndef NET_INTERP_H
#define NET_INTERP_H

/* What a network client draws of everyone else: not the newest snapshot but
 * the world as it stood INTERP_DELAY before it, between two snapshots the
 * client already has. Snapshots come SNAPSHOT_RATE times a second and frames
 * at 60 or more; drawn as they come, other players and enemies would move in
 * steps of several frames.
 *
 * Time here is the server's, counted in ticks: a snapshot is the world at its
 * tick, and the motion between two is spread over the ticks between them.
 * When a snapshot arrived says only how the client's clock lines up with the
 * server's. Arrivals are as uneven as the network and the once-a-frame poll
 * that reads them, and two snapshots paired by them would have everyone speed
 * up and slow down from one pair to the next.
 *
 * Nothing here reaches the network or a frame: net_client.c feeds it, and a
 * test drives it with times of its own. */

#include "config.h"
#include "net_protocol.h"

struct World;

typedef struct {
    MsgSnapshot snaps[SNAPSHOT_HISTORY]; /* a ring, oldest to newest by ascending tick */
    double recv_time[SNAPSHOT_HISTORY];  /* the client's clock when each one came, seconds */
    int head, count;                     /* the slot the next one goes to; how many are kept */
    double offset;  /* the client's clock minus the server's, smoothed: seconds */
    double settled; /* `offset` as the polls before the latest one left it */
    int synced;     /* `settled` holds something: a poll before the latest one brought a snapshot */
} SnapshotBuffer;

/* Forget every snapshot: a new connection starts from nothing. */
void interp_reset(SnapshotBuffer *b);

/* Keep `s`, which arrived at `now` on the client's clock, in seconds. Only a
 * snapshot newer than the newest kept is taken, and the oldest kept makes room
 * for it. Snapshots pushed with one `now` came in one poll: they share its
 * time, and only the newest of them says how the clocks line up. */
void interp_push(SnapshotBuffer *b, const MsgSnapshot *s, double now);

/* Fill `view` with `w`, the world the client has, but with its players and
 * enemies as they stood INTERP_DELAY before `now`:
 *
 *   - whatever does not move — who is in the world, alive, hp, the enemy's
 *     state, the weapon — is that of the last snapshot at or before that
 *     moment;
 *   - a position moves on a straight line to the next snapshot, a facing
 *     along the shorter arc, when the character is in both and alive in the
 *     earlier one: a respawn is a jump, not a walk across the map;
 *   - past the newest snapshot whoever is alive in the last two goes on at
 *     the speed they show, for INTERP_EXTRAPOLATE at most, and then stands;
 *     before the oldest, the oldest is shown.
 *
 * The local player (w->local_player), the items, the doors and the rest of
 * `w` are left as `w` has them. With no snapshot kept `view` is `w`. */
void interp_view(struct World *view, const struct World *w, const SnapshotBuffer *b, double now);

#endif
