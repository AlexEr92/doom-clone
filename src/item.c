#include "item.h"
#include "utils.h"
#include <math.h>
#include <string.h>

void item_list_init(ItemList *il) {
    memset(il, 0, sizeof(*il));
}

int item_add(ItemList *il, int sprite_id, ItemType type, float amount, int weapon) {
    if (il->count >= MAX_ITEMS) return -1;
    Item *it = &il->items[il->count];
    it->active = 1;
    it->sprite_id = sprite_id;
    it->type = type;
    it->amount = amount;
    it->weapon = weapon;
    /* position will be read from the sprite when needed */
    it->x = 0.0f;
    it->y = 0.0f;
    return il->count++;
}

static int apply_pickup(Item *it, SpriteList *sl, Player *pl, WeaponSystem *ws, Audio *au) {
    switch (it->type) {
        case ITEM_MEDKIT: {
            if (pl->hp >= 100.0f) return 0;
            pl->hp = clampf(pl->hp + it->amount, 0.0f, 100.0f);
            break;
        }
        case ITEM_ARMOR: {
            if (pl->armor >= 100.0f) return 0;
            pl->armor = clampf(pl->armor + it->amount, 0.0f, 100.0f);
            break;
        }
        case ITEM_AMMO: {
            if (it->weapon < 0 || it->weapon >= WEAPON_COUNT) return 0;
            Weapon *w = &ws->weapons[it->weapon];
            if (w->ammo >= w->max_ammo) return 0;
            w->ammo = clampi(w->ammo + (int)it->amount, 0, w->max_ammo);
            break;
        }
        default:
            return 0;
    }
    /* deactivate sprite + item */
    if (it->sprite_id >= 0 && it->sprite_id < sl->count) {
        sl->items[it->sprite_id].active = 0;
    }
    it->active = 0;
    audio_play_volume(au, SND_PICKUP, 0.8f);
    return 1;
}

int item_update(ItemList *il, SpriteList *sl, Player *pl, WeaponSystem *ws, Audio *au) {
    const float radius = 0.45f;
    int picked = 0;
    for (int i = 0; i < il->count; i++) {
        Item *it = &il->items[i];
        if (!it->active) continue;
        if (it->sprite_id < 0 || it->sprite_id >= sl->count) continue;
        Sprite *sp = &sl->items[it->sprite_id];
        if (!sp->active) { it->active = 0; continue; }
        float dx = sp->x - pl->x;
        float dy = sp->y - pl->y;
        if (dx * dx + dy * dy <= radius * radius) {
            if (apply_pickup(it, sl, pl, ws, au)) picked = 1;
        }
    }
    return picked;
}