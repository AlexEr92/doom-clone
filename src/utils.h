#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

#define SCREEN_W 640
#define SCREEN_H 400
#define WINDOW_W 1280
#define WINDOW_H 800

#define MAP_MAX_W 24
#define MAP_MAX_H 24

#define FOV_PLANE 0.66f
#define MOVE_SPEED 3.0f
#define ROT_SPEED 3.0f
#define MOUSE_SENS 0.002f
#define PLAYER_RADIUS 0.2f

#define FIXED_DT (1.0 / 60.0)

typedef struct {
    uint32_t *pixels;
} Framebuffer;

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline uint32_t make_color(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static inline uint32_t shade_color(uint32_t c, float factor) {
    uint8_t r = (uint8_t)(c & 0xFF);
    uint8_t g = (uint8_t)((c >> 8) & 0xFF);
    uint8_t b = (uint8_t)((c >> 16) & 0xFF);
    r = (uint8_t)(r * factor);
    g = (uint8_t)(g * factor);
    b = (uint8_t)(b * factor);
    return make_color(r, g, b);
}

double get_time_seconds(void);

#endif