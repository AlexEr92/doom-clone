#include "net_interp.h"
#include "world.h"
#include "utils.h"
#include <math.h>
#include <string.h>

/* How far one poll's offset pulls the smoothed one: a tenth of the way. The
 * jitter of single arrivals averages out over ten snapshots, half a second at
 * SNAPSHOT_RATE, and a lasting change of latency is followed about as fast. */
#define OFFSET_SMOOTHING 0.1

/* An arrival this far off the smoothed offset, in seconds, is no jitter: the
 * server has lost ticks, or the route has changed. Smoothed in, the moment
 * drawn would stay past every snapshot for seconds, so it is taken at once. */
#define OFFSET_RESYNC 0.5

void interp_reset(SnapshotBuffer *b)
{
    memset(b, 0, sizeof(*b));
}

/* The ring slot of the i-th snapshot kept, 0 being the oldest. */
static int slot(const SnapshotBuffer *b, int i)
{
    return (b->head - b->count + i + SNAPSHOT_HISTORY) % SNAPSHOT_HISTORY;
}

void interp_push(SnapshotBuffer *b, const MsgSnapshot *s, double now)
{
    int newest = slot(b, b->count - 1);
    if (b->count > 0 && (int32_t)(s->tick - b->snaps[newest].tick) <= 0) {
        return;
    }
    /* The first snapshot of a poll: what the polls before made of the offset
     * is settled, and this poll's newest snapshot moves it from there. */
    if (b->count == 0 || now != b->recv_time[newest]) {
        b->synced = b->count > 0;
        b->settled = b->offset;
    }
    double sample = now - (double)s->tick / TICK_RATE;
    if (!b->synced || fabs(sample - b->settled) > OFFSET_RESYNC) {
        b->offset = sample;
    } else {
        b->offset = b->settled + (sample - b->settled) * OFFSET_SMOOTHING;
    }

    b->snaps[b->head] = *s;
    b->recv_time[b->head] = now;
    b->head = (b->head + 1) % SNAPSHOT_HISTORY;
    if (b->count < SNAPSHOT_HISTORY) {
        b->count++;
    }
}

static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* Turning the short way round: from 3.1 to -3.1 is a small turn across PI,
 * not a whole one back through 0. */
static float lerp_angle(float a, float b, float t)
{
    return wrap_angle(a + wrap_angle(b - a) * t);
}

static const NetPlayer *find_player(const MsgSnapshot *s, int id)
{
    for (int i = 0; i < s->player_count; i++) {
        if (s->players[i].id == id) {
            return &s->players[i];
        }
    }
    return NULL;
}

/* Whether a character alive in `from` as `alive_from` and in `to` as
 * `alive_to` moves at `t` between them: a corpse does not, and past `to` only
 * whoever is still alive in it goes on. */
static int moves(int alive_from, int alive_to, double t)
{
    return alive_from && (t <= 1.0 || alive_to);
}

void interp_view(World *view, const World *w, const SnapshotBuffer *b, double now)
{
    *view = *w;
    if (b->count == 0) {
        return;
    }

    /* The moment drawn, in the server's ticks, and the two snapshots around
     * it: `from` the last one at or before it, `to` the one after. Before the
     * oldest the oldest two hold at their start; past the newest the newest
     * two carry on. With a single snapshot both are that one. */
    double at = (now - b->offset - INTERP_DELAY) * TICK_RATE;
    int f = 0;
    while (f + 2 < b->count && (double)b->snaps[slot(b, f + 1)].tick <= at) {
        f++;
    }
    const MsgSnapshot *from = &b->snaps[slot(b, f)];
    const MsgSnapshot *to = &b->snaps[slot(b, b->count > 1 ? f + 1 : f)];
    double span = (double)(int32_t)(to->tick - from->tick);
    double t = 0.0;
    if (span > 0.0) {
        t = (at - (double)from->tick) / span;
        t = fmax(0.0, fmin(t, 1.0 + INTERP_EXTRAPOLATE * TICK_RATE / span));
    }
    float tf = (float)t;

    snapshot_apply_characters(view, t < 1.0 ? from : to);

    for (int id = 0; id < MAX_PLAYERS; id++) {
        const NetPlayer *a = find_player(from, id);
        const NetPlayer *z = find_player(to, id);
        if (id == w->local_player || !a || !z || !moves(a->alive, z->alive, t)) {
            continue;
        }
        PlayerState *p = &view->players[id];
        p->x = lerp(a->x, z->x, tf);
        p->y = lerp(a->y, z->y, tf);
        p->angle = lerp_angle(a->angle, z->angle, tf);
    }

    for (int i = 0; i < from->enemy_count && i < to->enemy_count; i++) {
        const NetEnemy *a = &from->enemies[i];
        const NetEnemy *z = &to->enemies[i];
        if (!moves(a->state != ESTATE_DEAD, z->state != ESTATE_DEAD, t)) {
            continue;
        }
        Enemy *e = &view->enemies.items[i];
        e->x = lerp(a->x, z->x, tf);
        e->y = lerp(a->y, z->y, tf);
    }

    if (w->local_player >= 0 && w->local_player < MAX_PLAYERS) {
        view->players[w->local_player] = w->players[w->local_player];
    }
}
