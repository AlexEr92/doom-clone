#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

/* What the server and its clients say to each other: one struct per message,
 * sent as the bytes it lies in memory as. Nothing is converted on the way —
 * every platform the game targets (x86-64, ARM64) is little-endian, and the
 * two ends are the same build (docs/requirements.md, sections 1 and 6). A peer
 * of the other byte order is turned away at the handshake, because it reads
 * PROTO_VERSION byte-swapped.
 *
 * Padding is spelled out as _pad fields, so a message built from a zeroed
 * struct carries zeroes there and not whatever the stack held, and every size
 * is pinned by an assertion below: a field added or widened stops the build
 * instead of shifting what the other end reads.
 *
 * The snapshot is the one message not sent as is: see snapshot_encode(). */

#include "config.h"
#include "event.h"
#include <stddef.h>
#include <stdint.h>

struct World;
struct PlayerState;

/* Raised with every change to anything in this file a peer reads. */
#define PROTO_VERSION 1

#define NET_NAME_MAX 16 /* player name, terminating NUL included */
#define NET_MAP_MAX 32  /* map file name, terminating NUL included */

typedef enum {
    MSG_JOIN = 1,
    MSG_ACCEPT,
    MSG_REJECT,
    MSG_INPUT,
    MSG_SNAPSHOT,
    MSG_DISCONNECT, /* a MsgHeader alone */
} MsgType;

/* MsgReject.reason */
typedef enum {
    REJECT_SERVER_FULL = 1, /* every one of MAX_PLAYERS slots is taken */
    REJECT_VERSION,         /* MsgJoin.proto is not this build's PROTO_VERSION */
} RejectReason;

typedef struct {
    uint8_t type; /* MsgType */
    uint8_t _pad[3];
} MsgHeader;

/* ---- reliable, NET_CH_RELIABLE ---- */

typedef struct {
    MsgHeader h;
    uint16_t proto; /* PROTO_VERSION */
    char name[NET_NAME_MAX];
} MsgJoin;

typedef struct {
    MsgHeader h;
    uint8_t your_id; /* the player id the client plays as */
    uint8_t _pad[3];
    uint32_t seed;         /* what the server seeds its rand() with */
    char map[NET_MAP_MAX]; /* a file name under assets/maps/, not a path */
} MsgAccept;

typedef struct {
    MsgHeader h;
    uint8_t reason; /* RejectReason */
} MsgReject;

/* ---- unreliable, NET_CH_UNRELIABLE ---- */

/* PlayerInput (input.h) as it goes on the wire. */
typedef struct {
    MsgHeader h;
    uint32_t tick;
    uint16_t buttons; /* BTN_* */
    uint8_t _pad[2];
    float angle;
} MsgInput;

/* One player who is in the world. hp and armor are whole points, rounded up:
 * a player with any hp left never arrives with 0. */
typedef struct {
    uint8_t id;     /* index into World.players */
    uint8_t flags;  /* no bits defined yet; always 0 */
    uint8_t weapon; /* WeaponType in hand */
    uint8_t alive;
    float x, y, angle;
    uint8_t hp, armor;
    uint16_t ammo;   /* of the weapon in hand */
    uint16_t frags;  /* not counted by the simulation yet; always 0 */
    uint16_t deaths; /* likewise */
} NetPlayer;

/* An enemy, corpses included; its index in MsgSnapshot.enemies is its id. */
typedef struct {
    uint8_t type;  /* EnemyType */
    uint8_t state; /* EnemyState */
    uint8_t hp;    /* whole points, rounded up like NetPlayer.hp */
    uint8_t _pad;
    float x, y;
} NetEnemy;

/* A GameEvent (event.h). `arg` is the third byte whose meaning depends on the
 * kind; GameEvent has no such byte yet, so it is always 0. */
typedef struct {
    uint8_t kind; /* GameEventKind */
    uint8_t actor;
    uint8_t arg;
    uint8_t _pad;
    float x, y;
} NetEvent;

/* The whole world as one tick left it — no deltas: it fits in one packet.
 *
 * In memory every list has room for its limit. On the wire each one is cut to
 * its count instead: the fields go out in the order below with nothing between
 * them, players[], enemies[] and events[] carrying player_count, enemy_count
 * and event_count entries. That is snapshot_encode()'s business; the struct is
 * never sent as it is. */
typedef struct {
    MsgHeader h;
    uint32_t tick;            /* World.tick the snapshot was taken at */
    uint32_t last_input_tick; /* the newest input of the receiver's the server has used */
    uint8_t player_count, enemy_count, event_count, _pad;
    NetPlayer players[MAX_PLAYERS]; /* the players in the world, by ascending id */
    NetEnemy enemies[MAX_ENEMIES];
    uint64_t items_alive;     /* bit i: World.items.items[i] is still there */
    uint8_t doors[MAX_DOORS]; /* openness of each door, 0 closed .. 255 open */
    NetEvent events[MAX_EVENTS];
} MsgSnapshot;

/* Fill `s` from the world as it stands: every player in the world, every
 * enemy, the items, the doors, and the events of the last tick. `s` is zeroed
 * first, padding and unused slots included.
 *
 * The events are those of the last world_step() only. A server that sends
 * snapshots less often than it ticks replaces them with the ones it collected
 * in between, through snapshot_set_events() after this call. */
void snapshot_write(MsgSnapshot *s, const struct World *w, uint32_t last_input_tick);

/* Make the first `count` of `events` the snapshot's events, as many as
 * MAX_EVENTS holds; the rest are dropped. */
void snapshot_set_events(MsgSnapshot *s, const GameEvent *events, int count);

/* Make the snapshot's events those of `events` that `listener` hears
 * (event_gain() in event.h): their own first, as they happened, then the rest
 * nearest first, as many as MAX_EVENTS holds. Events at one distance keep the
 * order they happened in. With ten players firing, the events of the ticks
 * between two snapshots outnumber what one carries, and what a player has to
 * hear is the fight next to them, not whichever came first. */
void snapshot_pick_events(MsgSnapshot *s, const GameEvent *events, int count,
                          const struct PlayerState *listener);

/* Put the state in `s` into `w`, which must have been loaded with the map the
 * server runs: items and doors are matched by index, and their count comes
 * from the map. Whoever `s` does not list is taken out of the world — that is
 * how a client learns someone has left — but w->local_player is left alone.
 *
 * Only what a snapshot carries is set; the rest of each entity keeps what it
 * had, and a player who appears in a slot that was empty starts from
 * player_init(). Door states are inferred from openness: 0 is closed, 255 is
 * open, and in between the door is moving the way its openness changed. The
 * events replace w->events. */
void snapshot_apply(struct World *w, const MsgSnapshot *s);

/* The part of snapshot_apply() that puts the players and the enemies of `s`
 * into `w`, by the same rules, and nothing else: the tick, the items, the
 * doors and the events stay as they are. A client draws the characters of an
 * older snapshot than the rest of the world (net_interp.h). */
void snapshot_apply_characters(struct World *w, const MsgSnapshot *s);

/* Lay `s` out for the wire, each list cut to its count, into `out`, which has
 * room for sizeof(MsgSnapshot) bytes — the encoded snapshot is never longer.
 * Returns the number of bytes written, or 0 when a count is past its limit. */
size_t snapshot_encode(const MsgSnapshot *s, void *out);

/* Read a snapshot of `size` bytes, as snapshot_encode() lays it out, into `s`.
 * Returns 0, or -1 with `s` zeroed when the bytes are not a snapshot: too
 * short, another type, a count past its limit, a size that does not match the
 * counts, or an id, weapon, enemy type or state, or event kind out of range.
 * Positions are not checked — the server is trusted with those. */
int snapshot_decode(MsgSnapshot *s, const void *data, size_t size);

/* ---- the layout, pinned ---- */

_Static_assert(sizeof(MsgHeader) == 4, "MsgHeader changed size");
_Static_assert(sizeof(MsgJoin) == 22, "MsgJoin changed size");
_Static_assert(sizeof(MsgAccept) == 44, "MsgAccept changed size");
_Static_assert(sizeof(MsgReject) == 5, "MsgReject changed size");
_Static_assert(sizeof(MsgInput) == 16, "MsgInput changed size");
_Static_assert(sizeof(NetPlayer) == 24, "NetPlayer changed size");
_Static_assert(sizeof(NetEnemy) == 12, "NetEnemy changed size");
_Static_assert(sizeof(NetEvent) == 12, "NetEvent changed size");

/* A field added to the snapshot, or one the limits pushed off its alignment,
 * shows up here: snapshot_encode() and snapshot_decode() copy field by field
 * and have to be told about it. */
_Static_assert(offsetof(MsgSnapshot, players) == 16, "the snapshot head changed size");
_Static_assert(sizeof(MsgSnapshot) == 16 + MAX_PLAYERS * sizeof(NetPlayer) +
                                              MAX_ENEMIES * sizeof(NetEnemy) + sizeof(uint64_t) +
                                              MAX_DOORS + MAX_EVENTS * sizeof(NetEvent),
               "MsgSnapshot changed: check snapshot_encode() and snapshot_decode()");

/* N9. A snapshot with every list full must still go out as one packet. */
_Static_assert(sizeof(MsgSnapshot) <= SNAPSHOT_MAX_BYTES,
               "a full snapshot no longer fits in SNAPSHOT_MAX_BYTES");

_Static_assert(MAX_PLAYERS <= 255 && MAX_ENEMIES <= 255 && MAX_EVENTS <= 255,
               "the counts in a snapshot are uint8_t");

/* A peer of the other byte order reads the version with its bytes swapped,
 * which tells it apart only while the two bytes differ. */
_Static_assert((PROTO_VERSION & 0xff) != ((PROTO_VERSION >> 8) & 0xff),
               "PROTO_VERSION must not read the same in both byte orders");

#endif
