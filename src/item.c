#include "item.h"
#include "utils.h"
#include <math.h>
#include <string.h>

/* How close a player has to get to a pickup, in world cells. */
#define PICKUP_RADIUS 0.45f

void item_list_init(ItemList *il)
{
    memset(il, 0, sizeof(*il));
}

int item_add(ItemList *il, float x, float y, ItemType type, float amount, int weapon)
{
    if (il->count >= MAX_ITEMS) {
        return -1;
    }
    Item *it = &il->items[il->count];
    it->active = 1;
    it->type = type;
    it->x = x;
    it->y = y;
    it->amount = amount;
    it->weapon = weapon;
    return il->count++;
}

/* Someone who can pick things up: in the world, not dead, with hp left. */
static int can_pick_up(const PlayerState *p)
{
    return p->active && p->alive && p->hp > 0.0f;
}

/* Give the item to `pl`. Returns 0 when they have no use for it, and the
 * pickup then stays where it is for the next player. */
static int apply_pickup(Item *it, PlayerState *pl, EventQueue *evq)
{
    switch (it->type) {
        case ITEM_MEDKIT: {
            if (pl->hp >= 100.0f) {
                return 0;
            }
            pl->hp = clampf(pl->hp + it->amount, 0.0f, 100.0f);
            break;
        }
        case ITEM_ARMOR: {
            if (pl->armor >= 100.0f) {
                return 0;
            }
            pl->armor = clampf(pl->armor + it->amount, 0.0f, 100.0f);
            break;
        }
        case ITEM_AMMO: {
            if (it->weapon < 0 || it->weapon >= WEAPON_COUNT) {
                return 0;
            }
            Weapon *w = &pl->weapons.weapons[it->weapon];
            if (w->ammo >= w->max_ammo) {
                return 0;
            }
            w->ammo = clampi(w->ammo + (int)it->amount, 0, w->max_ammo);
            break;
        }
        default: return 0;
    }
    it->active = 0;
    event_push(evq, EV_PICKUP, pl->id, it->x, it->y);
    return 1;
}

void item_update(ItemList *il, PlayerState *players, int count, EventQueue *evq)
{
    for (int i = 0; i < il->count; i++) {
        Item *it = &il->items[i];
        if (!it->active) {
            continue;
        }
        for (int p = 0; p < count; p++) {
            PlayerState *pl = &players[p];
            if (!can_pick_up(pl)) {
                continue;
            }
            float dx = it->x - pl->x;
            float dy = it->y - pl->y;
            if (dx * dx + dy * dy > PICKUP_RADIUS * PICKUP_RADIUS) {
                continue;
            }
            if (apply_pickup(it, pl, evq)) {
                break; /* gone; the players after this one find nothing */
            }
        }
    }
}
