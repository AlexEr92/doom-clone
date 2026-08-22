/* utils.h: colour packing and range clamping. Header-only inline functions,
 * so nothing from src/ is linked here. */

#include "unity.h"
#include "utils.h"

void setUp(void)
{}

void tearDown(void)
{}

/* make_color packs 0xAABBGGRR — red in the LOW byte. Getting this backwards
 * swaps red and blue across the whole game and is invisible in greys. */
static void test_make_color_puts_red_in_the_low_byte(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xFF0000FFu, make_color(255, 0, 0));
    TEST_ASSERT_EQUAL_HEX32(0xFF00FF00u, make_color(0, 255, 0));
    TEST_ASSERT_EQUAL_HEX32(0xFFFF0000u, make_color(0, 0, 255));
}

static void test_make_color_is_always_opaque(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u, make_color(0, 0, 0) & 0xFF000000u);
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u, make_color(255, 255, 255) & 0xFF000000u);
}

static void test_make_color_keeps_channels_apart(void)
{
    uint32_t c = make_color(0x12, 0x34, 0x56);
    TEST_ASSERT_EQUAL_HEX32(0xFF563412u, c);
}

/* shade_color must decompose exactly the way make_color composed. */
static void test_shade_color_scales_every_channel(void)
{
    uint32_t c = make_color(100, 150, 200);
    TEST_ASSERT_EQUAL_HEX32(make_color(50, 75, 100), shade_color(c, 0.5f));
}

static void test_shade_color_identity_and_black(void)
{
    uint32_t c = make_color(10, 20, 30);
    TEST_ASSERT_EQUAL_HEX32(c, shade_color(c, 1.0f));
    TEST_ASSERT_EQUAL_HEX32(make_color(0, 0, 0), shade_color(c, 0.0f));
}

/* A channel must not bleed into its neighbours while being scaled. The result
 * is truncated, not rounded: 255 * 0.5f is 127. */
static void test_shade_color_does_not_mix_channels(void)
{
    TEST_ASSERT_EQUAL_HEX32(make_color(127, 0, 0), shade_color(make_color(255, 0, 0), 0.5f));
    TEST_ASSERT_EQUAL_HEX32(make_color(0, 127, 0), shade_color(make_color(0, 255, 0), 0.5f));
    TEST_ASSERT_EQUAL_HEX32(make_color(0, 0, 127), shade_color(make_color(0, 0, 255), 0.5f));
}

static void test_clampf_bounds(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, clampf(-5.0f, 0.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, clampf(5.0f, 0.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.25f, clampf(0.25f, 0.0f, 1.0f));
}

static void test_clampf_returns_the_bounds_themselves(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, clampf(0.0f, 0.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, clampf(1.0f, 0.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, clampf(-7.0f, -3.0f, -1.0f));
}

static void test_clampi_bounds(void)
{
    TEST_ASSERT_EQUAL_INT(0, clampi(-5, 0, 10));
    TEST_ASSERT_EQUAL_INT(10, clampi(50, 0, 10));
    TEST_ASSERT_EQUAL_INT(7, clampi(7, 0, 10));
    TEST_ASSERT_EQUAL_INT(0, clampi(0, 0, 10));
    TEST_ASSERT_EQUAL_INT(10, clampi(10, 0, 10));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_make_color_puts_red_in_the_low_byte);
    RUN_TEST(test_make_color_is_always_opaque);
    RUN_TEST(test_make_color_keeps_channels_apart);
    RUN_TEST(test_shade_color_scales_every_channel);
    RUN_TEST(test_shade_color_identity_and_black);
    RUN_TEST(test_shade_color_does_not_mix_channels);
    RUN_TEST(test_clampf_bounds);
    RUN_TEST(test_clampf_returns_the_bounds_themselves);
    RUN_TEST(test_clampi_bounds);
    return UNITY_END();
}
