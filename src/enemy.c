#include "enemy.h"
#include "sprite.h"
#include "door.h"
#include "audio.h"
#include "utils.h"
#include <math.h>
#include <string.h>

static const EnemyDef defs[ENEMY_COUNT] = {
    [ENEMY_IMP]  = {
        .max_hp = 30.0f, .speed = 1.8f, .damage = 8.0f,
        .attack_range = 1.2f, .attack_cd = 1.0f, .detect_range = 8.0f,
        .sprite_type = SPRITE_ENEMY, .sprite_scale = 1.0f,
    },
    [ENEMY_SERG] = {
        .max_hp = 60.0f, .speed = 1.2f, .damage = 14.0f,
        .attack_range = 6.0f, .attack_cd = 1.6f, .detect_range = 10.0f,
        .sprite_type = SPRITE_ENEMY_SERG, .sprite_scale = 1.0f,
    },
};

const EnemyDef *enemy_def(int type) {
    if (type < 0 || type >= ENEMY_COUNT) return &defs[0];
    return &defs[type];
}

void enemy_list_init(EnemyList *el) {
    memset(el, 0, sizeof(*el));
}

int enemy_spawn(EnemyList *el, SpriteList *sl, float x, float y, int type) {
    if (el->count >= MAX_ENEMIES) return -1;
    if (type < 0 || type >= ENEMY_COUNT) type = ENEMY_IMP;
    const EnemyDef *d = &defs[type];
    /* Use sprite_add so the per-type default scale (human height, floor
     * anchored) is applied. */
    int sid = sprite_add(sl, x, y, d->sprite_type);
    if (sid < 0) return -1;

    Enemy *e = &el->items[el->count];
    e->type = type;
    e->state = ESTATE_IDLE;
    e->x = x;
    e->y = y;
    e->hp = d->max_hp;
    e->anim_timer = 0.0f;
    e->attack_cooldown = 0.0f;
    e->sprite_id = sid;
    e->alert_timer = 0.0f;
    return el->count++;
}

/* Lightweight DDA line-of-sight: returns 1 if no wall between (x0,y0) and (x1,y1). */
static int line_of_sight(const Map *m, DoorList *dl, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1e-3f) return 1;
    int steps = (int)(dist / 0.1f);
    if (steps < 1) steps = 1;
    float sx = dx / (float)steps;
    float sy = dy / (float)steps;
    float cx = x0, cy = y0;
    for (int i = 0; i < steps; i++) {
        cx += sx;
        cy += sy;
        if (map_is_wall_door(m, dl, cx, cy)) return 0;
    }
    return 1;
}

static void move_towards(Enemy *e, const Map *m, DoorList *dl, float tx, float ty, double dt) {
    const EnemyDef *d = &defs[e->type];
    float dx = tx - e->x;
    float dy = ty - e->y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1e-3f) return;
    float step = d->speed * (float)dt;
    if (step > dist) step = dist;
    float nx = e->x + dx / dist * step;
    float ny = e->y + dy / dist * step;
    /* slide: try X then Y with a small radius */
    float r = 0.2f;
    if (!map_is_wall_door(m, dl, nx + (nx > e->x ? r : -r), e->y)) e->x = nx;
    if (!map_is_wall_door(m, dl, e->x, ny + (ny > e->y ? r : -r))) e->y = ny;
}

void enemy_update_all(EnemyList *el, SpriteList *sl, const Map *m,
                      DoorList *dl, Player *pl, Audio *au, double dt) {
    (void)sl;
    for (int i = 0; i < el->count; i++) {
        Enemy *e = &el->items[i];
        if (e->state == ESTATE_DEAD) continue;
        const EnemyDef *d = &defs[e->type];

        float dx = pl->x - e->x;
        float dy = pl->y - e->y;
        float dist = sqrtf(dx * dx + dy * dy);
        int can_see = (dist <= d->detect_range) && line_of_sight(m, dl, e->x, e->y, pl->x, pl->y);

        if (e->attack_cooldown > 0.0f) e->attack_cooldown -= (float)dt;
        e->anim_timer += (float)dt;

        switch (e->state) {
            case ESTATE_IDLE:
                if (can_see) {
                    e->state = ESTATE_ALERT;
                    e->alert_timer = 0.4f;
                }
                break;
            case ESTATE_ALERT:
                if (!can_see) { e->state = ESTATE_IDLE; break; }
                e->alert_timer -= (float)dt;
                if (e->alert_timer <= 0.0f) e->state = ESTATE_CHASE;
                break;
            case ESTATE_CHASE:
                if (can_see) {
                    if (dist <= d->attack_range) e->state = ESTATE_ATTACK;
                    else move_towards(e, m, dl, pl->x, pl->y, dt);
                } else {
                    /* lost sight: keep moving towards last known pos (player's current) briefly */
                    move_towards(e, m, dl, pl->x, pl->y, dt);
                }
                break;
            case ESTATE_ATTACK:
                if (!can_see) { e->state = ESTATE_CHASE; break; }
                if (dist > d->attack_range * 1.3f) { e->state = ESTATE_CHASE; break; }
                if (e->attack_cooldown <= 0.0f) {
                    /* hit: armor absorbs a fraction of damage first */
                    float dmg = d->damage;
                    if (pl->armor > 0.0f) {
                        float absorbed = dmg * 0.5f;
                        if (absorbed > pl->armor) absorbed = pl->armor;
                        pl->armor -= absorbed;
                        dmg -= absorbed;
                    }
                    pl->hp -= dmg;
                    if (pl->hp < 0.0f) pl->hp = 0.0f;
                    if (au) audio_play(au, SND_PLAYER_HURT, dist, 12.0f);
                    e->attack_cooldown = d->attack_cd;
                }
                break;
            case ESTATE_DEAD:
                break;
        }
        /* keep sprite position in sync */
        if (e->sprite_id >= 0 && e->sprite_id < sl->count) {
            sl->items[e->sprite_id].x = e->x;
            sl->items[e->sprite_id].y = e->y;
        }
    }
}

void enemy_damage(EnemyList *el, SpriteList *sl, int idx, float dmg,
                  Audio *au, float px, float py) {
    if (idx < 0 || idx >= el->count) return;
    Enemy *e = &el->items[idx];
    if (e->state == ESTATE_DEAD) return;
    e->hp -= dmg;
    if (e->hp <= 0.0f) {
        e->hp = 0.0f;
        e->state = ESTATE_DEAD;
        /* switch sprite to corpse; floor-anchored via sprite scale by type */
        if (e->sprite_id >= 0 && e->sprite_id < sl->count) {
            Sprite *sp = &sl->items[e->sprite_id];
            sp->type = SPRITE_ENEMY_DEAD;
            sp->scale = 0.55f;
            sp->vmove = 0;
        }
        if (au) {
            float dx = e->x - px, dy = e->y - py;
            float dist = sqrtf(dx * dx + dy * dy);
            audio_play(au, SND_ENEMY_DEATH, dist, 16.0f);
        }
    } else {
        /* being shot alerts/chases immediately */
        if (e->state == ESTATE_IDLE || e->state == ESTATE_ALERT) {
            e->state = ESTATE_CHASE;
        }
        if (au) {
            float dx = e->x - px, dy = e->y - py;
            float dist = sqrtf(dx * dx + dy * dy);
            audio_play(au, SND_ENEMY_HURT, dist, 16.0f);
        }
    }
}

int enemy_all_dead(const EnemyList *el) {
    for (int i = 0; i < el->count; i++) {
        if (el->items[i].state != ESTATE_DEAD) return 0;
    }
    return el->count > 0;
}