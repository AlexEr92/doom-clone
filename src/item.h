#ifndef ITEM_H
#define ITEM_H

#include "sprite.h"
#include "player.h"
#include "weapon.h"
#include "audio.h"

typedef enum {
    ITEM_NONE = 0,
    ITEM_MEDKIT,
    ITEM_AMMO,
    ITEM_ARMOR,
} ItemType;

typedef struct {
    int active;       /* 1 until picked up */
    int sprite_id;     /* index into SpriteList */
    ItemType type;
    float x, y;
    float amount;     /* hp/ammo/armor amount */
    int weapon;       /* for ammo: which weapon (WEAPON_PISTOL/SHOTGUN) */
} Item;

#define MAX_ITEMS 64

typedef struct {
    Item items[MAX_ITEMS];
    int count;
} ItemList;

void item_list_init(ItemList *il);

/* Add a pickup item tied to an existing sprite_id (created during map load).
 * type derived from sprite type if ITEM_NONE. */
int item_add(ItemList *il, int sprite_id, ItemType type, float amount, int weapon);

/* Check pickups: if player is within radius, apply effect and deactivate the
 * sprite + item. Returns 1 if something was picked up this call. */
int item_update(ItemList *il, SpriteList *sl, Player *pl, WeaponSystem *ws, Audio *au);

#endif