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
