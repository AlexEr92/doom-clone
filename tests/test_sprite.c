/* sprite.c: which character draws each entity, which animation it plays and
 * which frame of it shows — everything the client works out about a sprite
 * before a pixel is drawn.
 *
 * sprite_render() is not tested: a framebuffer is what would show it wrong.
 * zBuffer, which it reads, is defined here so that raycast.c and the whole
 * renderer behind it stay out of the link. The world and the sheets are built
 * by hand: the clocks read only positions, hp and states out of the one, and
 * only the length of each sheet out of the other. enemy.c is linked for
 * enemy_def(), assets.c for the skin names. */

#include "unity.h"
#include "sprite.h"
#include "world.h"
#include <math.h>
#include <string.h>

float zBuffer[SCREEN_W];

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
        w.players[6].weapons.weapons[WEAPON_PISTOL].anim = 0.5f;
        frame(FRAME_DT);
    }
    for (int id = 1; id < MAX_PLAYERS; id++) {
        int expected = id == 3 ? ANIM_RUN : (id == 6 ? ANIM_SHOOT : ANIM_IDLE);
        TEST_ASSERT_EQUAL_INT(expected, player_sprite(id)->anim);
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

/* An attacking enemy shows the shot, looping in step with its attack_cd. */
static void test_attacking_enemy_shoots(void)
{
    Enemy *e = add_enemy(ENEMY_IMP, 5.0f, 5.0f);
    e->state = ESTATE_ATTACK;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_SHOOT, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(0)->frame);

    int ticks = (int)roundf(enemy_def(ENEMY_IMP)->attack_cd / FRAME_DT);
    frames(ticks / 2, FRAME_DT);
    TEST_ASSERT_EQUAL_INT(SHEET_COLS[ANIM_SHOOT] / 2, enemy_sprite(0)->frame);
    /* and a little past attack_cd, with the next attack, it starts over */
    frames(ticks / 2 + 3, FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_SHOOT, enemy_sprite(0)->anim);
    TEST_ASSERT_EQUAL_INT(0, enemy_sprite(0)->frame);
}

/* A player's shot follows Weapon.anim: frame 0 as it is fired, the last as it
 * runs out, and no shot at all once it has. */
static void test_player_shot_follows_the_weapon(void)
{
    PlayerState *p = add_player(1, 2.0f, 8.0f);
    Weapon *wp = &p->weapons.weapons[WEAPON_PISTOL];
    wp->anim = 1.0f;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_SHOOT, player_sprite(1)->anim);
    TEST_ASSERT_EQUAL_INT(0, player_sprite(1)->frame);

    wp->anim = 0.05f;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(SHEET_COLS[ANIM_SHOOT] - 1, player_sprite(1)->frame);

    wp->anim = 0.0f;
    frame(FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_IDLE, player_sprite(1)->anim);
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

    frames(60, FRAME_DT);
    TEST_ASSERT_EQUAL_INT(ANIM_SHOOT, enemy_sprite(0)->anim);
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

/* A dead player stays on the screen, lying where they fell. */
static void test_dead_player_is_drawn_dying(void)
{
    PlayerState *p = add_player(1, 2.0f, 8.0f);
    p->alive = 0;
    p->hp = 0.0f;
    frames(60, FRAME_DT);
    TEST_ASSERT_EQUAL_INT(1, sl.count);
    TEST_ASSERT_EQUAL_INT(ANIM_DEATH, player_sprite(1)->anim);
    TEST_ASSERT_EQUAL_INT(SHEET_COLS[ANIM_DEATH] - 1, player_sprite(1)->frame);
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
    RUN_TEST(test_standing_character_idles);
    RUN_TEST(test_longer_idle_sheet_loops);
    RUN_TEST(test_running_character_cycles_the_run);
    RUN_TEST(test_run_does_not_blink_between_moves);
    RUN_TEST(test_attacking_enemy_shoots);
    RUN_TEST(test_player_shot_follows_the_weapon);
    RUN_TEST(test_hp_drop_shows_pain_for_a_while);
    RUN_TEST(test_death_plays_once_and_stays_down);
    RUN_TEST(test_dead_player_is_drawn_dying);
    RUN_TEST(test_a_freed_slot_forgets_its_last_player);
    RUN_TEST(test_zero_dt_holds_every_animation);
    return UNITY_END();
}
