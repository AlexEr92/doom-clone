#include "enemy.h"
#include "raycast_world.h"
#include "door.h"
#include "utils.h"
#include <math.h>
#include <string.h>

/* CHASE without line of sight: how long the enemy keeps heading for the last
 * known position, and how close to it counts as having arrived. */
#define ENEMY_SEARCH_TIME 3.0f
#define ENEMY_SEARCH_REACH 0.3f

/* How often a chase looks around for a better target. Looking every tick, as
 * IDLE and ALERT do, makes an enemy standing between two players at the same
 * distance flip between them every tick and go nowhere. */
#define ENEMY_RETARGET_TIME 0.5f

static const EnemyDef defs[ENEMY_COUNT] = {
        [ENEMY_IMP] =
                {
                        .max_hp = 30.0f,
                        .speed = 1.8f,
                        .damage = 8.0f,
                        .attack_range = 1.2f,
                        .attack_cd = 1.0f,
                        .detect_range = 8.0f,
                },
        [ENEMY_SERG] =
                {
                        .max_hp = 60.0f,
                        .speed = 1.2f,
                        .damage = 14.0f,
                        .attack_range = 6.0f,
                        .attack_cd = 1.6f,
                        .detect_range = 10.0f,
                },
};

const EnemyDef *enemy_def(int type)
{
    if (type < 0 || type >= ENEMY_COUNT) {
        return &defs[0];
    }
    return &defs[type];
}

void enemy_list_init(EnemyList *el)
{
    memset(el, 0, sizeof(*el));
}

int enemy_spawn(EnemyList *el, float x, float y, int type)
{
    if (el->count >= MAX_ENEMIES) {
        return -1;
    }
    if (type < 0 || type >= ENEMY_COUNT) {
        type = ENEMY_IMP;
    }
    const EnemyDef *d = &defs[type];

    Enemy *e = &el->items[el->count];
    e->type = type;
    e->state = ESTATE_IDLE;
    e->x = x;
    e->y = y;
    e->hp = d->max_hp;
    e->anim_timer = 0.0f;
    e->attack_cooldown = 0.0f;
    e->alert_timer = 0.0f;
    e->target = -1;
    e->retarget_timer = 0.0f;
    e->last_seen_x = x;
    e->last_seen_y = y;
    e->search_timer = 0.0f;
    return el->count++;
}

/* Someone worth going after: in the world, not dead, with hp left. */
static int is_targetable(const PlayerState *p)
{
    return p->active && p->alive && p->hp > 0.0f;
}

/* Point the enemy at `target` and hand it a fresh search. last_seen_x/y and
 * search_timer belong to the target rather than to the enemy, so they start at
 * (x,y) instead of carrying over from whoever was being chased before. */
static void set_target(Enemy *e, int target, float x, float y)
{
    e->target = target;
    e->last_seen_x = x;
    e->last_seen_y = y;
    e->search_timer = ENEMY_SEARCH_TIME;
}

/* The player the enemy is after, or NULL if it has none or the one it had is
 * gone. */
static PlayerState *target_of(const Enemy *e, PlayerState *players, int player_count)
{
    if (e->target < 0 || e->target >= player_count) {
        return NULL;
    }
    PlayerState *p = &players[e->target];
    return is_targetable(p) ? p : NULL;
}

/* Whether the enemy can see `p`, with the distance to it either way. A NULL
 * player is nobody and is never seen. */
static int can_see_player(const Enemy *e, const EnemyDef *d, const Map *m, DoorList *dl,
                          const PlayerState *p, float *out_dist)
{
    if (!p) {
        *out_dist = 0.0f;
        return 0;
    }
    float dx = p->x - e->x;
    float dy = p->y - e->y;
    float dist = sqrtf(dx * dx + dy * dy);
    *out_dist = dist;
    return (dist <= d->detect_range) && world_line_of_sight(m, dl, e->x, e->y, p->x, p->y);
}

/* The nearest player the enemy can see, or -1. Ties go to the lower id: two
 * players at the same distance must not be told apart by anything as fragile
 * as the order the array happens to be walked in. */
static int pick_target(const Enemy *e, const EnemyDef *d, const Map *m, DoorList *dl,
                       PlayerState *players, int player_count, float *out_dist)
{
    int best = -1;
    float best_dist = 0.0f;
    for (int i = 0; i < player_count; i++) {
        PlayerState *p = &players[i];
        if (!is_targetable(p)) {
            continue;
        }
        float dist;
        if (!can_see_player(e, d, m, dl, p, &dist)) {
            continue;
        }
        if (best >= 0 && dist >= best_dist) {
            continue;
        }
        best = i;
        best_dist = dist;
    }
    *out_dist = best_dist;
    return best;
}

static void move_towards(Enemy *e, const Map *m, DoorList *dl, float tx, float ty, double dt)
{
    const EnemyDef *d = &defs[e->type];
    float dx = tx - e->x;
    float dy = ty - e->y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1e-3f) {
        return;
    }
    float step = d->speed * (float)dt;
    if (step > dist) {
        step = dist;
    }
    float nx = e->x + dx / dist * step;
    float ny = e->y + dy / dist * step;
    /* slide: try X then Y with a small radius */
    float r = 0.2f;
    if (!map_is_wall_door(m, dl, nx + (nx > e->x ? r : -r), e->y)) {
        e->x = nx;
    }
    if (!map_is_wall_door(m, dl, e->x, ny + (ny > e->y ? r : -r))) {
        e->y = ny;
    }
}

void enemy_update_all(EnemyList *el, const Map *m, DoorList *dl, PlayerState *players,
                      int player_count, EventQueue *evq, double dt)
{
    for (int i = 0; i < el->count; i++) {
        Enemy *e = &el->items[i];
        if (e->state == ESTATE_DEAD) {
            continue;
        }
        const EnemyDef *d = &defs[e->type];

        if (e->retarget_timer > 0.0f) {
            e->retarget_timer -= (float)dt;
        }

        /* Where the target it already has stands, if it still has one. */
        float dist;
        PlayerState *pl = target_of(e, players, player_count);
        int can_see = can_see_player(e, d, m, dl, pl, &dist);

        /* IDLE and ALERT choose again every tick; a chase keeps what it has
         * until the timer runs out, or until the target dies or slips out of
         * sight. */
        int chasing = (e->state == ESTATE_CHASE || e->state == ESTATE_ATTACK);
        if (!chasing || !can_see || e->retarget_timer <= 0.0f) {
            float pick_dist;
            int pick = pick_target(e, d, m, dl, players, player_count, &pick_dist);
            if (pick != e->target) {
                if (pick >= 0) {
                    set_target(e, pick, players[pick].x, players[pick].y);
                } else {
                    /* nobody in sight: the search of the last known position
                     * goes on with what it already has */
                    e->target = -1;
                }
            }
            pl = (pick >= 0) ? &players[pick] : NULL;
            dist = pick_dist;
            can_see = (pick >= 0);
            e->retarget_timer = ENEMY_RETARGET_TIME;
        }

        if (can_see) {
            e->last_seen_x = pl->x;
            e->last_seen_y = pl->y;
            e->search_timer = ENEMY_SEARCH_TIME;
        }

        if (e->attack_cooldown > 0.0f) {
            e->attack_cooldown -= (float)dt;
        }
        e->anim_timer += (float)dt;

        switch (e->state) {
            case ESTATE_IDLE:
                if (can_see) {
                    e->state = ESTATE_ALERT;
                    e->alert_timer = 0.4f;
                }
                break;
            case ESTATE_ALERT:
                if (!can_see) {
                    e->state = ESTATE_IDLE;
                    break;
                }
                e->alert_timer -= (float)dt;
                if (e->alert_timer <= 0.0f) {
                    e->state = ESTATE_CHASE;
                }
                break;
            case ESTATE_CHASE:
                if (can_see) {
                    if (dist <= d->attack_range) {
                        e->state = ESTATE_ATTACK;
                    } else {
                        move_towards(e, m, dl, pl->x, pl->y, dt);
                    }
                } else {
                    /* lost sight: search the last known position, give up after
                     * reaching it or running out of time */
                    e->search_timer -= (float)dt;
                    float sdx = e->last_seen_x - e->x;
                    float sdy = e->last_seen_y - e->y;
                    if (e->search_timer <= 0.0f ||
                        sdx * sdx + sdy * sdy <= ENEMY_SEARCH_REACH * ENEMY_SEARCH_REACH) {
                        e->search_timer = 0.0f;
                        e->state = ESTATE_IDLE;
                        break;
                    }
                    move_towards(e, m, dl, e->last_seen_x, e->last_seen_y, dt);
                }
                break;
            case ESTATE_ATTACK:
                if (!can_see) {
                    e->state = ESTATE_CHASE;
                    break;
                }
                if (dist > d->attack_range * 1.3f) {
                    e->state = ESTATE_CHASE;
                    break;
                }
                if (e->attack_cooldown <= 0.0f) {
                    /* no player dealt it, and the enemy is the source: the
                     * client measures the distance from its own player to it */
                    player_damage(pl, d->damage, -1, e->x, e->y, evq);
                    e->attack_cooldown = d->attack_cd;
                }
                break;
            case ESTATE_DEAD: break;
        }
    }
}

void enemy_damage(EnemyList *el, int idx, float dmg, EventQueue *evq, int attacker, float px,
                  float py)
{
    if (idx < 0 || idx >= el->count) {
        return;
    }
    Enemy *e = &el->items[idx];
    if (e->state == ESTATE_DEAD) {
        return;
    }
    e->hp -= dmg;
    if (e->hp <= 0.0f) {
        e->hp = 0.0f;
        e->state = ESTATE_DEAD;
        event_push(evq, EV_ENEMY_DEATH, idx, e->x, e->y);
    } else {
        /* being shot alerts/chases immediately */
        if (e->state == ESTATE_IDLE || e->state == ESTATE_ALERT) {
            e->state = ESTATE_CHASE;
        }
        if (attacker >= 0) {
            /* whoever fired takes over as the target, and holds it for a full
             * retarget interval: without that the next tick would hand the
             * enemy straight back to the nearest player it can see */
            set_target(e, attacker, px, py);
            e->retarget_timer = ENEMY_RETARGET_TIME;
        } else {
            e->last_seen_x = px;
            e->last_seen_y = py;
            e->search_timer = ENEMY_SEARCH_TIME;
        }
        event_push(evq, EV_ENEMY_HURT, idx, e->x, e->y);
    }
}

int enemy_all_dead(const EnemyList *el)
{
    for (int i = 0; i < el->count; i++) {
        if (el->items[i].state != ESTATE_DEAD) {
            return 0;
        }
    }
    return el->count > 0;
}
