#include "net_protocol.h"
#include "world.h"
#include "utils.h"
#include <math.h>
#include <string.h>

/* hp and armor as whole points. Rounded up, so that anyone with hp left keeps
 * some — the client takes hp <= 0 for dead. NaN and anything below 0 are 0. */
static uint8_t quantize_points(float v)
{
    if (!(v > 0.0f)) {
        return 0;
    }
    if (v >= 255.0f) {
        return 255;
    }
    return (uint8_t)ceilf(v);
}

static uint8_t quantize_openness(float openness)
{
    return (uint8_t)(clampf(openness, 0.0f, 1.0f) * 255.0f + 0.5f);
}

/* A player entry snapshot_apply() can take as it is: the id and the weapon
 * are indices in range, and the receiver holds a weapon `yours` says they own
 * — the server never switches to one not picked up. */
static int player_valid(const NetPlayer *np, const NetWeapons *yours)
{
    if (np->id >= MAX_PLAYERS || np->weapon >= WEAPON_COUNT) {
        return 0;
    }
    return np->id != yours->id || ((yours->owned >> np->weapon) & 1u);
}

/* Bytes on the wire of a snapshot with these counts. */
static size_t wire_size(size_t players, size_t enemies, size_t events)
{
    return offsetof(MsgSnapshot, players) + players * sizeof(NetPlayer) +
           enemies * sizeof(NetEnemy) + sizeof(uint64_t) + MAX_DOORS + events * sizeof(NetEvent);
}

void snapshot_write(MsgSnapshot *s, const World *w, int receiver, uint32_t last_input_tick)
{
    memset(s, 0, sizeof(*s));
    s->h.type = MSG_SNAPSHOT;
    s->tick = w->tick;
    s->last_input_tick = last_input_tick;

    s->yours.id = (uint8_t)receiver;
    if (receiver >= 0 && receiver < MAX_PLAYERS) {
        const WeaponSystem *ws = &w->players[receiver].weapons;
        for (int k = 0; k < WEAPON_COUNT; k++) {
            const Weapon *wp = &ws->weapons[k];
            if (wp->owned) {
                s->yours.owned |= (uint8_t)(1u << k);
            }
            s->yours.ammo[k] = (uint16_t)clampi(wp->ammo, 0, UINT16_MAX);
        }
    }

    int n = 0;
    for (int i = 0; i < w->player_count; i++) {
        const PlayerState *p = &w->players[i];
        if (!p->active) {
            continue;
        }
        const WeaponSystem *ws = &p->weapons;
        NetPlayer *np = &s->players[n++];
        np->id = (uint8_t)i;
        np->weapon = (uint8_t)ws->current;
        np->alive = p->alive ? 1 : 0;
        np->x = p->x;
        np->y = p->y;
        np->angle = p->angle;
        np->hp = quantize_points(p->hp);
        np->armor = quantize_points(p->armor);
    }
    s->player_count = (uint8_t)n;

    s->enemy_count = (uint8_t)w->enemies.count;
    for (int i = 0; i < w->enemies.count; i++) {
        const Enemy *e = &w->enemies.items[i];
        NetEnemy *ne = &s->enemies[i];
        ne->type = (uint8_t)e->type;
        ne->state = (uint8_t)e->state;
        ne->hp = quantize_points(e->hp);
        ne->x = e->x;
        ne->y = e->y;
    }

    for (int i = 0; i < w->items.count; i++) {
        if (w->items.items[i].active) {
            s->items_alive |= (uint64_t)1 << i;
        }
    }

    for (int i = 0; i < w->doors.count; i++) {
        s->doors[i] = quantize_openness(w->doors.doors[i].openness);
    }

    snapshot_set_events(s, w->events.items, w->events.count);
}

void snapshot_set_events(MsgSnapshot *s, const GameEvent *events, int count)
{
    int n = count < MAX_EVENTS ? count : MAX_EVENTS;
    memset(s->events, 0, sizeof(s->events));
    for (int i = 0; i < n; i++) {
        const GameEvent *ev = &events[i];
        NetEvent *ne = &s->events[i];
        ne->kind = ev->kind;
        ne->actor = ev->actor;
        ne->x = ev->x;
        ne->y = ev->y;
    }
    s->event_count = (uint8_t)n;
}

void snapshot_pick_events(MsgSnapshot *s, const GameEvent *events, int count,
                          const PlayerState *listener)
{
    GameEvent picked[MAX_EVENTS];
    float dist[MAX_EVENTS];
    int n = 0;
    for (int i = 0; i < count && n < MAX_EVENTS; i++) {
        if (event_is_own(&events[i], listener->id)) {
            picked[n++] = events[i];
        }
    }
    int own = n;
    /* The others are kept sorted by distance as they come in: each goes in
     * after every one as near as it, and once the list is full the farthest
     * falls off the end. */
    for (int i = 0; i < count; i++) {
        const GameEvent *e = &events[i];
        if (event_is_own(e, listener->id) ||
            event_gain(e, listener->id, listener->x, listener->y) <= 0.0f) {
            continue;
        }
        float d = event_distance(e, listener->x, listener->y);
        int at = n;
        while (at > own && dist[at - 1] > d) {
            at--;
        }
        if (at >= MAX_EVENTS) {
            continue;
        }
        int last = n < MAX_EVENTS ? n : MAX_EVENTS - 1;
        for (int j = last; j > at; j--) {
            picked[j] = picked[j - 1];
            dist[j] = dist[j - 1];
        }
        picked[at] = *e;
        dist[at] = d;
        if (n < MAX_EVENTS) {
            n++;
        }
    }
    snapshot_set_events(s, picked, n);
}

/* What a door is doing, judged by where its openness went: the snapshot
 * carries how far open it is, not which way it moves. */
static DoorState door_state_from(const Door *d, float openness)
{
    if (openness <= 0.0f) {
        return DOOR_CLOSED;
    }
    if (openness >= 1.0f) {
        return DOOR_OPEN;
    }
    if (openness > d->openness) {
        return DOOR_OPENING;
    }
    if (openness < d->openness) {
        return DOOR_CLOSING;
    }
    return d->state;
}

void snapshot_apply(World *w, const MsgSnapshot *s)
{
    w->tick = s->tick;

    snapshot_apply_characters(w, s);

    for (int i = 0; i < w->items.count; i++) {
        w->items.items[i].active = (int)((s->items_alive >> i) & 1u);
    }

    for (int i = 0; i < w->doors.count; i++) {
        Door *d = &w->doors.doors[i];
        float openness = (float)s->doors[i] / 255.0f;
        d->state = door_state_from(d, openness);
        d->openness = openness;
    }

    int events = s->event_count < MAX_EVENTS ? s->event_count : MAX_EVENTS;
    for (int i = 0; i < events; i++) {
        const NetEvent *ne = &s->events[i];
        GameEvent *ev = &w->events.items[i];
        ev->kind = ne->kind;
        ev->actor = ne->actor;
        ev->x = ne->x;
        ev->y = ne->y;
    }
    w->events.count = events;
}

void snapshot_apply_characters(World *w, const MsgSnapshot *s)
{
    int listed[MAX_PLAYERS] = {0};
    for (int i = 0; i < s->player_count && i < MAX_PLAYERS; i++) {
        const NetPlayer *np = &s->players[i];
        if (!player_valid(np, &s->yours)) {
            continue;
        }
        PlayerState *p = &w->players[np->id];
        if (!p->active) {
            player_init(p, 0, 0);
        }
        p->id = np->id;
        p->alive = np->alive ? 1 : 0;
        p->x = np->x;
        p->y = np->y;
        p->angle = np->angle;
        p->hp = (float)np->hp;
        p->armor = (float)np->armor;
        p->weapons.current = np->weapon;
        listed[np->id] = 1;
        if (np->id >= w->player_count) {
            w->player_count = np->id + 1;
        }
    }
    if (s->yours.id < MAX_PLAYERS && listed[s->yours.id]) {
        WeaponSystem *ws = &w->players[s->yours.id].weapons;
        for (int k = 0; k < WEAPON_COUNT; k++) {
            ws->weapons[k].owned = (s->yours.owned >> k) & 1u;
            ws->weapons[k].ammo = s->yours.ammo[k];
        }
    }
    /* Freed as world_remove_player() frees a slot, but without letting go of
     * local_player: which player is this client's is not the snapshot's call. */
    for (int i = 0; i < w->player_count; i++) {
        if (!listed[i]) {
            w->players[i].active = 0;
            w->players[i].alive = 0;
        }
    }

    int enemies = s->enemy_count < MAX_ENEMIES ? s->enemy_count : MAX_ENEMIES;
    for (int i = 0; i < enemies; i++) {
        const NetEnemy *ne = &s->enemies[i];
        if (i >= w->enemies.count) {
            enemy_spawn(&w->enemies, ne->x, ne->y, ne->type);
        }
        Enemy *e = &w->enemies.items[i];
        e->type = ne->type;
        e->state = (EnemyState)ne->state;
        e->hp = (float)ne->hp;
        e->x = ne->x;
        e->y = ne->y;
    }
    w->enemies.count = enemies;
}

size_t snapshot_encode(const MsgSnapshot *s, void *out)
{
    if (s->player_count > MAX_PLAYERS || s->enemy_count > MAX_ENEMIES ||
        s->event_count > MAX_EVENTS) {
        return 0;
    }
    uint8_t *p = out;
    size_t n;

    n = offsetof(MsgSnapshot, players);
    memcpy(p, s, n);
    p += n;
    n = s->player_count * sizeof(NetPlayer);
    memcpy(p, s->players, n);
    p += n;
    n = s->enemy_count * sizeof(NetEnemy);
    memcpy(p, s->enemies, n);
    p += n;
    memcpy(p, &s->items_alive, sizeof(s->items_alive));
    p += sizeof(s->items_alive);
    memcpy(p, s->doors, sizeof(s->doors));
    p += sizeof(s->doors);
    n = s->event_count * sizeof(NetEvent);
    memcpy(p, s->events, n);
    p += n;

    return (size_t)(p - (uint8_t *)out);
}

/* Every byte of a decoded snapshot that snapshot_apply() uses as an index or
 * hands on as an enum is in range. */
static int snapshot_valid(const MsgSnapshot *s)
{
    if (s->yours.id >= MAX_PLAYERS || (s->yours.owned >> WEAPON_COUNT) != 0) {
        return 0;
    }
    for (int i = 0; i < s->player_count; i++) {
        if (!player_valid(&s->players[i], &s->yours)) {
            return 0;
        }
    }
    for (int i = 0; i < s->enemy_count; i++) {
        if (s->enemies[i].type >= ENEMY_COUNT || s->enemies[i].state > ESTATE_DEAD) {
            return 0;
        }
    }
    for (int i = 0; i < s->event_count; i++) {
        if (s->events[i].kind >= EV_COUNT) {
            return 0;
        }
    }
    return 1;
}

int snapshot_decode(MsgSnapshot *s, const void *data, size_t size)
{
    const uint8_t *p = data;
    size_t n;

    memset(s, 0, sizeof(*s));
    n = offsetof(MsgSnapshot, players);
    if (!data || size < n) {
        return -1;
    }
    memcpy(s, p, n);
    p += n;
    if (s->h.type != MSG_SNAPSHOT || s->player_count > MAX_PLAYERS ||
        s->enemy_count > MAX_ENEMIES || s->event_count > MAX_EVENTS ||
        size != wire_size(s->player_count, s->enemy_count, s->event_count)) {
        memset(s, 0, sizeof(*s));
        return -1;
    }

    n = s->player_count * sizeof(NetPlayer);
    memcpy(s->players, p, n);
    p += n;
    n = s->enemy_count * sizeof(NetEnemy);
    memcpy(s->enemies, p, n);
    p += n;
    memcpy(&s->items_alive, p, sizeof(s->items_alive));
    p += sizeof(s->items_alive);
    memcpy(s->doors, p, sizeof(s->doors));
    p += sizeof(s->doors);
    n = s->event_count * sizeof(NetEvent);
    memcpy(s->events, p, n);

    if (!snapshot_valid(s)) {
        memset(s, 0, sizeof(*s));
        return -1;
    }
    return 0;
}
