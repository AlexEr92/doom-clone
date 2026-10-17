/* hud.c: which frame of the weapon sheet Weapon.anim shows.
 *
 * Drawing is not tested — a framebuffer is what would show it wrong — but the
 * frame decides whether a shot plays all of its frames, in order and for how
 * long, and that needs none. weapon.c is linked so that the animation runs
 * down the way a tick runs it, not the way the test assumes it does. */

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
 * skipped, for fire_cd in all to within a tick, and then the weapon is back
 * at rest rather than stuck on the last frame. */
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
    TEST_ASSERT_INT_WITHIN(1, (int)roundf(w->fire_cd / (float)TEST_STEP), shot);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_weapon_at_rest_shows_frame_zero);
    RUN_TEST(test_shot_spans_the_frames_after_rest);
    RUN_TEST(test_single_frame_sheet_stays_on_it);
    RUN_TEST(test_pistol_shot_plays_every_frame_once_at_60_fps);
    return UNITY_END();
}
