/* check_sheets — validates the asset sheets that are already in assets/.
 *
 * prepare_sheet enforces the frame grid and the transparency; this tool
 * catches what a single sheet cannot know about itself — most of all whether
 * ten skins drawn in ten separate runs of a generative model ended up the
 * same height and standing on the same line.
 *
 * Exits non-zero if anything failed. Not built by default; see BUILD_TOOLS.
 */

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#include "sheet_common.h"

#define MAX_SKINS 32

/* How far apart two skins may be before they read as different people: the
 * standing silhouette measured in frame pixels. */
#define SKIN_HEIGHT_TOL 4
#define SKIN_BOTTOM_TOL 2

static int errors = 0;
static int warnings = 0;

static void fail(const char *path, const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "  ОШИБКА %s: ", path);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    errors++;
}

static void warn(const char *path, const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "  предупреждение %s: ", path);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    warnings++;
}

/* Expected frame counts, by file base name. A mismatch is a warning, not an
 * error: the length of an animation is content, and a longer strip is a
 * legitimate choice. */
static const struct {
    const char *name;
    int frames;
} EXPECTED[] = {
        {"idle", 1}, {"run", 4},       {"shoot", 5}, {"pain", 5}, {"death", 5},
        {"face", 7}, {"explosion", 6}, {"blood", 3}, {"puff", 3}, {"grenade", 1},
};

static int expected_frames(const char *base)
{
    for (size_t i = 0; i < sizeof(EXPECTED) / sizeof(EXPECTED[0]); i++) {
        if (!strcmp(EXPECTED[i].name, base)) {
            return EXPECTED[i].frames;
        }
    }
    if (strstr(base, "_fp")) {
        return 4;
    }
    return 0; /* unknown: no expectation */
}

typedef struct {
    int height; /* silhouette height in the first frame */
    int bottom; /* gap between the silhouette and the bottom of the frame */
    int ok;
} Silhouette;

static Silhouette measure_first_frame(const unsigned char *px, int w, int fw, int fh)
{
    Silhouette s = {0, 0, 0};
    int top = -1, bot = -1;
    for (int y = 0; y < fh; y++) {
        for (int x = 0; x < fw; x++) {
            if (px[((size_t)y * w + x) * 4 + 3] == 0) {
                continue;
            }
            if (top < 0) {
                top = y;
            }
            bot = y;
            break;
        }
    }
    if (top < 0) {
        return s;
    }
    s.height = bot - top + 1;
    s.bottom = fh - 1 - bot;
    s.ok = 1;
    return s;
}

static void check_file(const char *path, Silhouette *out)
{
    int fw = 0, fh = 0;
    if (parse_frame_size(path, &fw, &fh) != 0) {
        fail(path, "в имени нет суффикса _<ширина>x<высота>");
        return;
    }
    int w = 0, h = 0, comp = 0;
    unsigned char *px = stbi_load(path, &w, &h, &comp, 4);
    if (!px) {
        fail(path, "не читается: %s", stbi_failure_reason());
        return;
    }
    if (w % fw || h % fh) {
        fail(path, "%dx%d не кратно кадру %dx%d", w, h, fw, fh);
        stbi_image_free(px);
        return;
    }
    int cols = w / fw, rows = h / fh;

    int soft = 0, pink = 0;
    unsigned char key[3] = {KEY_R, KEY_G, KEY_B};
    for (size_t i = 0; i < (size_t)w * h; i++) {
        unsigned char a = px[i * 4 + 3];
        if (a != 0 && a != 255) {
            soft++;
        }
        if (a != 0 && chan_dist(px + i * 4, key) <= TOL_FG) {
            pink++;
        }
    }
    if (soft) {
        fail(path, "%d полупрозрачных пикселей — рендер не смешивает полутона", soft);
    }
    if (pink) {
        fail(path, "%d непрозрачных пикселей близки к фону — розовая кайма", pink);
    }

    /* An opaque pixel on a frame edge means the picture is cropped: fine along
     * the bottom, where the sprite stands, wrong anywhere else. */
    int clipped = 0;
    for (int f = 0; f < cols * rows; f++) {
        int fx = (f % cols) * fw, fy = (f / cols) * fh;
        for (int x = 0; x < fw; x++) {
            if (px[((size_t)fy * w + fx + x) * 4 + 3]) {
                clipped++;
            }
        }
        for (int y = 0; y < fh; y++) {
            if (px[((size_t)(fy + y) * w + fx) * 4 + 3]) {
                clipped++;
            }
            if (px[((size_t)(fy + y) * w + fx + fw - 1) * 4 + 3]) {
                clipped++;
            }
        }
    }
    if (clipped) {
        warn(path, "%d непрозрачных пикселей упираются в край кадра — картинка обрезана", clipped);
    }

    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    char base[128];
    snprintf(base, sizeof(base), "%s", b);
    char *us = strrchr(base, '_');
    if (us) {
        *us = '\0';
    }
    int want = expected_frames(base);
    if (want && cols != want) {
        warn(path, "кадров %d, по спецификации ожидалось %d", cols, want);
    }

    if (out) {
        *out = measure_first_frame(px, w, fw, fh);
    }
    stbi_image_free(px);
}

static int is_png(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && !strcmp(name + n - 4, ".png");
}

static void walk(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') {
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "%.400s/%.100s", dir, e->d_name);
        if (is_png(e->d_name)) {
            check_file(path, NULL);
        } else {
            walk(path); /* opendir fails harmlessly on plain files */
        }
    }
    closedir(d);
}

static const char *SKIN_ANIMS[] = {"idle", "run", "shoot", "pain", "death"};

static void check_skins(const char *root)
{
    char list[512];
    snprintf(list, sizeof(list), "%.400s/players/skins.txt", root);
    FILE *f = fopen(list, "r");
    if (!f) {
        printf("  %s отсутствует — проверка скинов пропущена\n", list);
        return;
    }
    char names[MAX_SKINS][64];
    int n = 0;
    while (n < MAX_SKINS && fgets(names[n], sizeof(names[0]), f)) {
        char *s = names[n];
        size_t len = strlen(s);
        while (len && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ')) {
            s[--len] = '\0';
        }
        if (len) {
            n++;
        }
    }
    fclose(f);

    Silhouette ref = {0, 0, 0};
    const char *ref_name = NULL;
    for (int i = 0; i < n; i++) {
        for (size_t a = 0; a < sizeof(SKIN_ANIMS) / sizeof(SKIN_ANIMS[0]); a++) {
            char path[512];
            snprintf(path, sizeof(path), "%.200s/players/%.60s/%.20s_64x64.png", root, names[i],
                     SKIN_ANIMS[a]);
            FILE *t = fopen(path, "rb");
            if (!t) {
                fail(path, "скин %s не полон: файла нет", names[i]);
                continue;
            }
            fclose(t);
            if (strcmp(SKIN_ANIMS[a], "idle")) {
                continue;
            }
            Silhouette s;
            check_file(path, &s);
            if (!s.ok) {
                fail(path, "кадр пуст");
                continue;
            }
            if (!ref.ok) {
                ref = s;
                ref_name = names[i];
                continue;
            }
            int dh = s.height - ref.height;
            int db = s.bottom - ref.bottom;
            if (dh > SKIN_HEIGHT_TOL || dh < -SKIN_HEIGHT_TOL) {
                fail(path, "рост %d px против %d у %s — персонажи разного размера", s.height,
                     ref.height, ref_name);
            }
            if (db > SKIN_BOTTOM_TOL || db < -SKIN_BOTTOM_TOL) {
                fail(path, "ноги на %d px от низа кадра против %d у %s", s.bottom, ref.bottom,
                     ref_name);
            }
        }
    }
    printf("  скинов в списке: %d\n", n);
}

int main(int argc, char **argv)
{
    const char *root = argc > 1 ? argv[1] : "assets";
    printf("check_sheets: %s\n", root);
    walk(root);
    check_skins(root);
    if (errors || warnings) {
        printf("итог: ошибок %d, предупреждений %d\n", errors, warnings);
    }
    return errors ? 1 : 0;
}
