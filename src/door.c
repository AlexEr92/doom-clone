#include "door.h"
#include "utils.h"
#include <math.h>
#include <string.h>

#define DOOR_OPEN_TIME   4.0f   /* seconds the door stays open */
#define DOOR_SPEED       2.0f   /* openness units per second */
#define USE_RANGE         1.2f   /* cells; how close player must be */

void door_list_init(DoorList *dl) {
    memset(dl, 0, sizeof(*dl));
}

int door_discover(DoorList *dl, const Map *m) {
    dl->count = 0;
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            if (m->cells[y][x] == 2) {
                if (dl->count >= MAX_DOORS) return dl->count;
                Door *d = &dl->doors[dl->count];
                d->cellx = x;
                d->celly = y;
                d->state = DOOR_CLOSED;
                d->openness = 0.0f;
                d->timer = 0.0f;
                dl->count++;
            }
        }
    }
    return dl->count;
}

int door_at(const DoorList *dl, int cellx, int celly) {
    for (int i = 0; i < dl->count; i++) {
        if (dl->doors[i].cellx == cellx && dl->doors[i].celly == celly) return i;
    }
    return -1;
}

int door_is_blocking(const DoorList *dl, int cellx, int celly) {
    int idx = door_at(dl, cellx, celly);
    if (idx < 0) return 0; /* not a door */
    return dl->doors[idx].openness < 0.5f;
}

void door_try_use(DoorList *dl, const Player *p, const Map *m) {
    (void)m;
    /* Check the cell directly in front of the player (1 cell ahead). */
    float fx = p->x + p->dirX * USE_RANGE;
    float fy = p->y + p->dirY * USE_RANGE;
    int cx = (int)fx;
    int cy = (int)fy;
    int idx = door_at(dl, cx, cy);
    if (idx < 0) {
        /* also try the cell the player is standing in/on the line to it */
        int cx2 = (int)p->x;
        int cy2 = (int)p->y;
        idx = door_at(dl, cx2, cy2);
        if (idx < 0) return;
    }
    Door *d = &dl->doors[idx];
    switch (d->state) {
        case DOOR_CLOSED:
            d->state = DOOR_OPENING;
            break;
        case DOOR_OPEN:
            d->state = DOOR_CLOSING;
            d->timer = 0.0f;
            break;
        case DOOR_OPENING:
            /* allow immediate close toggle */
            d->state = DOOR_CLOSING;
            d->timer = 0.0f;
            break;
        case DOOR_CLOSING:
            d->state = DOOR_OPENING;
            break;
    }
    d->triggered = 1;
}

void door_update_all(DoorList *dl, double dt) {
    for (int i = 0; i < dl->count; i++) {
        Door *d = &dl->doors[i];
        switch (d->state) {
            case DOOR_CLOSED:
                break;
            case DOOR_OPENING:
                d->openness += DOOR_SPEED * (float)dt;
                if (d->openness >= 1.0f) {
                    d->openness = 1.0f;
                    d->state = DOOR_OPEN;
                    d->timer = DOOR_OPEN_TIME;
                }
                break;
            case DOOR_OPEN:
                d->timer -= (float)dt;
                if (d->timer <= 0.0f) {
                    d->state = DOOR_CLOSING;
                }
                break;
            case DOOR_CLOSING:
                d->openness -= DOOR_SPEED * (float)dt;
                if (d->openness <= 0.0f) {
                    d->openness = 0.0f;
                    d->state = DOOR_CLOSED;
                }
                break;
        }
        d->triggered = 0;
    }
}