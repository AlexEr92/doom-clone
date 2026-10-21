#ifndef EVENT_H
#define EVENT_H

#include "config.h"
#include <math.h>
#include <stdint.h>

/* Things the simulation did during a tick that something outside it has to
 * react to — sound, for now. The simulation never plays anything itself: it
 * appends to the queue, and the client drains it after the tick. That is what
 * lets enemy.c, weapon.c, item.c and door.c compile without SDL. On the wire
 * the same records go as NetEvent, and the client measures the distance to
 * *its own* player rather than to whoever caused them. */

typedef enum {
    EV_SHOT = 0,    /* actor = shooter, x/y = where the shot was fired from */
    EV_ENEMY_HURT,  /* actor = enemy index, x/y = enemy */
    EV_ENEMY_DEATH, /* actor = enemy index, x/y = enemy */
    EV_PLAYER_HURT, /* actor = victim player, x/y = whoever hit them */
    EV_PICKUP,      /* actor = player, x/y = the item */
    EV_DOOR,        /* actor = door index, x/y = centre of the door cell */
    EV_NO_AMMO,     /* actor = player, x/y = player */
    /* actor = the player credited with the kill — the victim themselves when
     * no player is —, arg = victim, x/y = victim */
    EV_PLAYER_DEATH,
    EV_COUNT
} GameEventKind;

/* Kept small and free of pointers so it can be memcpy'd into a snapshot. */
typedef struct {
    uint8_t kind;  /* GameEventKind */
    uint8_t actor; /* index of whoever caused it; meaning depends on kind */
    uint8_t arg;   /* what else the kind needs to say, 0 for most of them */
    float x, y;    /* world cells: where it happened */
} GameEvent;

typedef struct EventQueue {
    GameEvent items[MAX_EVENTS];
    int count;
} EventQueue;

static inline void event_queue_clear(EventQueue *q)
{
    if (q) {
        q->count = 0;
    }
}

/* Append an event. A NULL queue and a full one are both silently ignored:
 * losing a sound must never change the simulation.
 *
 * The queue holds one tick, and an event of a kind, actor and arg it already
 * holds is not added again: one shotgun blast hurts an enemy once per pellet, and
 * seven copies of one sound starting on one sample play as a single sound
 * some 17 dB louder, clipped, on seven mixer channels. Events of different
 * ticks are different sounds and stay apart, which is why it happens here,
 * before the server mixes the ticks between two snapshots together. */
static inline void event_push(EventQueue *q, GameEventKind kind, int actor, int arg, float x,
                              float y)
{
    if (!q || q->count >= MAX_EVENTS) {
        return;
    }
    for (int i = 0; i < q->count; i++) {
        const GameEvent *e = &q->items[i];
        if (e->kind == (uint8_t)kind && e->actor == (uint8_t)actor && e->arg == (uint8_t)arg) {
            return;
        }
    }
    GameEvent *e = &q->items[q->count++];
    e->kind = (uint8_t)kind;
    e->actor = (uint8_t)actor;
    e->arg = (uint8_t)arg;
    e->x = x;
    e->y = y;
}

/* How far, in cells, the sound of each kind carries: the client fades it
 * linearly to nothing there, and the server sends no event any farther from
 * the receiver. 0 is a sound only the player it is about hears. */
static inline float event_range(int kind)
{
    switch (kind) {
        case EV_SHOT: return 20.0f;
        case EV_ENEMY_HURT:
        case EV_ENEMY_DEATH:
        case EV_DOOR: return 16.0f;
        case EV_PLAYER_HURT: return 12.0f;
        default: return 0.0f; /* EV_PICKUP, EV_NO_AMMO, EV_PLAYER_DEATH */
    }
}

/* Whether `e` is player `id`'s own: their shot, their pain, their pickup,
 * their empty click, a death they died or dealt. The kinds whose actor is an
 * enemy or a door never are. */
static inline int event_is_own(const GameEvent *e, int id)
{
    switch (e->kind) {
        case EV_SHOT:
        case EV_PLAYER_HURT:
        case EV_PICKUP:
        case EV_NO_AMMO: return e->actor == id;
        case EV_PLAYER_DEATH: return e->actor == id || e->arg == id;
        default: return 0;
    }
}

static inline float event_distance(const GameEvent *e, float x, float y)
{
    float dx = e->x - x, dy = e->y - y;
    return sqrtf(dx * dx + dy * dy);
}

/* How much of the sound of `e` reaches player `id` standing at (x, y): all of
 * it when it is their own, wherever it happened; otherwise from all of it
 * where it happened down to nothing at event_range(). 0 is not heard. */
static inline float event_gain(const GameEvent *e, int id, float x, float y)
{
    if (event_is_own(e, id)) {
        return 1.0f;
    }
    float range = event_range(e->kind);
    float dist = event_distance(e, x, y);
    return dist < range ? 1.0f - dist / range : 0.0f;
}

#endif
