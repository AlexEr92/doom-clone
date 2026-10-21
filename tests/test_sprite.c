/* sprite.c: which character draws each entity, which animation it plays and
 * which frame of it shows — everything the client works out about a sprite
 * before a pixel is drawn.
 *
 * sprite_render() is not tested: a framebuffer is what would show it wrong.
 * zBuffer, which it reads, is defined here so that raycast.c and the whole
 * renderer behind it stay out of the link. The world and the sheets are built
 * by hand: the clocks read only positions, hp, states and what a shot resets
 * out of the one, and only the length of each sheet out of the other. enemy.c
 * is linked for enemy_def(), assets.c for the skin names. */

#include "unity.h"
#include "sprite.h"
#include "world.h"
#include <math.h>
#include <string.h>

float zBuffer[SCREEN_W];

/* player.c comes in behind enemy.c, whose attack calls player_damage(); its
 * player_init() calls this, and weapon.c would bring the hitscan with it. No
 * test here runs either: the players are built by hand. */
void weapon_system_init(WeaponSystem *ws)
{
    memset(ws, 0, sizeof(*ws));
}

#define FRAME_DT (1.0f / 60.0f)

/* Frames in each sheet, as the files in assets/players/ have them. */
static const int SHEET_COLS[ANIM_COUNT] = {
        [ANIM_IDLE] = 1, [ANIM_RUN] = 4, [ANIM_SHOOT] = 5, [ANIM_PAIN] = 5, [ANIM_DEATH] = 5,
};

static World w;
static Assets a;
static SpriteAnim sa;
static SpriteList sl;

/* skins.txt as the fixture has it: the sergeant's character first, so that a
 * skin taken from the enemy type rather than from the name would show. */
static void list_skins(const char *const names[], int n)
{
    memset(a.skin_names, 0, sizeof(a.skin_names));
    a.skin_count = n;
    for (int i = 0; i < n; i++) {
        strcpy(a.skin_names[i], names[i]);
    }
    sprite_anim_init(&sa, &a);
}

void setUp(void)
{
    memset(&w, 0, sizeof(w));
    memset(&a, 0, sizeof(a));
    for (int s = 0; s < MAX_SKINS; s++) {
        for (int k = 0; k < ANIM_COUNT; k++) {
            a.skin_anim[s][k].cols = SHEET_COLS[k];
        }
    }
    a.missing.cols = 1;
    static const char *const names[] = {"swat", "marine"};
    list_skins(names, 2);
    sprite_init(&sl);

    /* The player the camera looks through, out of the way of everyone. */
    w.local_player = 0;
    w.player_count = 1;
    w.players[0].active = 1;
    w.players[0].alive = 1;
    w.players[0].hp = 100.0f;
}

void tearDown(void)
{}

static PlayerState *add_player(int id, float x, float y)
{
    PlayerState *p = &w.players[id];
    p->id = (uint8_t)id;
    p->active = 1;
    p->alive = 1;
    p->x = x;
    p->y = y;
    p->hp = 100.0f;
    if (w.player_count <= id) {
        w.player_count = id + 1;
    }
    return p;
}

static Enemy *add_enemy(int type, float x, float y)
{
    Enemy *e = &w.enemies.items[w.enemies.count++];
    e->type = type;
    e->state = ESTATE_IDLE;
    e->x = x;
    e->y = y;
    e->hp = enemy_def(type)->max_hp;
    return e;
}

/* One rendered frame, as main.c runs it. */
static void frame(float dt)
{
    sprite_anim_update(&sa, &w, dt);
    sprites_build_from_world(&sl, &w, w.local_player, &sa, &a);
}

static void frames(int n, float dt)
{
    for (int i = 0; i < n; i++) {
        frame(dt);
    }
}

/* Enemies go into the list first, in id order, and nothing else is on the
 * fixture's map; players come after them, in id order too. */
static const Sprite *enemy_sprite(int id)
{
    return &sl.items[id];
}

static const Sprite *player_sprite(int id)
{
    int idx = w.enemies.count;
    for (int i = 0; i < id; i++) {
        if (i != w.local_player && w.players[i].active) {
            idx++;
        }
    }
    TEST_ASSERT_TRUE(idx < sl.count);
    TEST_ASSERT_EQUAL_INT(SPRITE_PLAYER, sl.items[idx].type);
    return &sl.items[idx];
}

/* ---- Skins ---- */

static void test_imp_and_sergeant_draw_with_their_own_skins(void)
{
    Enemy *imp = add_enemy(ENEMY_IMP, 5.0f, 5.0f);
    Enemy *serg = add_enemy(ENEMY_SERG, 7.0f, 5.0f);
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(1, enemy_sprite(0)->skin); /* marine */
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(1)->skin); /* swat */

    /* and a corpse is the same character, dying */
    imp->state = ESTATE_DEAD;
    serg->state = ESTATE_DEAD;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(1, enemy_sprite(0)->skin);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(1)->skin);
    TEST_ASSERT_EQUAL_INT(ANIM_DEATH, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(ANIM_DEATH, enemy_sprite(1)->anim);
}

/* A character the list does not name is not drawn as somebody else. */
static void test_enemy_with_no_listed_skin_gets_none(void)
{
    static const char *const names[] = {"swat"};
    list_skins(names, 1);
    add_enemy(ENEMY_IMP, 5.0f, 5.0f);
    add_enemy(ENEMY_SERG, 7.0f, 5.0f);
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(-1, enemy_sprite(0)->skin);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(0)->frame);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(1)->skin);
}

/* Nine other players, nine skins, each by id, each animating on its own. */
static void test_nine_players_draw_apart_and_animate_apart(void)
{
    for (int id = 1; id < MAX_PLAYERS; id++) {
        add_player(id, 2.0f + (float)id, 8.0f);
    }
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(MAX_PLAYERS - 1, sl.count);
    for (int id = 1; id < MAX_PLAYERS; id++) {
        TEST_ASSERT_EQUAL_INT(id, player_sprite(id)->skin);
    }

    for (int i = 0; i < 20; i++) {
        w.players[3].x += 0.05f;
        if (i == 15) {
            w.players[6].weapons.weapons[WEAPON_PISTOL].anim = 1.0f;
        }
        frame(FRAME_DT);
    }
    for (int id = 1; id < MAX_PLAYERS; id++) {
        int expected = id == 3 ? ANIM_RUN : (id == 6 ? ANIM_SHOOT : ANIM_IDLE);
        TEST_ASSERT_EQUAL_INT(expected, player_sprite(id)->anim);
    }
}

/* Every client of a match draws one player as one character, whoever the
 * client itself plays: the skin goes by the player's id, not by where the
 * player stands in the list of the ones this client draws. */
static void test_a_player_wears_one_skin_on_every_client(void)
{
    for (int id = 1; id < MAX_PLAYERS; id++) {
        add_player(id, 2.0f + (float)id, 8.0f);
    }
    for (int local = 0; local < MAX_PLAYERS; local++) {
        w.local_player = local;
        frame(FRAME_DT);
        TEST_ASSERT_EQUAL_INT(MAX_PLAYERS - 1, sl.count);
        for (int id = 0; id < MAX_PLAYERS; id++) {
            if (id != local) {
                TEST_ASSERT_EQUAL_INT(id, player_sprite(id)->skin);
            }
        }
    }
}

/* ---- Choosing the animation ---- */

static void test_standing_character_idles(void)
{
    add_enemy(ENEMY_IMP, 5.0f, 5.0f);
    frames(30, FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_IDLE, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(0)->frame);
}

/* An idle sheet of more than one frame loops: a longer strip is a longer
 * animation, with nothing to change in the code. */
static void test_longer_idle_sheet_loops(void)
{
    for (int s = 0; s < MAX_SKINS; s++) {
        a.skin_anim[s][ANIM_IDLE].cols = 2;
    }
    add_enemy(ENEMY_IMP, 5.0f, 5.0f);
    int seen[2] = {0};
    for (int i = 0; i < 120; i++) {
        frame(FRAME_DT);
        TEST_ASSERT_EQUAL_INT(ANIM_IDLE, enemy_sprite(0)->anim);
        seen[enemy_sprite(0)->frame]++;
    }
    TEST_ASSERT_TRUE(seen[0] > 0 && seen[1] > 0);
}

/* A runner goes through every frame of the run, round and round. */
static void test_running_character_cycles_the_run(void)
{
    Enemy *e = add_enemy(ENEMY_IMP, 5.0f, 5.0f);
    e->state = ESTATE_CHASE;
    frame(FRAME_DT);

    int seen[4] = {0};
    for (int i = 0; i < 60; i++) {
        e->x += 0.03f;
        frame(FRAME_DT);
        TEST_ASSERT_EQUAL_INT(ANIM_RUN, enemy_sprite(0)->anim);
        seen[enemy_sprite(0)->frame]++;
    }
    for (int f = 0; f < 4; f++) {
        TEST_ASSERT_TRUE(seen[f] > 0);
    }
}

/* At 144 FPS a tick lands on every second or third frame and the frames in
 * between move nobody; a snapshot from a server moves another player only
 * twenty times a second. Neither blinks the run into idle. */
static void test_run_does_not_blink_between_moves(void)
{
    const float dt = 1.0f / 144.0f;
    Enemy *e = add_enemy(ENEMY_IMP, 5.0f, 5.0f);
    frame(dt);

    double acc = 0.0;
    int moving = 0;
    for (int i = 0; i < 144; i++) {
        acc += dt;
        while (acc >= FIXED_DT) {
            e->x += 0.03f;
            acc -= FIXED_DT;
            moving = 1;
        }
        frame(dt);
        if (moving) {
            TEST_ASSERT_EQUAL_INT(ANIM_RUN, enemy_sprite(0)->anim);
        }
    }

    PlayerState *p = add_player(1, 2.0f, 8.0f);
    frame(dt);
    acc = 0.0;
    moving = 0;
    for (int i = 0; i < 144; i++) {
        acc += dt;
        if (acc >= 1.0 / SNAPSHOT_RATE) {
            p->x += 0.15f;
            acc -= 1.0 / SNAPSHOT_RATE;
            moving = 1;
        }
        frame(dt);
        if (moving) {
            TEST_ASSERT_EQUAL_INT(ANIM_RUN, player_sprite(1)->anim);
        }
    }

    /* and having stopped, both stand again soon after */
    frames(72, dt);
    TEST_ASSERT_EQUAL_INT(ANIM_IDLE, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(ANIM_IDLE, player_sprite(1)->anim);
}

/* An attacking enemy aims, on the first frame of the sheet, until it attacks.
 * Each attack resets attack_cooldown, and that is the shot: the flash runs
 * through the rest of the sheet, in order, for WEAPON_SHOT_TIME to within a
 * frame, and the enemy is back to aiming for the next one. */
static void test_attacking_enemy_flashes_on_each_attack(void)
{
    Enemy *e = add_enemy(ENEMY_IMP, 5.0f, 5.0f);
    e->state = ESTATE_ATTACK;
    frames(30, FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_SHOOT, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(0)->frame);

    for (int attack = 0; attack < 2; attack++) {
        e->attack_cooldown = enemy_def(ENEMY_IMP)->attack_cd;
        int seen[8] = {0};
        int last = 0;
        for (int i = 0; i < 60; i++) {
            frame(FRAME_DT);
            e->attack_cooldown -= FRAME_DT; /* as the next tick counts it */
            int f = enemy_sprite(0)->frame;
            TEST_ASSERT_EQUAL_INT(ANIM_SHOOT, enemy_sprite(0)->anim);
            if (f != 0) {
                TEST_ASSERT_TRUE(f == last || f == last + 1);
            }
            seen[f]++;
            last = f;
        }
        TEST_ASSERT_EQUAL_INT(0, last);
        int flash = 0;
        for (int f = 1; f < SHEET_COLS[ANIM_SHOOT]; f++) {
            TEST_ASSERT_TRUE(seen[f] > 0);
            flash += seen[f];
        }
        TEST_ASSERT_INT_WITHIN(1, (int)roundf(WEAPON_SHOT_TIME / FRAME_DT), flash);
    }
}

/* The flash runs on the client's clock, not the world's: the shot that kills
 * the player stops the world, and the enemy that fired it still finishes. */
static void test_shot_plays_out_when_the_world_stops(void)
{
    Enemy *e = add_enemy(ENEMY_SERG, 5.0f, 5.0f);
    e->state = ESTATE_ATTACK;
    frame(FRAME_DT);
    e->attack_cooldown = enemy_def(ENEMY_SERG)->attack_cd;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(1, enemy_sprite(0)->frame);

    frames((int)roundf(WEAPON_SHOT_TIME / FRAME_DT) + 1, FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_SHOOT, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(0)->frame);
}

/* A player's shot starts when Weapon.anim goes back up, flashes for
 * WEAPON_SHOT_TIME and leaves no shot behind: a player does not stand aiming
 * between shots the way an attacking enemy does. */
static void test_player_shot_starts_with_the_weapon(void)
{
    PlayerState *p = add_player(1, 2.0f, 8.0f);
    Weapon *wp = &p->weapons.weapons[WEAPON_PISTOL];
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_IDLE, player_sprite(1)->anim);

    wp->anim = 1.0f;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_SHOOT, player_sprite(1)->anim);
    TEST_ASSERT_EQUAL_INT(1, player_sprite(1)->frame);

    int shown = 1;
    while (player_sprite(1)->anim == ANIM_SHOOT && shown < 60) {
        wp->anim = fmaxf(wp->anim - FRAME_DT / WEAPON_SHOT_TIME, 0.0f);
        frame(FRAME_DT);
        shown++;
    }
    TEST_ASSERT_EQUAL_INT(ANIM_IDLE, player_sprite(1)->anim);
    TEST_ASSERT_INT_WITHIN(1, (int)roundf(WEAPON_SHOT_TIME / FRAME_DT), shown - 1);
}

/* A drop in hp shows as pain, over anything else the character is doing, and
 * for a while only. */
static void test_hp_drop_shows_pain_for_a_while(void)
{
    Enemy *e = add_enemy(ENEMY_SERG, 5.0f, 5.0f);
    e->state = ESTATE_ATTACK;
    frame(FRAME_DT);
    e->hp -= 10.0f;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_PAIN, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(0)->frame);

    /* and back to aiming: getting hurt is not a shot */
    frames(60, FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_SHOOT, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(0)->frame);
}

/* The death plays once and the body stays in its last frame; hitting the
 * corpse again does not stand it up to die a second time. */
static void test_death_plays_once_and_stays_down(void)
{
    Enemy *e = add_enemy(ENEMY_IMP, 5.0f, 5.0f);
    frame(FRAME_DT);
    e->hp = 0.0f;
    e->state = ESTATE_DEAD;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_DEATH, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(0)->frame);

    int last = 0;
    for (int i = 0; i < 180; i++) {
        frame(FRAME_DT);
        TEST_ASSERT_TRUE(enemy_sprite(0)->frame >= last);
        last = enemy_sprite(0)->frame;
    }
    TEST_ASSERT_EQUAL_INT(SHEET_COLS[ANIM_DEATH] - 1, last);

    e->hp = -5.0f;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_DEATH, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(SHEET_COLS[ANIM_DEATH] - 1, enemy_sprite(0)->frame);
}

/* A fall is there to be watched: each frame of it shows for a fifth of a
 * second, so the body comes to rest on the last one 0.8 s after the shot —
 * not in a blink. */
static void test_death_takes_its_time(void)
{
    Enemy *e = add_enemy(ENEMY_IMP, 5.0f, 5.0f);
    frame(FRAME_DT);
    e->hp = 0.0f;
    e->state = ESTATE_DEAD;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(0)->frame);

    int falling = 0;
    while (enemy_sprite(0)->frame < SHEET_COLS[ANIM_DEATH] - 1 && falling < 180) {
        frame(FRAME_DT);
        falling++;
    }
    TEST_ASSERT_INT_WITHIN(1, (int)roundf(0.8f / FRAME_DT), falling);
}

/* A shot does not outlive the one who fired it: someone who dies mid-flash
 * and is back on their feet straight after stands, not shooting. */
static void test_death_ends_the_flash(void)
{
    PlayerState *p = add_player(1, 2.0f, 8.0f);
    frame(FRAME_DT);
    p->weapons.weapons[WEAPON_PISTOL].anim = 1.0f;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_SHOOT, player_sprite(1)->anim);

    p->alive = 0;
    p->hp = 0.0f;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_DEATH, player_sprite(1)->anim);

    p->alive = 1;
    p->hp = 100.0f;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_IDLE, player_sprite(1)->anim);
}

/* A dead player stays on the screen, lying where they fell, as the character
 * they were. */
static void test_dead_player_is_drawn_dying(void)
{
    PlayerState *p = add_player(3, 2.0f, 8.0f);
    frame(FRAME_DT);
    p->alive = 0;
    p->hp = 0.0f;
    frames(60, FRAME_DT);
    TEST_ASSERT_EQUAL_INT(1, sl.count);
    TEST_ASSERT_EQUAL_INT(ANIM_DEATH, player_sprite(3)->anim);
    TEST_ASSERT_EQUAL_INT(SHEET_COLS[ANIM_DEATH] - 1, player_sprite(3)->frame);
    TEST_ASSERT_EQUAL_INT(3, player_sprite(3)->skin);
}

/* A player protected after a respawn blinks: shown for the first half of each
 * PROTECT_BLINK_TIME (0.2 s), hidden for the second, kept in the list either
 * way. Once the protection is gone they stay shown; nobody else ever blinks. */
static void test_a_protected_player_blinks(void)
{
    PlayerState *p = add_player(3, 2.0f, 8.0f);
    add_player(4, 6.0f, 8.0f);
    p->spawn_protect_timer = 1.0f;

    int shown = 0, hidden = 0, changes = 0, was = 1;
    for (int i = 0; i < 60; i++) {
        frame(FRAME_DT);
        TEST_ASSERT_TRUE(player_sprite(4)->active);
        int now = player_sprite(3)->active;
        shown += now;
        hidden += !now;
        changes += now != was;
        was = now;
    }
    /* a second: five blinks, each half shown and half hidden */
    TEST_ASSERT_INT_WITHIN(2, 30, shown);
    TEST_ASSERT_INT_WITHIN(2, 30, hidden);
    TEST_ASSERT_INT_WITHIN(1, 10, changes);

    p->spawn_protect_timer = 0.0f;
    for (int i = 0; i < 30; i++) {
        frame(FRAME_DT);
        TEST_ASSERT_TRUE(player_sprite(3)->active);
    }
}

/* Someone who joins a slot another player left starts afresh: the distance
 * between where the last one stood and where they spawn is not a run. */
static void test_a_freed_slot_forgets_its_last_player(void)
{
    PlayerState *p = add_player(1, 2.0f, 8.0f);
    frame(FRAME_DT);
    p->active = 0;
    frame(FRAME_DT);
    add_player(1, 12.0f, 3.0f);
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_IDLE, player_sprite(1)->anim);
}

/* With no time passing — the game paused — nothing moves on. */
static void test_zero_dt_holds_every_animation(void)
{
    Enemy *e = add_enemy(ENEMY_IMP, 5.0f, 5.0f);
    e->state = ESTATE_DEAD;
    frame(FRAME_DT);
    frames(120, 0.0f);
    TEST_ASSERT_EQUAL_INT(ANIM_DEATH, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(0)->frame);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_imp_and_sergeant_draw_with_their_own_skins);
    RUN_TEST(test_enemy_with_no_listed_skin_gets_none);
    RUN_TEST(test_nine_players_draw_apart_and_animate_apart);
    RUN_TEST(test_a_player_wears_one_skin_on_every_client);
    RUN_TEST(test_standing_character_idles);
    RUN_TEST(test_longer_idle_sheet_loops);
    RUN_TEST(test_running_character_cycles_the_run);
    RUN_TEST(test_run_does_not_blink_between_moves);
    RUN_TEST(test_attacking_enemy_flashes_on_each_attack);
    RUN_TEST(test_shot_plays_out_when_the_world_stops);
    RUN_TEST(test_player_shot_starts_with_the_weapon);
    RUN_TEST(test_hp_drop_shows_pain_for_a_while);
    RUN_TEST(test_death_plays_once_and_stays_down);
    RUN_TEST(test_death_takes_its_time);
    RUN_TEST(test_death_ends_the_flash);
    RUN_TEST(test_dead_player_is_drawn_dying);
    RUN_TEST(test_a_protected_player_blinks);
    RUN_TEST(test_a_freed_slot_forgets_its_last_player);
    RUN_TEST(test_zero_dt_holds_every_animation);
    return UNITY_END();
}
