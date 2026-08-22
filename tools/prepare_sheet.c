/* prepare_sheet — turns a raw picture from a generative model into asset
 * sheets the game can load.
 *
 * The model returns one large image: poses laid out on a magenta field, with
 * a compressed, uneven background, soft edges and no alpha at all. This tool
 * finds the objects on that field, brings them to a common scale and anchor,
 * and writes the strips named in docs/assets-spec.md.
 *
 * Not built by default; see the BUILD_TOOLS option in CMakeLists.txt.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "sheet_common.h"

#define MAX_ROWS 8
#define MAX_BLOBS 16
#define MAX_OUTS 4

typedef struct {
    int x0, x1, y0, y1;
    int clip_y0;  /* top of the row this blob belongs to */
    int anchor_x; /* horizontal centre of the bottom of the silhouette */
} Blob;

typedef struct {
    int y0, y1;
    int ground; /* lowest occupied row across the whole band */
    int n;
    Blob blobs[MAX_BLOBS];
} SheetRow;

typedef struct {
    const char *name;   /* output base name; "" means take it from --name */
    const char *suffix; /* appended to --name, e.g. "_fp" */
    int count;          /* blobs to take from the row; 0 = all that are left */
} OutSpec;

typedef struct {
    int n;
    OutSpec outs[MAX_OUTS];
} RowSpec;

typedef struct {
    const char *name;
    int fw, fh;
    int nrows; /* expected rows; 0 = however many are found */
    RowSpec rows[MAX_ROWS];
    int fit;      /* 1 = scale so the largest blob fits the frame */
    float fill;   /* otherwise: share of the frame height the reference takes */
    int per_blob; /* 1 = one file per blob, names come from --names */
} Profile;

static const Profile PROFILES[] = {
        {"player",
         64,
         64,
         4,
         {{2, {{"idle", "", 1}, {"run", "", 0}}},
          {1, {{"shoot", "", 0}}},
          {1, {{"pain", "", 0}}},
          {1, {{"death", "", 0}}}},
         0,
         0.85f,
         0},
        {"weapon_fp", 128, 128, 1, {{1, {{"", "_fp", 0}}}}, 1, 0.0f, 0},
        {"strip", 64, 64, 1, {{1, {{"", "", 0}}}}, 1, 0.0f, 0},
        {"singles", 64, 64, 1, {{1, {{"", "", 0}}}}, 1, 0.0f, 1},
};

static const int PROFILE_COUNT = (int)(sizeof(PROFILES) / sizeof(PROFILES[0]));

/* ---- image ---- */

typedef struct {
    int w, h;
    unsigned char *rgb;   /* 3 bytes per pixel, as loaded */
    unsigned char *alpha; /* 0..255, from the distance to the background */
    unsigned char bg[3];
} Src;

/* The background is the median of the border, per channel: the border is all
 * background on every sheet, and a median shrugs off the compression noise
 * that makes a plain "most common colour" unstable. */
static void find_background(Src *s)
{
    int n = 2 * (s->w + s->h);
    unsigned char *buf = (unsigned char *)malloc((size_t)n);
    if (!buf) {
        s->bg[0] = KEY_R;
        s->bg[1] = KEY_G;
        s->bg[2] = KEY_B;
        return;
    }
    for (int c = 0; c < 3; c++) {
        int k = 0;
        for (int x = 0; x < s->w; x++) {
            buf[k++] = s->rgb[((size_t)0 * s->w + x) * 3 + c];
            buf[k++] = s->rgb[((size_t)(s->h - 1) * s->w + x) * 3 + c];
        }
        for (int y = 0; y < s->h; y++) {
            buf[k++] = s->rgb[((size_t)y * s->w + 0) * 3 + c];
            buf[k++] = s->rgb[((size_t)y * s->w + (s->w - 1)) * 3 + c];
        }
        /* counting sort over 256 buckets, then pick the middle */
        int hist[256];
        memset(hist, 0, sizeof(hist));
        for (int i = 0; i < k; i++) {
            hist[buf[i]]++;
        }
        int acc = 0;
        for (int v = 0; v < 256; v++) {
            acc += hist[v];
            if (acc * 2 >= k) {
                s->bg[c] = (unsigned char)v;
                break;
            }
        }
    }
    free(buf);
}

static void build_alpha(Src *s)
{
    size_t n = (size_t)s->w * s->h;
    for (size_t i = 0; i < n; i++) {
        int d = chan_dist(s->rgb + i * 3, s->bg);
        int a;
        if (d <= TOL_BG) {
            a = 0;
        } else if (d >= TOL_FG) {
            a = 255;
        } else {
            a = (d - TOL_BG) * 255 / (TOL_FG - TOL_BG);
        }
        s->alpha[i] = (unsigned char)a;
    }
}

static int solid(const Src *s, int x, int y)
{
    return s->alpha[(size_t)y * s->w + x] == 255;
}

/* ---- segmentation ---- */

/* Bands of occupied indices, merging anything separated by less than min_gap
 * and dropping anything shorter than min_len. Returns the number of bands. */
static int find_bands(const unsigned char *occ, int n, int min_gap, int min_len, int *out,
                      int max_out)
{
    int count = 0;
    int start = -1;
    for (int i = 0; i <= n; i++) {
        int on = (i < n) ? occ[i] : 0;
        if (on && start < 0) {
            start = i;
        } else if (!on && start >= 0) {
            if (count > 0 && start - out[2 * (count - 1) + 1] - 1 < min_gap) {
                out[2 * (count - 1) + 1] = i - 1; /* close enough: same band */
            } else if (count < max_out) {
                out[2 * count] = start;
                out[2 * count + 1] = i - 1;
                count++;
            } else {
                return -1; /* more bands than we can hold */
            }
            start = -1;
        }
    }
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (out[2 * i + 1] - out[2 * i] + 1 >= min_len) {
            out[2 * kept] = out[2 * i];
            out[2 * kept + 1] = out[2 * i + 1];
            kept++;
        }
    }
    return kept;
}

static void measure_blob(const Src *s, Blob *b)
{
    int top = b->y1, bot = b->y0;
    for (int y = b->y0; y <= b->y1; y++) {
        for (int x = b->x0; x <= b->x1; x++) {
            if (solid(s, x, y)) {
                if (y < top) {
                    top = y;
                }
                if (y > bot) {
                    bot = y;
                }
                break;
            }
        }
    }
    b->y0 = top;
    b->y1 = bot;

    /* Anchor on the bottom fifth of the silhouette — the feet of a character,
     * the hands of a first-person weapon. The bounding box centre would do
     * instead, but a muzzle flash sticking out to one side drags it along and
     * the whole figure jumps sideways on the firing frame. */
    int cut = b->y1 - (b->y1 - b->y0) / 5;
    int lo = b->x1, hi = b->x0, any = 0;
    for (int y = cut; y <= b->y1; y++) {
        for (int x = b->x0; x <= b->x1; x++) {
            if (!solid(s, x, y)) {
                continue;
            }
            if (x < lo) {
                lo = x;
            }
            if (x > hi) {
                hi = x;
            }
            any = 1;
        }
    }
    b->anchor_x = any ? (lo + hi) / 2 : (b->x0 + b->x1) / 2;
}

static int segment(const Src *s, SheetRow *rows, int max_rows, int min_gap, int min_len)
{
    unsigned char *occ = (unsigned char *)malloc((size_t)(s->w > s->h ? s->w : s->h));
    int *bands = (int *)malloc(sizeof(int) * 2 * MAX_BLOBS);
    if (!occ || !bands) {
        free(occ);
        free(bands);
        return -1;
    }

    for (int y = 0; y < s->h; y++) {
        occ[y] = 0;
        for (int x = 0; x < s->w; x++) {
            if (solid(s, x, y)) {
                occ[y] = 1;
                break;
            }
        }
    }
    int nrows = find_bands(occ, s->h, min_gap, min_len, bands, MAX_BLOBS);
    if (nrows < 0 || nrows > max_rows) {
        free(occ);
        free(bands);
        return -1;
    }

    int rowspan[MAX_ROWS * 2];
    for (int i = 0; i < nrows * 2; i++) {
        rowspan[i] = bands[i];
    }

    for (int r = 0; r < nrows; r++) {
        SheetRow *row = &rows[r];
        row->y0 = rowspan[2 * r];
        row->y1 = rowspan[2 * r + 1];
        for (int x = 0; x < s->w; x++) {
            occ[x] = 0;
            for (int y = row->y0; y <= row->y1; y++) {
                if (solid(s, x, y)) {
                    occ[x] = 1;
                    break;
                }
            }
        }
        int nb = find_bands(occ, s->w, min_gap, min_len, bands, MAX_BLOBS);
        if (nb < 0) {
            free(occ);
            free(bands);
            return -1;
        }
        row->n = nb;
        row->ground = row->y0;
        for (int i = 0; i < nb; i++) {
            Blob *b = &row->blobs[i];
            b->x0 = bands[2 * i];
            b->x1 = bands[2 * i + 1];
            b->y0 = row->y0;
            b->y1 = row->y1;
            b->clip_y0 = row->y0;
            measure_blob(s, b);
            if (b->y1 > row->ground) {
                row->ground = b->y1;
            }
        }
    }

    free(occ);
    free(bands);
    return nrows;
}

/* ---- output ---- */

static int ensure_dir(const char *path)
{
    char buf[512];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i <= n; i++) {
        if (buf[i] != '/' && buf[i] != '\0') {
            continue;
        }
        char save = buf[i];
        buf[i] = '\0';
#ifdef _WIN32
        _mkdir(buf);
#else
        mkdir(buf, 0777);
#endif
        buf[i] = save;
    }
    return 0;
}

/* One output frame: box-average the source region that maps onto it, with the
 * colour weighted by alpha so the magenta never bleeds into the edge, then
 * snap coverage to fully opaque or fully transparent. */
static void render_frame(const Src *s, const Blob *b, int ground, float scale, int fw, int fh,
                         unsigned char *dst, int dst_stride_px, int frame_x)
{
    float ax = (float)b->anchor_x + 0.5f;
    float gy = (float)ground + 1.0f;

    for (int oy = 0; oy < fh; oy++) {
        for (int ox = 0; ox < fw; ox++) {
            float fx0 = ax + ((float)ox - fw * 0.5f) / scale;
            float fx1 = ax + ((float)ox + 1.0f - fw * 0.5f) / scale;
            float fy0 = gy - (float)(fh - oy) / scale;
            float fy1 = gy - (float)(fh - oy - 1) / scale;

            /* half-open source range: a boundary that lands exactly on a
             * pixel edge belongs to the next output pixel, not this one */
            int ix0 = (int)floorf(fx0);
            int ix1 = (int)ceilf(fx1) - 1;
            int iy0 = (int)floorf(fy0);
            int iy1 = (int)ceilf(fy1) - 1;
            if (ix1 < ix0) {
                ix1 = ix0;
            }
            if (iy1 < iy0) {
                iy1 = iy0;
            }

            double sa = 0.0, sr = 0.0, sg = 0.0, sb = 0.0;
            int cnt = 0;
            for (int y = iy0; y <= iy1; y++) {
                if (y < 0 || y >= s->h || y < b->clip_y0 || y > ground) {
                    cnt++;
                    continue;
                }
                for (int x = ix0; x <= ix1; x++) {
                    if (x < 0 || x >= s->w || x < b->x0 || x > b->x1) {
                        cnt++;
                        continue;
                    }
                    size_t i = (size_t)y * s->w + x;
                    double a = s->alpha[i];
                    sa += a;
                    sr += s->rgb[i * 3 + 0] * a;
                    sg += s->rgb[i * 3 + 1] * a;
                    sb += s->rgb[i * 3 + 2] * a;
                    cnt++;
                }
            }

            unsigned char *p = dst + ((size_t)oy * dst_stride_px + frame_x + ox) * 4;
            if (cnt > 0 && sa >= 0.5 * cnt * 255.0) {
                p[0] = (unsigned char)(sr / sa + 0.5);
                p[1] = (unsigned char)(sg / sa + 0.5);
                p[2] = (unsigned char)(sb / sa + 0.5);
                p[3] = 255;
            } else {
                p[0] = KEY_R;
                p[1] = KEY_G;
                p[2] = KEY_B;
                p[3] = 0;
            }
        }
    }
}

static int write_sheet(const Src *s, const Blob *blobs, const int *grounds, int nframes,
                       float scale, int fw, int fh, const char *dir, const char *base)
{
    size_t px = (size_t)fw * nframes * fh;
    unsigned char *buf = (unsigned char *)malloc(px * 4);
    if (!buf) {
        fprintf(stderr, "prepare_sheet: не хватило памяти под %s\n", base);
        return -1;
    }
    for (size_t i = 0; i < px; i++) {
        buf[i * 4 + 0] = KEY_R;
        buf[i * 4 + 1] = KEY_G;
        buf[i * 4 + 2] = KEY_B;
        buf[i * 4 + 3] = 0;
    }
    for (int i = 0; i < nframes; i++) {
        render_frame(s, &blobs[i], grounds[i], scale, fw, fh, buf, fw * nframes, i * fw);
    }

    char path[512];
    snprintf(path, sizeof(path), "%.400s/%.60s_%dx%d.png", dir, base, fw, fh);
    int ok = stbi_write_png(path, fw * nframes, fh, 4, buf, fw * nframes * 4);
    free(buf);
    if (!ok) {
        fprintf(stderr, "prepare_sheet: не удалось записать %s\n", path);
        return -1;
    }
    printf("  %s  %d кадр(ов) %dx%d\n", path, nframes, fw, fh);
    return 0;
}

static void draw_debug(const Src *s, const SheetRow *rows, int nrows, const char *src_path)
{
    unsigned char *buf = (unsigned char *)malloc((size_t)s->w * s->h * 3);
    if (!buf) {
        return;
    }
    memcpy(buf, s->rgb, (size_t)s->w * s->h * 3);

    for (int r = 0; r < nrows; r++) {
        for (int i = 0; i < rows[r].n; i++) {
            const Blob *b = &rows[r].blobs[i];
            for (int x = b->x0; x <= b->x1; x++) {
                for (int t = 0; t < 3; t++) {
                    int ys[2] = {b->y0 + t, b->y1 - t};
                    for (int k = 0; k < 2; k++) {
                        if (ys[k] < 0 || ys[k] >= s->h) {
                            continue;
                        }
                        size_t o = ((size_t)ys[k] * s->w + x) * 3;
                        buf[o] = 0;
                        buf[o + 1] = 255;
                        buf[o + 2] = 0;
                    }
                }
            }
            for (int y = b->y0; y <= b->y1; y++) {
                for (int t = 0; t < 3; t++) {
                    int xs[3] = {b->x0 + t, b->x1 - t, b->anchor_x + t - 1};
                    for (int k = 0; k < 3; k++) {
                        if (xs[k] < 0 || xs[k] >= s->w) {
                            continue;
                        }
                        size_t o = ((size_t)y * s->w + xs[k]) * 3;
                        buf[o] = (k == 2) ? 255 : 0;
                        buf[o + 1] = (k == 2) ? 255 : 255;
                        buf[o + 2] = 0;
                    }
                }
            }
        }
        for (int x = 0; x < s->w; x++) {
            size_t o = ((size_t)rows[r].ground * s->w + x) * 3;
            buf[o] = 0;
            buf[o + 1] = 128;
            buf[o + 2] = 255;
        }
    }

    char path[512];
    const char *dot = strrchr(src_path, '.');
    int n = dot ? (int)(dot - src_path) : (int)strlen(src_path);
    snprintf(path, sizeof(path), "%.*s_debug.png", n, src_path);
    if (stbi_write_png(path, s->w, s->h, 3, buf, s->w * 3)) {
        printf("  разметка: %s\n", path);
    }
    free(buf);
}

/* ---- driver ---- */

static void usage(void)
{
    fprintf(stderr,
            "использование: prepare_sheet <raw.png> --profile <профиль> --out <каталог>\n"
            "               [--name <база>] [--names a,b,c] [--fw N --fh N]\n"
            "               [--fill 0.85] [--shrink] [--min-gap N] [--min-size N] [--debug]\n"
            "\nпрофили: ");
    for (int i = 0; i < PROFILE_COUNT; i++) {
        fprintf(stderr, "%s%s", PROFILES[i].name, i + 1 < PROFILE_COUNT ? ", " : "\n");
    }
}

int main(int argc, char **argv)
{
    const char *src_path = NULL, *prof_name = NULL, *out_dir = NULL;
    const char *base = NULL, *names = NULL;
    int fw = 0, fh = 0, shrink = 0, debug = 0;
    int min_gap = 12, min_size = 16;
    float fill = -1.0f;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' && !src_path) {
            src_path = a;
        } else if (!strcmp(a, "--profile") && i + 1 < argc) {
            prof_name = argv[++i];
        } else if (!strcmp(a, "--out") && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (!strcmp(a, "--name") && i + 1 < argc) {
            base = argv[++i];
        } else if (!strcmp(a, "--names") && i + 1 < argc) {
            names = argv[++i];
        } else if (!strcmp(a, "--fw") && i + 1 < argc) {
            fw = atoi(argv[++i]);
        } else if (!strcmp(a, "--fh") && i + 1 < argc) {
            fh = atoi(argv[++i]);
        } else if (!strcmp(a, "--fill") && i + 1 < argc) {
            fill = (float)atof(argv[++i]);
        } else if (!strcmp(a, "--min-gap") && i + 1 < argc) {
            min_gap = atoi(argv[++i]);
        } else if (!strcmp(a, "--min-size") && i + 1 < argc) {
            min_size = atoi(argv[++i]);
        } else if (!strcmp(a, "--shrink")) {
            shrink = 1;
        } else if (!strcmp(a, "--debug")) {
            debug = 1;
        } else {
            usage();
            return 2;
        }
    }
    if (!src_path || !prof_name || !out_dir) {
        usage();
        return 2;
    }

    const Profile *prof = NULL;
    for (int i = 0; i < PROFILE_COUNT; i++) {
        if (!strcmp(PROFILES[i].name, prof_name)) {
            prof = &PROFILES[i];
        }
    }
    if (!prof) {
        fprintf(stderr, "prepare_sheet: неизвестный профиль %s\n", prof_name);
        usage();
        return 2;
    }
    if (fw <= 0) {
        fw = prof->fw;
    }
    if (fh <= 0) {
        fh = prof->fh;
    }
    if (fill <= 0.0f) {
        fill = prof->fill;
    }

    Src s;
    int comp = 0;
    s.rgb = stbi_load(src_path, &s.w, &s.h, &comp, 3);
    if (!s.rgb) {
        fprintf(stderr, "prepare_sheet: не читается %s: %s\n", src_path, stbi_failure_reason());
        return 1;
    }
    s.alpha = (unsigned char *)malloc((size_t)s.w * s.h);
    if (!s.alpha) {
        fprintf(stderr, "prepare_sheet: не хватило памяти\n");
        return 1;
    }
    find_background(&s);
    build_alpha(&s);
    printf("%s: %dx%d, фон #%02X%02X%02X\n", src_path, s.w, s.h, s.bg[0], s.bg[1], s.bg[2]);

    SheetRow rows[MAX_ROWS];
    int nrows = segment(&s, rows, MAX_ROWS, min_gap, min_size);
    if (nrows <= 0) {
        fprintf(stderr, "prepare_sheet: не удалось разобрать лист на объекты\n");
        return 1;
    }
    for (int r = 0; r < nrows; r++) {
        printf("  ряд %d: y %d..%d, объектов %d, земля y=%d\n", r + 1, rows[r].y0, rows[r].y1,
               rows[r].n, rows[r].ground);
    }
    if (debug) {
        draw_debug(&s, rows, nrows, src_path);
    }

    if (prof->nrows && nrows != prof->nrows) {
        fprintf(stderr,
                "prepare_sheet: профиль %s ждёт %d ряд(ов), а найдено %d — "
                "проверьте разметку (--debug) или промежутки (--min-gap)\n",
                prof->name, prof->nrows, nrows);
        return 1;
    }

    /* scale: one number for the whole sheet, so relative sizes survive */
    float scale;
    float fit = 1e9f;
    for (int r = 0; r < nrows; r++) {
        for (int i = 0; i < rows[r].n; i++) {
            const Blob *b = &rows[r].blobs[i];
            /* one pixel of headroom: the width is centred, so it needs a
             * margin on both sides, the height only above — the bottom of
             * the blob is pinned to the bottom of the frame */
            float sx = (float)(fw - 2) / (float)(b->x1 - b->x0 + 1);
            float sy = (float)(fh - 1) / (float)(b->y1 - b->y0 + 1);
            if (sx < fit) {
                fit = sx;
            }
            if (sy < fit) {
                fit = sy;
            }
        }
    }
    if (prof->fit) {
        scale = fit;
    } else {
        const Blob *ref = &rows[0].blobs[0];
        scale = fill * (float)fh / (float)(ref->y1 - ref->y0 + 1);
        if (scale > fit) {
            if (shrink) {
                printf("  масштаб уменьшен с %.4f до %.4f, чтобы ничего не обрезать\n", scale, fit);
                scale = fit;
            } else {
                fprintf(stderr,
                        "prepare_sheet: при масштабе %.4f часть объектов не влезает в кадр "
                        "%dx%d; --shrink уменьшит масштаб (и рост персонажа) до %.4f\n",
                        scale, fw, fh, fit);
            }
        }
    }
    printf("  масштаб %.4f (1:%.1f)\n", scale, 1.0f / scale);

    if (ensure_dir(out_dir) != 0) {
        fprintf(stderr, "prepare_sheet: не создать каталог %s\n", out_dir);
        return 1;
    }

    /* per_blob: one file per object, names given on the command line */
    if (prof->per_blob) {
        if (!names) {
            fprintf(stderr, "prepare_sheet: профиль %s требует --names\n", prof->name);
            return 2;
        }
        char list[512];
        snprintf(list, sizeof(list), "%s", names);
        int total = 0;
        for (int r = 0; r < nrows; r++) {
            total += rows[r].n;
        }
        char *save = list;
        int used = 0;
        for (int r = 0; r < nrows; r++) {
            for (int i = 0; i < rows[r].n; i++) {
                char *comma = strchr(save, ',');
                if (comma) {
                    *comma = '\0';
                }
                if (!*save) {
                    fprintf(stderr, "prepare_sheet: имён в --names меньше, чем объектов (%d)\n",
                            total);
                    return 1;
                }
                int g = rows[r].ground;
                if (write_sheet(&s, &rows[r].blobs[i], &g, 1, scale, fw, fh, out_dir, save) != 0) {
                    return 1;
                }
                used++;
                save = comma ? comma + 1 : save + strlen(save);
            }
        }
        if (*save) {
            fprintf(stderr, "prepare_sheet: имён в --names больше, чем объектов (%d)\n", used);
            return 1;
        }
        return 0;
    }

    for (int r = 0; r < nrows; r++) {
        const RowSpec *spec = &prof->rows[r];
        int taken = 0;
        for (int o = 0; o < spec->n; o++) {
            const OutSpec *out = &spec->outs[o];
            int count = out->count ? out->count : rows[r].n - taken;
            if (count <= 0 || taken + count > rows[r].n) {
                fprintf(stderr, "prepare_sheet: в ряду %d объектов %d, для «%s» их не хватает\n",
                        r + 1, rows[r].n, out->name[0] ? out->name : "выход");
                return 1;
            }
            char name[128];
            if (out->name[0]) {
                snprintf(name, sizeof(name), "%s", out->name);
            } else if (base) {
                snprintf(name, sizeof(name), "%s%s", base, out->suffix);
            } else {
                fprintf(stderr, "prepare_sheet: профиль %s требует --name\n", prof->name);
                return 2;
            }
            int grounds[MAX_BLOBS];
            for (int i = 0; i < count; i++) {
                grounds[i] = rows[r].ground;
            }
            if (write_sheet(&s, &rows[r].blobs[taken], grounds, count, scale, fw, fh, out_dir,
                            name) != 0) {
                return 1;
            }
            taken += count;
        }
    }
    return 0;
}
