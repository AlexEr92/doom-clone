/* text.c: where the 3x5 font puts each glyph of a line of text or a number.
 *
 * The test knows neither what a glyph looks like nor any colour of the HUD,
 * and compares no picture. It draws a line or a number in one call into
 * buffer A, and the same characters one at a time with text_draw_char() into
 * buffer B where the layout puts them — the first at x0, each next
 * TEXT_STEP * scale to the right — and asks the two to be equal pixel for
 * pixel. A glyph drawn differently changes both alike; digits walked in the
 * wrong order (bug-006: 100 shown as 001), a line off its anchor or at the
 * wrong step do not. text_draw_char() and the constants of text.h are the
 * reference the test builds B with, so a fault in them is not its to catch.
 *
 * Each screen sits in the middle of a larger block whose margins hold a
 * sentinel, so that a write past either end of the screen shows. */

#include "unity.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

#define PIXELS (SCREEN_W * SCREEN_H)
/* Rows of margin above and below the screen, more than the tallest glyph the
 * tests draw off it: a write the clip misses lands in the margin, not past
 * the block. */
#define MARGIN (32 * SCREEN_W)
#define BLOCK (MARGIN + PIXELS + MARGIN)
#define SENTINEL 0xDEADBEEFu
#define PAPER 0x00000000u
#define INK 0xFF336699u

static uint32_t block_a[BLOCK];
static uint32_t block_b[BLOCK];
static Framebuffer a = {block_a + MARGIN};
static Framebuffer b = {block_b + MARGIN};

static void clear(uint32_t *block)
{
    for (int i = 0; i < BLOCK; i++) {
        block[i] = i < MARGIN || i >= MARGIN + PIXELS ? SENTINEL : PAPER;
    }
}

void setUp(void)
{
    clear(block_a);
    clear(block_b);
}

void tearDown(void)
{}

/* s one glyph at a time, glyph i where the layout puts it. */
static void draw_by_glyph(Framebuffer *f, int x0, int y0, const char *s, int scale)
{
    for (int i = 0; s[i]; i++) {
        text_draw_char(f, x0 + i * TEXT_STEP * scale, y0, s[i], INK, scale);
    }
}

static int inked(const Framebuffer *f)
{
    int n = 0;
    for (int i = 0; i < PIXELS; i++) {
        n += f->pixels[i] == INK;
    }
    return n;
}

static void fail_at(const char *what, int index)
{
    char msg[96];
    int x = ((index % SCREEN_W) + SCREEN_W) % SCREEN_W;
    snprintf(msg, sizeof(msg), "%s at x=%d y=%d", what, x, (index - x) / SCREEN_W);
    TEST_FAIL_MESSAGE(msg);
}

/* A and B pixel for pixel, margins included. B must hold something, or the
 * two would agree on nothing drawn at all. */
static void assert_same(void)
{
    TEST_ASSERT_TRUE_MESSAGE(inked(&b) > 0, "the glyphs drawn one by one left no ink");
    for (int i = 0; i < BLOCK; i++) {
        if (block_a[i] != block_b[i]) {
            fail_at("the call and its glyphs one by one differ", i - MARGIN);
        }
    }
}

static void assert_margins_untouched(const uint32_t *block)
{
    for (int i = 0; i < MARGIN; i++) {
        if (block[i] != SENTINEL) {
            fail_at("written above the screen", i - MARGIN);
        }
        if (block[MARGIN + PIXELS + i] != SENTINEL) {
            fail_at("written below the screen", PIXELS + i);
        }
    }
}

/* `moved` is `orig` shifted by (dx, dy): what lands on the screen is there,
 * and nothing else is. */
static void assert_shifted(const Framebuffer *moved, const Framebuffer *orig, int dx, int dy)
{
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            int sx = x - dx, sy = y - dy;
            uint32_t want = PAPER;
            if (sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < SCREEN_H) {
                want = orig->pixels[sy * SCREEN_W + sx];
            }
            if (moved->pixels[y * SCREEN_W + x] != want) {
                fail_at("the shifted drawing differs", y * SCREEN_W + x);
            }
        }
    }
}

/* Every inked pixel lies in the box w x h at (x0, y0). */
static void assert_ink_within(const Framebuffer *f, int x0, int y0, int w, int h)
{
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            int inside = x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
            if (f->pixels[y * SCREEN_W + x] == INK && !inside) {
                fail_at("ink outside the line's box", y * SCREEN_W + x);
            }
        }
    }
}

static void check_int(int value, const char *digits, int x0, int y0, int scale)
{
    setUp();
    text_draw_int(&a, x0, y0, value, INK, scale);
    draw_by_glyph(&b, x0, y0, digits, scale);
    assert_same();
}

static void check_line(const char *s, int x0, int y0, int scale)
{
    setUp();
    text_draw(&a, x0, y0, s, INK, scale);
    draw_by_glyph(&b, x0, y0, s, scale);
    assert_same();
}

/* ---- Numbers ---- */

static void test_a_number_puts_its_digits_left_to_right_from_the_most_significant(void)
{
    check_int(47, "47", 0, 0, 1);
    check_int(1234567890, "1234567890", 0, 0, 1);
}

static void test_a_number_keeps_every_repeated_digit(void)
{
    check_int(100, "100", 0, 0, 1);
    check_int(50, "50", 0, 0, 1);
}

static void test_one_digit_and_zero_are_one_glyph_at_the_anchor(void)
{
    check_int(7, "7", 0, 0, 1);
    check_int(0, "0", 0, 0, 1);
}

static void test_a_negative_number_is_drawn_without_its_sign(void)
{
    check_int(-47, "47", 0, 0, 1);
    check_int(-2147483647 - 1, "2147483648", 0, 0, 1);
}

static void test_a_larger_scale_multiplies_the_step(void)
{
    for (int scale = 2; scale <= 3; scale++) {
        check_int(47, "47", 0, 0, scale);
        check_int(100, "100", 0, 0, scale);
        check_int(0, "0", 0, 0, scale);
    }
}

static void test_a_number_away_from_the_corner_starts_at_its_anchor(void)
{
    check_int(100, "100", 123, 45, 1);
    check_int(47, "47", 301, 207, 3);
}

/* ---- Lines of text ---- */

static void test_a_line_puts_one_glyph_a_step_and_a_space_takes_one(void)
{
    for (int scale = 1; scale <= 3; scale++) {
        check_line("PRESS 1 2 WEAPON", 0, 0, scale);
        check_line("  hp:100  -47!?", 17, 33, scale);
    }
}

static void test_a_line_drawn_elsewhere_is_the_same_line_moved(void)
{
    const char *s = "AMMO 50";
    for (int scale = 1; scale <= 3; scale++) {
        setUp();
        text_draw(&a, 0, 0, s, INK, scale);
        text_draw(&b, 211, 97, s, INK, scale);
        TEST_ASSERT_TRUE(inked(&a) > 0);
        assert_shifted(&b, &a, 211, 97);

        setUp();
        text_draw_int(&a, 0, 0, 1906, INK, scale);
        text_draw_int(&b, 5, 300, 1906, INK, scale);
        TEST_ASSERT_TRUE(inked(&a) > 0);
        assert_shifted(&b, &a, 5, 300);
    }
}

/* ---- Width and centring ---- */

static void test_width_reaches_the_right_edge_of_the_last_glyph(void)
{
    const char *lines[] = {"7", "47", "100", "A B", "YOU DIED"};
    for (int scale = 1; scale <= 3; scale++) {
        for (size_t k = 0; k < sizeof(lines) / sizeof(lines[0]); k++) {
            int n = (int)strlen(lines[k]);
            int w = text_width(lines[k], scale);
            TEST_ASSERT_EQUAL_INT((n - 1) * TEXT_STEP * scale + TEXT_GLYPH_W * scale, w);

            setUp();
            text_draw(&a, 40, 60, lines[k], INK, scale);
            assert_ink_within(&a, 40, 60, w, TEXT_GLYPH_H * scale);
        }
    }
    TEST_ASSERT_EQUAL_INT(0, text_width("", 2));
}

static void test_a_centred_line_starts_half_its_width_left_of_the_middle(void)
{
    const char *s = "PRESS ENTER TO START";
    for (int scale = 1; scale <= 3; scale++) {
        setUp();
        text_draw_centered(&a, 200, s, INK, scale);
        draw_by_glyph(&b, SCREEN_W / 2 - text_width(s, scale) / 2, 200, s, scale);
        assert_same();
    }
}

/* ---- The edges of the screen ---- */

/* A line partly or wholly off the screen is the part of it on the screen:
 * nothing written past the buffer, nothing wrapped onto the next row. */
static void test_a_line_off_the_edge_is_clipped(void)
{
    const char *s = "0123456789 AZ !-:";
    const int scale = 3;
    const int x0 = 100, y0 = 100;
    const int to[][2] = {
            {SCREEN_W - 7, y0}, {-7, y0},
            {x0, -8},           {x0, SCREEN_H - 6},
            {SCREEN_W + 5, y0}, {-SCREEN_W, y0},
            {x0, -20},          {x0, SCREEN_H + 2},
            {-5, -7},           {SCREEN_W - 10, SCREEN_H - 6},
    };
    for (size_t k = 0; k < sizeof(to) / sizeof(to[0]); k++) {
        setUp();
        text_draw(&b, x0, y0, s, INK, scale);
        TEST_ASSERT_TRUE(inked(&b) > 0);
        text_draw(&a, to[k][0], to[k][1], s, INK, scale);
        assert_margins_untouched(block_a);
        assert_shifted(&a, &b, to[k][0] - x0, to[k][1] - y0);
    }
}

static void test_a_number_off_the_edge_is_clipped(void)
{
    setUp();
    text_draw_int(&b, 100, 100, 100, INK, 3);
    text_draw_int(&a, SCREEN_W - 20, SCREEN_H - 8, 100, INK, 3);
    assert_margins_untouched(block_a);
    assert_shifted(&a, &b, SCREEN_W - 120, SCREEN_H - 108);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_number_puts_its_digits_left_to_right_from_the_most_significant);
    RUN_TEST(test_a_number_keeps_every_repeated_digit);
    RUN_TEST(test_one_digit_and_zero_are_one_glyph_at_the_anchor);
    RUN_TEST(test_a_negative_number_is_drawn_without_its_sign);
    RUN_TEST(test_a_larger_scale_multiplies_the_step);
    RUN_TEST(test_a_number_away_from_the_corner_starts_at_its_anchor);
    RUN_TEST(test_a_line_puts_one_glyph_a_step_and_a_space_takes_one);
    RUN_TEST(test_a_line_drawn_elsewhere_is_the_same_line_moved);
    RUN_TEST(test_width_reaches_the_right_edge_of_the_last_glyph);
    RUN_TEST(test_a_centred_line_starts_half_its_width_left_of_the_middle);
    RUN_TEST(test_a_line_off_the_edge_is_clipped);
    RUN_TEST(test_a_number_off_the_edge_is_clipped);
    return UNITY_END();
}
