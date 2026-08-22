#ifndef SHEET_COMMON_H
#define SHEET_COMMON_H

/* Shared bits of the two sheet tools: the frame-size suffix that every asset
 * name carries, and the magenta key both of them agree on. Kept in step with
 * assets.c by hand — the tools are built separately from the game and must
 * not drag SDL in. */

#include <stdio.h>
#include <string.h>

#define KEY_R 255
#define KEY_G 0
#define KEY_B 255

/* Largest max-channel distance from the sheet background that still counts as
 * background, and the distance past which a pixel is fully opaque. Between the
 * two the alpha ramps: a generative model returns a compressed image whose
 * background is a cloud of near-magenta shades with a soft fringe along every
 * silhouette, and a single threshold either keeps the fringe or eats the edge. */
#define TOL_BG 48
#define TOL_FG 96

/* Alpha at which a source pixel is trusted to carry the real colour of the
 * silhouette. Everything below it sits on the ramp, which means it is part
 * background — letting those into the colour average paints a purple rim
 * around anything dark. They still count towards coverage: they decide
 * whether the output pixel is drawn, not what colour it is. */
#define A_TRUSTED 255

/* Below this brightness a pixel is black, not a colour on any axis. */
#define TINT_MIN_LEVEL 32

/* A pixel sitting on the magenta axis — red and blue both far above green,
 * and close to each other — is the key colour mixed with something darker,
 * not a colour anyone drew. Distance alone does not catch these: the dark
 * end of the fringe lands past any threshold that still keeps a real edge,
 * yet it is background all the same. The spec forbids the key colour inside
 * a silhouette, so nothing legitimate is lost by refusing them. */
static inline int is_key_tinted(const unsigned char *c)
{
    int lo = c[0] < c[2] ? c[0] : c[2];
    int hi = c[0] > c[2] ? c[0] : c[2];
    int spread = c[0] > c[2] ? c[0] - c[2] : c[2] - c[0];
    /* Near-black has no hue to lean anywhere: an outline drawn in it trips
     * every ratio here while being nothing of the sort. */
    return hi >= TINT_MIN_LEVEL && c[1] * 3 < lo && spread * 4 <= hi;
}

static inline int chan_dist(const unsigned char *p, const unsigned char *q)
{
    int dr = p[0] > q[0] ? p[0] - q[0] : q[0] - p[0];
    int dg = p[1] > q[1] ? p[1] - q[1] : q[1] - p[1];
    int db = p[2] > q[2] ? p[2] - q[2] : q[2] - p[2];
    int m = dr > dg ? dr : dg;
    return m > db ? m : db;
}

/* Read the _<w>x<h> suffix out of a file name: "run_64x64.png" -> 64, 64.
 * Returns 0 on success, -1 if the name carries no suffix. */
static inline int parse_frame_size(const char *path, int *fw, int *fh)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    if (!dot) {
        return -1;
    }
    for (const char *p = dot - 1; p > base; p--) {
        if (*p != '_') {
            continue;
        }
        int w = 0, h = 0, n = 0;
        if (sscanf(p + 1, "%dx%d%n", &w, &h, &n) == 2 && p + 1 + n == dot && w > 0 && h > 0) {
            *fw = w;
            *fh = h;
            return 0;
        }
    }
    return -1;
}

#endif
