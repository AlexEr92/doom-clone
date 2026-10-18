/* hud.c: which frame of the weapon sheet Weapon.anim shows, and which frame
 * of the face the status bar picks.
 *
 * Drawing is not tested — a framebuffer is what would show it wrong — but the
 * frame decides whether a shot plays all of its frames, in order and for how
 * long, and that needs none; nor does the face, whose steps of health, the
 * length of its grimace and what wins over what are not something a glance
 * at the screen measures. weapon.c is linked so that the animation runs down
 * the way a tick runs it, not the way the test assumes it does; player.c so
 * that a hit takes hp off the way the world takes it. */

#include "unity.h"
#include "hud.h"
#include "weapon.h"
#include "player.h"
#include <math.h>

#define TEST_STEP (1.0 / 60.0)

/* The pistol and shotgun sheets: rest, then three frames of the shot. */
#define SHEET_COLS 4

void setUp(void)
{}

void tearDown(void)
{}

static void test_weapon_at_rest_shows_frame_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, hud_weapon_frame(SHEET_COLS, 0.0f));
}

/* The shot opens on the first frame after rest and closes on the last. */
static void test_shot_spans_the_frames_after_rest(void)
{
    TEST_ASSERT_EQUAL_INT(1, hud_weapon_frame(SHEET_COLS, 1.0f));
    TEST_ASSERT_EQUAL_INT(2, hud_weapon_frame(SHEET_COLS, 0.5f));
    TEST_ASSERT_EQUAL_INT(SHEET_COLS - 1, hud_weapon_frame(SHEET_COLS, 0.01f));
}

/* A procedural weapon is one frame, and the recoil is all a shot shows. */
static void test_single_frame_sheet_stays_on_it(void)
{
    TEST_ASSERT_EQUAL_INT(0, hud_weapon_frame(1, 1.0f));
    TEST_ASSERT_EQUAL_INT(0, hud_weapon_frame(1, 0.5f));
}

/* A pistol shot at 60 FPS: every shot frame shows, in order, none of them
 * skipped, for WEAPON_SHOT_TIME in all to within a tick, and then the weapon
 * is back at rest rather than stuck on the last frame. */
static void test_pistol_shot_plays_every_frame_once_at_60_fps(void)
{
    PlayerState p;
    player_init(&p, 2, 2);
    Weapon *w = &p.weapons.weapons[WEAPON_PISTOL];
    w->anim = 1.0f; /* what weapon_try_fire() leaves */

    int ticks[SHEET_COLS] = {0};
    int last = hud_weapon_frame(SHEET_COLS, w->anim);
    ticks[last]++;
    for (int t = 0; t < 120; t++) {
        weapon_update(&p, TEST_STEP);
        int f = hud_weapon_frame(SHEET_COLS, w->anim);
        if (f != 0) {
            TEST_ASSERT_TRUE(f == last || f == last + 1);
        }
        ticks[f]++;
        last = f;
    }

    TEST_ASSERT_EQUAL_INT(0, last);
    int shot = 0;
    for (int f = 1; f < SHEET_COLS; f++) {
        TEST_ASSERT_TRUE(ticks[f] > 0);
        shot += ticks[f];
    }
    TEST_ASSERT_INT_WITHIN(1, (int)roundf(WEAPON_SHOT_TIME / (float)TEST_STEP), shot);
}

/* ---- The face ---- */

/* A frame of the face clock at 60 FPS. */
static int face_step(HudFace *f, const PlayerState *p)
{
    hud_face_update(f, p, (float)TEST_STEP);
    return hud_face_frame(f, p);
}

/* Five steps of 20 hp, the top one down to 80 inclusive. */
static void test_face_shows_health_in_steps_of_20(void)
{
    static const struct {
        float hp;
        int frame;
    } STEPS[] = {
            {100.0f, FACE_HEALTH},    {80.0f, FACE_HEALTH},     {79.5f, FACE_HEALTH + 1},
            {60.0f, FACE_HEALTH + 1}, {59.0f, FACE_HEALTH + 2}, {40.0f, FACE_HEALTH + 2},
            {39.0f, FACE_HEALTH + 3}, {20.0f, FACE_HEALTH + 3}, {19.0f, FACE_HEALTH + 4},
            {0.5f, FACE_HEALTH + 4},
    };
    PlayerState p;
    player_init(&p, 2, 2);
    HudFace f;
    hud_face_init(&f);
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); i++) {
        p.hp = STEPS[i].hp;
        hud_face_init(&f); /* a step down is a hit; each step is checked on its own */
        TEST_ASSERT_EQUAL_INT_MESSAGE(STEPS[i].frame, face_step(&f, &p), "hp step");
    }
    /* More than 100 — nothing gives it today — is still the top step. */
    p.hp = 150.0f;
    hud_face_init(&f);
    TEST_ASSERT_EQUAL_INT(FACE_HEALTH, face_step(&f, &p));
}

/* A hit shows the grimace for HUD_PAIN_TIME to within a frame, then the step
 * of the health left. */
static void test_hit_grimaces_for_pain_time(void)
{
    PlayerState p;
    player_init(&p, 2, 2);
    HudFace f;
    hud_face_init(&f);
    TEST_ASSERT_EQUAL_INT(FACE_HEALTH, face_step(&f, &p));

    player_damage(&p, 50.0f, -1);
    int pain = 0;
    int frame = FACE_PAIN;
    for (int t = 0; t < 120 && (frame = face_step(&f, &p)) == FACE_PAIN; t++) {
        pain++;
    }
    TEST_ASSERT_INT_WITHIN(1, (int)roundf(HUD_PAIN_TIME / (float)TEST_STEP), pain);
    TEST_ASSERT_EQUAL_INT(FACE_HEALTH + 2, frame);
}

/* A hit landing while the grimace still shows starts it over. */
static void test_second_hit_restarts_the_grimace(void)
{
    PlayerState p;
    player_init(&p, 2, 2);
    HudFace f;
    hud_face_init(&f);
    face_step(&f, &p);
    player_damage(&p, 10.0f, -1);
    face_step(&f, &p);
    int half = (int)(HUD_PAIN_TIME / 2.0f / (float)TEST_STEP);
    for (int t = 0; t < half; t++) {
        face_step(&f, &p);
    }
    player_damage(&p, 10.0f, -1);
    face_step(&f, &p);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, HUD_PAIN_TIME, f.pain_left);
}

/* Nothing to compare on the first frame, and hp going up is no hit. */
static void test_first_frame_and_healing_do_not_grimace(void)
{
    PlayerState p;
    player_init(&p, 2, 2);
    p.hp = 30.0f;
    HudFace f;
    hud_face_init(&f);
    TEST_ASSERT_EQUAL_INT(FACE_HEALTH + 3, face_step(&f, &p));
    p.hp = 55.0f;
    TEST_ASSERT_EQUAL_INT(FACE_HEALTH + 2, face_step(&f, &p));
}

/* A reloaded world starts the face over: the hp of the last one is not
 * compared with the new. */
static void test_init_forgets_the_last_player(void)
{
    PlayerState p;
    player_init(&p, 2, 2);
    HudFace f;
    hud_face_init(&f);
    face_step(&f, &p);
    hud_face_init(&f);
    p.hp = 40.0f;
    TEST_ASSERT_EQUAL_INT(FACE_HEALTH + 2, face_step(&f, &p));
}

/* A pause stops the face with everything else. */
static void test_dt_zero_holds_the_grimace(void)
{
    PlayerState p;
    player_init(&p, 2, 2);
    HudFace f;
    hud_face_init(&f);
    face_step(&f, &p);
    player_damage(&p, 10.0f, -1);
    face_step(&f, &p);
    for (int t = 0; t < 120; t++) {
        hud_face_update(&f, &p, 0.0f);
    }
    TEST_ASSERT_EQUAL_INT(FACE_PAIN, hud_face_frame(&f, &p));
}

/* Zero hp is the dead face, even on the frame of the hit that took the last
 * of it and before the world has marked the player dead; and so is a dead
 * player, whatever hp says. */
static void test_dead_face_wins_over_the_grimace(void)
{
    PlayerState p;
    player_init(&p, 2, 2);
    HudFace f;
    hud_face_init(&f);
    face_step(&f, &p);
    player_damage(&p, 500.0f, -1);
    TEST_ASSERT_EQUAL_INT(FACE_DEAD, face_step(&f, &p));
    p.alive = 0;
    TEST_ASSERT_EQUAL_INT(FACE_DEAD, face_step(&f, &p));
    p.hp = 40.0f;
    TEST_ASSERT_EQUAL_INT(FACE_DEAD, face_step(&f, &p));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_weapon_at_rest_shows_frame_zero);
    RUN_TEST(test_shot_spans_the_frames_after_rest);
    RUN_TEST(test_single_frame_sheet_stays_on_it);
    RUN_TEST(test_pistol_shot_plays_every_frame_once_at_60_fps);
    RUN_TEST(test_face_shows_health_in_steps_of_20);
    RUN_TEST(test_hit_grimaces_for_pain_time);
    RUN_TEST(test_second_hit_restarts_the_grimace);
    RUN_TEST(test_first_frame_and_healing_do_not_grimace);
    RUN_TEST(test_init_forgets_the_last_player);
    RUN_TEST(test_dt_zero_holds_the_grimace);
    RUN_TEST(test_dead_face_wins_over_the_grimace);
    return UNITY_END();
}
