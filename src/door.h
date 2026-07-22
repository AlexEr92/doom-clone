#ifndef DOOR_H
#define DOOR_H

#include "map.h"
#include "player.h"

typedef enum {
    DOOR_CLOSED = 0,
    DOOR_OPENING,
    DOOR_OPEN,
    DOOR_CLOSING,
} DoorState;

typedef struct Door {
    int cellx, celly;     /* map cell coords of the door */
    DoorState state;
    float openness;       /* 0 = closed (solid), 1 = fully open (walk-through) */
    float timer;          /* seconds remaining in OPEN before auto-close */
    int triggered;        /* set when player used the door this tick */
} Door;

#define MAX_DOORS 64

typedef struct DoorList {
    Door doors[MAX_DOORS];
    int count;
} DoorList;

void door_list_init(DoorList *dl);

/* Scan the map for door cells (== 2) and register them. Returns count. */
int door_discover(DoorList *dl, const Map *m);

/* Find a door at a given cell, or -1. */
int door_at(const DoorList *dl, int cellx, int celly);

/* Player "uses" the door nearest to their front within range. */
void door_try_use(DoorList *dl, const Player *p, const Map *m);

/* Per-tick update of door openness/timers. */
void door_update_all(DoorList *dl, double dt);

/* Returns 1 if the door at cellx,celly is currently blocking (solid).
 * A door blocks when openness < 0.5. */
int door_is_blocking(const DoorList *dl, int cellx, int celly);

#endif