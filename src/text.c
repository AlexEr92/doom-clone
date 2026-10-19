#include "text.h"
#include <string.h>

/* Each glyph is TEXT_GLYPH_H rows of TEXT_GLYPH_W-bit masks, written the way
 * the glyph reads: bit 2 is the leftmost column. */
static const uint8_t letters[26][TEXT_GLYPH_H] = {
        /* A */ {0b111, 0b101, 0b111, 0b101, 0b101},
        /* B */ {0b110, 0b101, 0b110, 0b101, 0b110},
        /* C */ {0b111, 0b100, 0b100, 0b100, 0b111},
        /* D */ {0b110, 0b101, 0b101, 0b101, 0b110},
        /* E */ {0b111, 0b100, 0b111, 0b100, 0b111},
        /* F */ {0b111, 0b100, 0b111, 0b100, 0b100},
        /* G */ {0b111, 0b100, 0b101, 0b101, 0b111},
        /* H */ {0b101, 0b101, 0b111, 0b101, 0b101},
        /* I */ {0b111, 0b010, 0b010, 0b010, 0b111},
        /* J */ {0b001, 0b001, 0b001, 0b101, 0b111},
        /* K */ {0b101, 0b110, 0b100, 0b110, 0b101},
        /* L */ {0b100, 0b100, 0b100, 0b100, 0b111},
        /* M */ {0b101, 0b111, 0b111, 0b101, 0b101},
        /* N */ {0b101, 0b111, 0b111, 0b111, 0b101},
        /* O */ {0b111, 0b101, 0b101, 0b101, 0b111},
        /* P */ {0b111, 0b101, 0b111, 0b100, 0b100},
        /* Q */ {0b111, 0b101, 0b101, 0b111, 0b011},
        /* R */ {0b111, 0b101, 0b110, 0b101, 0b101},
        /* S */ {0b111, 0b100, 0b111, 0b001, 0b111},
        /* T */ {0b111, 0b010, 0b010, 0b010, 0b010},
        /* U */ {0b101, 0b101, 0b101, 0b101, 0b111},
        /* V */ {0b101, 0b101, 0b101, 0b101, 0b010},
        /* W */ {0b101, 0b101, 0b111, 0b111, 0b101},
        /* X */ {0b101, 0b101, 0b010, 0b101, 0b101},
        /* Y */ {0b101, 0b101, 0b010, 0b010, 0b010},
        /* Z */ {0b111, 0b001, 0b010, 0b100, 0b111},
};

static const uint8_t digits[10][TEXT_GLYPH_H] = {
        /* 0 */ {0b111, 0b101, 0b101, 0b101, 0b111},
        /* 1 */ {0b010, 0b110, 0b010, 0b010, 0b111},
        /* 2 */ {0b111, 0b001, 0b111, 0b100, 0b111},
        /* 3 */ {0b111, 0b001, 0b111, 0b001, 0b111},
        /* 4 */ {0b101, 0b101, 0b111, 0b001, 0b001},
        /* 5 */ {0b111, 0b100, 0b111, 0b001, 0b111},
        /* 6 */ {0b111, 0b100, 0b111, 0b101, 0b111},
        /* 7 */ {0b111, 0b001, 0b010, 0b010, 0b010},
        /* 8 */ {0b111, 0b101, 0b111, 0b101, 0b111},
        /* 9 */ {0b111, 0b101, 0b111, 0b001, 0b111},
};

static const uint8_t bang[TEXT_GLYPH_H] = {0b010, 0b010, 0b010, 0b010, 0b000};
static const uint8_t dash[TEXT_GLYPH_H] = {0b000, 0b000, 0b111, 0b000, 0b000};
static const uint8_t colon[TEXT_GLYPH_H] = {0b000, 0b010, 0b000, 0b010, 0b000};

/* The rows of ch's glyph, or NULL for a character with none. */
static const uint8_t *glyph(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return letters[ch - 'A'];
    }
    if (ch >= 'a' && ch <= 'z') {
        return letters[ch - 'a'];
    }
    if (ch >= '0' && ch <= '9') {
        return digits[ch - '0'];
    }
    switch (ch) {
        case '!': return bang;
        case '-': return dash;
        case ':': return colon;
        default: return NULL;
    }
}

void text_draw_char(Framebuffer *fb, int x0, int y0, char ch, uint32_t color, int scale)
{
    const uint8_t *rows = glyph(ch);
    if (!rows) {
        return;
    }
    for (int r = 0; r < TEXT_GLYPH_H; r++) {
        for (int c = 0; c < TEXT_GLYPH_W; c++) {
            if (!(rows[r] & (1 << (TEXT_GLYPH_W - 1 - c)))) {
                continue;
            }
            for (int dy = 0; dy < scale; dy++) {
                int y = y0 + r * scale + dy;
                if (y < 0 || y >= SCREEN_H) {
                    continue;
                }
                for (int dx = 0; dx < scale; dx++) {
                    int x = x0 + c * scale + dx;
                    if (x < 0 || x >= SCREEN_W) {
                        continue;
                    }
                    fb->pixels[(size_t)y * SCREEN_W + x] = color;
                }
            }
        }
    }
}

void text_draw(Framebuffer *fb, int x0, int y0, const char *s, uint32_t color, int scale)
{
    for (int i = 0; s[i]; i++) {
        text_draw_char(fb, x0 + i * TEXT_STEP * scale, y0, s[i], color, scale);
    }
}

void text_draw_int(Framebuffer *fb, int x0, int y0, int value, uint32_t color, int scale)
{
    /* Division finds the least significant digit first, so the digits fill
     * the buffer from its end and the string starts at the most significant
     * one. Unsigned, so that -INT_MIN does not overflow. */
    char buf[16];
    char *p = buf + sizeof(buf) - 1;
    *p = '\0';
    unsigned int u = value < 0 ? 0u - (unsigned int)value : (unsigned int)value;
    do {
        *--p = (char)('0' + u % 10);
        u /= 10;
    } while (u > 0);
    text_draw(fb, x0, y0, p, color, scale);
}

int text_width(const char *s, int scale)
{
    int n = (int)strlen(s);
    if (n == 0) {
        return 0;
    }
    return (n - 1) * TEXT_STEP * scale + TEXT_GLYPH_W * scale;
}

void text_draw_centered(Framebuffer *fb, int y, const char *s, uint32_t color, int scale)
{
    text_draw(fb, SCREEN_W / 2 - text_width(s, scale) / 2, y, s, color, scale);
}
