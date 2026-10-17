#ifndef ITEM_H
#define ITEM_H

#include "config.h"
#include "player.h"
#include "weapon.h"
#include "event.h"

typedef enum {
    ITEM_NONE = 0,
    ITEM_MEDKIT,
    ITEM_AMMO,
    ITEM_ARMOR,
} ItemType;

typedef struct {
    int active; /* 1 until picked up */
    ItemType type;
    float x, y;   /* where the pickup lies, in world cells */
    float amount; /* hp/ammo/armor amount */
    int weapon;   /* for ammo: which weapon (WeaponType) */
} Item;

typedef struct {
    Item items[MAX_ITEMS];
    int count;
} ItemList;

void item_list_init(ItemList *il);

/* Put a pickup at (x, y) on the map and return its index, or -1 when the list
 * is full. The index is the item's id and never changes: a pickup is taken by
 * clearing `active`, never by removing it from the list. */
int item_add(ItemList *il, float x, float y, ItemType type, float amount, int weapon);

/* Hand every pickup to whoever is standing on it. `players` is indexed by id
 * and `count` is how far it reaches; a player who is not in the world or not
 * alive picks nothing up. Ammo goes to the weapons of the player who took it.
 *
 * An item within reach of several players at once goes to the lowest id — the
 * server and a client predicting it have to agree on who got it. A player who
 * cannot use the pickup (full hp, full ammo) does not block the next one from
 * taking it. Posts EV_PICKUP for each item taken. */
void item_update(ItemList *il, PlayerState *players, int count, EventQueue *evq);

#endif
