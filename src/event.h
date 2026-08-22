#ifndef EVENT_H
#define EVENT_H

#include <stdint.h>

/* Things the simulation did during a tick that something outside it has to
 * react to — sound, for now. The simulation never plays anything itself: it
 * appends to the queue, and the client drains it after the tick. That is what
 * lets enemy.c, weapon.c, item.c and door.c compile without SDL, and the same
 * records go on the wire as NetEvent in stage 7, where the client can finally
 * measure the distance to *its own* player instead of to the shooter. */

typedef enum {
    EV_SHOT = 0,    /* actor = shooter, x/y = where the shot was fired from */
    EV_ENEMY_HURT,  /* actor = enemy index, x/y = enemy */
    EV_ENEMY_DEATH, /* actor = enemy index, x/y = enemy */
    EV_PLAYER_HURT, /* actor = victim player, x/y = whoever hit them */
    EV_PICKUP,      /* actor = player, x/y = the item */
    EV_DOOR,        /* actor = door index, x/y = centre of the door cell */
    EV_NO_AMMO,     /* actor = player, x/y = player */
    EV_COUNT
} GameEventKind;

/* Kept small and free of pointers so it can be memcpy'd into a snapshot. */
typedef struct {
    uint8_t kind;  /* GameEventKind */
    uint8_t actor; /* index of whoever caused it; meaning depends on kind */
    float x, y;    /* world cells: where it happened */
} GameEvent;

#define MAX_EVENTS 32

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
 * losing a sound must never change the simulation. */
static inline void event_push(EventQueue *q, GameEventKind kind, int actor, float x, float y)
{
    if (!q || q->count >= MAX_EVENTS) {
        return;
    }
    GameEvent *e = &q->items[q->count++];
    e->kind = (uint8_t)kind;
    e->actor = (uint8_t)actor;
    e->x = x;
    e->y = y;
}

#endif
