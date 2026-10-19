#ifndef TEXT_H
#define TEXT_H

#include "utils.h"

/* The 3x5 bitmap font every overlay and the status bar draw with. A glyph is
 * TEXT_GLYPH_W x TEXT_GLYPH_H cells, each cell `scale` pixels square, its
 * top-left corner at (x0, y0). A line of text puts glyph i at
 * x0 + i * TEXT_STEP * scale: the glyph and one empty column. */
#define TEXT_GLYPH_W 3
#define TEXT_GLYPH_H 5
#define TEXT_STEP (TEXT_GLYPH_W + 1)

/* One glyph: 'A'-'Z' (lower case drawn as upper), '0'-'9', '!', '-', ':'.
 * Anything else, space included, draws nothing. Pixels off the screen are
 * skipped. */
void text_draw_char(Framebuffer *fb, int x0, int y0, char ch, uint32_t color, int scale);

/* A line of text, one glyph a step; a character with no glyph still takes
 * its step. */
void text_draw(Framebuffer *fb, int x0, int y0, const char *s, uint32_t color, int scale);

/* `value` in decimal, the most significant digit at x0. Zero is a single
 * '0'; a negative value is drawn without its sign. */
void text_draw_int(Framebuffer *fb, int x0, int y0, int value, uint32_t color, int scale);

/* Pixels from x0 to the right edge of the last glyph of `s`; 0 for "". */
int text_width(const char *s, int scale);

/* `s` centred across the screen at row y. */
void text_draw_centered(Framebuffer *fb, int y, const char *s, uint32_t color, int scale);

#endif
