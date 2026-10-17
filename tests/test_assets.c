/* assets.c: the frame-size suffix and the sheet loader.
 *
 * The generators need no test — a framebuffer is what would show them wrong.
 * What is checked here is the contract between a file name and the code that
 * asks for it, the four ways a file can be refused, and how the list of skins
 * turns into skin numbers.
 *
 * PNGs are written by the test itself rather than kept as fixtures: the
 * suffix is part of the name, so a case needs a file named for it, and CTest
 * runs from the build directory where a fixture path would have to be
 * threaded in from CMake. */

#include "unity.h"
#include "assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#define remove_dir(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(p) mkdir((p), 0777)
#define remove_dir(p) rmdir(p)
#endif

/* ---- Minimal PNG writer ---- */
/* stb_image reads PNG but nothing here writes one, and pulling in a writer
 * for four files is not worth it: deflate has a stored-block mode, so the
 * pixel data goes out uncompressed and only the two checksums are real. */

static uint32_t crc32_of(const uint8_t *p, size_t n, uint32_t crc)
{
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void put_chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
    uint8_t hdr[4];
    put_be32(hdr, (uint32_t)n);
    fwrite(hdr, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (n) {
        fwrite(data, 1, n, f);
    }
    uint32_t crc = crc32_of((const uint8_t *)type, 4, 0);
    crc = crc32_of(data, n, crc);
    put_be32(hdr, crc);
    fwrite(hdr, 1, 4, f);
}

/* RGBA pixels, row-major. Returns 0 on success. */
static int write_png(const char *path, int w, int h, const uint8_t *rgba)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    static const uint8_t SIG[8] = {137, 'P', 'N', 'G', '\r', '\n', 26, '\n'};
    fwrite(SIG, 1, 8, f);

    uint8_t ihdr[13];
    put_be32(ihdr, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 6;  /* colour type: RGBA */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* no filtering beyond the per-row byte */
    ihdr[12] = 0; /* no interlace */
    put_chunk(f, "IHDR", ihdr, sizeof(ihdr));

    /* Raw stream: one filter byte (0 = none) in front of every row. */
    size_t stride = (size_t)w * 4 + 1;
    size_t raw_n = stride * (size_t)h;
    uint8_t *raw = (uint8_t *)malloc(raw_n);
    if (!raw) {
        fclose(f);
        return -1;
    }
    for (int y = 0; y < h; y++) {
        raw[y * stride] = 0;
        memcpy(raw + y * stride + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
    }

    /* zlib: 0x78 0x01, stored blocks of at most 65535 bytes, adler32. */
    size_t max_z = 2 + raw_n + 5 * (raw_n / 65535 + 1) + 4;
    uint8_t *z = (uint8_t *)malloc(max_z);
    if (!z) {
        free(raw);
        fclose(f);
        return -1;
    }
    size_t zn = 0;
    z[zn++] = 0x78;
    z[zn++] = 0x01;
    size_t off = 0;
    do {
        size_t block = raw_n - off > 65535 ? 65535 : raw_n - off;
        z[zn++] = (uint8_t)(off + block >= raw_n ? 1 : 0);
        z[zn++] = (uint8_t)(block & 0xFF);
        z[zn++] = (uint8_t)(block >> 8);
        z[zn++] = (uint8_t)(~block & 0xFF);
        z[zn++] = (uint8_t)((~block >> 8) & 0xFF);
        memcpy(z + zn, raw + off, block);
        zn += block;
        off += block;
    } while (off < raw_n);

    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    put_be32(z + zn, (b << 16) | a);
    zn += 4;

    put_chunk(f, "IDAT", z, zn);
    put_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z);
    free(raw);
    return 0;
}

/* A sheet of solid magenta with one opaque red pixel and one half-transparent
 * green pixel at the top-left, so keying and the alpha threshold are visible
 * in the loaded result. */
static int write_sheet(const char *path, int w, int h)
{
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
    if (!px) {
        return -1;
    }
    for (int i = 0; i < w * h; i++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 255;
        px[i * 4 + 3] = 255;
    }
    const uint8_t red[4] = {200, 30, 40, 255};
    const uint8_t green[4] = {30, 200, 40, 100};
    memcpy(px, red, 4);
    memcpy(px + 4, green, 4);
    int rc = write_png(path, w, h, px);
    free(px);
    return rc;
}

/* ---- Fixtures ---- */

#define SHEET_OK "test_sheet_64x64.png"
#define SHEET_ODD "test_odd_64x64.png"
#define SHEET_JUNK "test_junk_64x64.png"

static Texture tex;

void setUp(void)
{
    memset(&tex, 0, sizeof(tex));
}

void tearDown(void)
{
    texture_free(&tex);
}

/* ---- The name ---- */

static void test_parse_frame_size_reads_suffix(void)
{
    int fw = 0, fh = 0;
    TEST_ASSERT_EQUAL_INT(
            0, assets_parse_frame_size("assets/weapons/pistol_fp_128x128.png", &fw, &fh));
    TEST_ASSERT_EQUAL_INT(128, fw);
    TEST_ASSERT_EQUAL_INT(128, fh);

    TEST_ASSERT_EQUAL_INT(0, assets_parse_frame_size("icon_pistol_32x16.png", &fw, &fh));
    TEST_ASSERT_EQUAL_INT(32, fw);
    TEST_ASSERT_EQUAL_INT(16, fh);
}

static void test_parse_frame_size_rejects_names_without_one(void)
{
    int fw = 0, fh = 0;
    TEST_ASSERT_NOT_EQUAL(0, assets_parse_frame_size("assets/weapons/pistol.png", &fw, &fh));
    TEST_ASSERT_NOT_EQUAL(0, assets_parse_frame_size("pistol_64x.png", &fw, &fh));
    TEST_ASSERT_NOT_EQUAL(0, assets_parse_frame_size("pistol_64x64", &fw, &fh));
    /* The suffix has to end the name: a file whose size sits in the middle is
     * a different file, not a sheet of that size. */
    TEST_ASSERT_NOT_EQUAL(0, assets_parse_frame_size("pistol_64x64_alt.png", &fw, &fh));
}

/* ---- The loader ---- */

static void test_sheet_splits_into_frames(void)
{
    TEST_ASSERT_EQUAL_INT(0, write_sheet(SHEET_OK, 256, 64));
    TEST_ASSERT_EQUAL_INT(0, assets_load_sheet(&tex, SHEET_OK, 64, 64));
    TEST_ASSERT_EQUAL_INT(256, tex.w);
    TEST_ASSERT_EQUAL_INT(64, tex.h);
    TEST_ASSERT_EQUAL_INT(64, tex.fw);
    TEST_ASSERT_EQUAL_INT(64, tex.fh);
    TEST_ASSERT_EQUAL_INT(4, tex.cols);
    TEST_ASSERT_EQUAL_INT(1, tex.rows);
    /* Frames are side by side, so the third one starts two frame widths in. */
    TEST_ASSERT_EQUAL_PTR(tex.pixels + 128, texture_frame(&tex, 2, 0));
    remove(SHEET_OK);
}

static void test_sheet_keys_out_magenta_and_hardens_alpha(void)
{
    TEST_ASSERT_EQUAL_INT(0, write_sheet(SHEET_OK, 256, 64));
    TEST_ASSERT_EQUAL_INT(0, assets_load_sheet(&tex, SHEET_OK, 64, 64));

    /* The background is gone whatever alpha it came in with. */
    TEST_ASSERT_EQUAL_HEX32(0u, tex.pixels[10] & 0xFF000000u);
    /* An opaque pixel stays, with the colour make_color() would have packed. */
    TEST_ASSERT_EQUAL_HEX32(make_color(200, 30, 40), tex.pixels[0]);
    /* alpha 100 is below the threshold, so the pixel drops out entirely
     * instead of being drawn as if it were solid. */
    TEST_ASSERT_EQUAL_HEX32(0u, tex.pixels[1] & 0xFF000000u);
    remove(SHEET_OK);
}

static void test_sheet_rejects_size_that_is_not_whole_frames(void)
{
    TEST_ASSERT_EQUAL_INT(0, write_sheet(SHEET_ODD, 100, 64));
    TEST_ASSERT_NOT_EQUAL(0, assets_load_sheet(&tex, SHEET_ODD, 64, 64));
    TEST_ASSERT_NULL(tex.pixels);
    remove(SHEET_ODD);
}

static void test_sheet_rejects_suffix_the_code_did_not_ask_for(void)
{
    /* Checked before the file is opened: the name alone settles it. */
    TEST_ASSERT_NOT_EQUAL(
            0, assets_load_sheet(&tex, "assets/players/marine/idle_64x64.png", 128, 128));
    TEST_ASSERT_NULL(tex.pixels);
    TEST_ASSERT_NOT_EQUAL(0, assets_load_sheet(&tex, "assets/weapons/pistol_fp.png", 128, 128));
    TEST_ASSERT_NULL(tex.pixels);
}

static void test_rejected_sheet_leaves_the_slot_alone(void)
{
    /* What assets_init() relies on: a broken file must not take the
     * procedural texture down with it. */
    TEST_ASSERT_EQUAL_INT(0, texture_alloc(&tex, TEX_SIZE, TEX_SIZE));
    tex.pixels[0] = make_color(1, 2, 3);
    uint32_t *before = tex.pixels;

    FILE *f = fopen(SHEET_JUNK, "wb");
    TEST_ASSERT_NOT_NULL(f);
    fwrite("this is not a png", 1, 17, f);
    fclose(f);

    TEST_ASSERT_NOT_EQUAL(0, assets_load_sheet(&tex, SHEET_JUNK, 64, 64));
    TEST_ASSERT_EQUAL_PTR(before, tex.pixels);
    TEST_ASSERT_EQUAL_HEX32(make_color(1, 2, 3), tex.pixels[0]);
    TEST_ASSERT_EQUAL_INT(TEX_SIZE, tex.fw);
    TEST_ASSERT_EQUAL_INT(1, tex.cols);
    remove(SHEET_JUNK);
}

/* ---- Characters ---- */

#define SKIN_DIR "test_players"

static Assets assets;

static void write_list(const char *text)
{
    FILE *f = fopen(SKIN_DIR "/skins.txt", "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(text, f);
    fclose(f);
}

static void assert_checker(const Texture *t)
{
    TEST_ASSERT_EQUAL_INT(TEX_SIZE, t->fw);
    TEST_ASSERT_EQUAL_INT(1, t->cols);
    TEST_ASSERT_EQUAL_MEMORY(assets.missing.pixels, t->pixels,
                             (size_t)TEX_SIZE * TEX_SIZE * sizeof(uint32_t));
}

/* CTest runs this from the build directory, where there is no assets/: the
 * client starts all the same, with every character drawn as the checker. */
static void test_without_the_skin_list_every_character_is_the_checker(void)
{
    TEST_ASSERT_EQUAL_INT(0, assets_init(&assets));
    TEST_ASSERT_EQUAL_INT(0, assets.skin_count);
    TEST_ASSERT_EQUAL_INT(-1, assets_skin_index(&assets, "marine"));
    for (int s = 0; s < MAX_SKINS; s++) {
        for (int k = 0; k < ANIM_COUNT; k++) {
            assert_checker(&assets.skin_anim[s][k]);
        }
    }
    assets_shutdown(&assets);
}

/* The line a name is on is its skin number — blank lines and line ends do not
 * count, and a name too long to keep still takes its number, so the ones
 * after it keep theirs. A sheet with no file is the checker, not a stand-in. */
static void test_skins_are_numbered_by_their_line(void)
{
    /* Longer than the line the loader reads at once, too. */
    char longname[200];
    memset(longname, 'x', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    char list[256];
    snprintf(list, sizeof(list), "alpha\r\n\n%s\nbeta  \n", longname);

    make_dir(SKIN_DIR);
    make_dir(SKIN_DIR "/beta");
    write_list(list);
    TEST_ASSERT_EQUAL_INT(0, write_sheet(SKIN_DIR "/beta/run_64x64.png", 256, 64));

    TEST_ASSERT_EQUAL_INT(0, assets_init(&assets));
    assets_load_skins(&assets, SKIN_DIR);
    TEST_ASSERT_EQUAL_INT(3, assets.skin_count);
    TEST_ASSERT_EQUAL_INT(0, assets_skin_index(&assets, "alpha"));
    TEST_ASSERT_EQUAL_INT(2, assets_skin_index(&assets, "beta"));
    TEST_ASSERT_EQUAL_INT(-1, assets_skin_index(&assets, longname));
    TEST_ASSERT_EQUAL_INT(-1, assets_skin_index(&assets, ""));
    TEST_ASSERT_EQUAL_INT(-1, assets_skin_index(&assets, "gamma"));

    TEST_ASSERT_EQUAL_INT(4, assets.skin_anim[2][ANIM_RUN].cols);
    assert_checker(&assets.skin_anim[2][ANIM_IDLE]);
    assert_checker(&assets.skin_anim[0][ANIM_RUN]);
    assets_shutdown(&assets);

    remove(SKIN_DIR "/beta/run_64x64.png");
    remove(SKIN_DIR "/skins.txt");
    remove_dir(SKIN_DIR "/beta");
    remove_dir(SKIN_DIR);
}

/* Past MAX_SKINS the list is cut off rather than overrunning the table. */
static void test_skin_list_stops_at_max_skins(void)
{
    char list[256] = "";
    for (int i = 0; i < MAX_SKINS + 2; i++) {
        char line[16];
        snprintf(line, sizeof(line), "s%d\n", i);
        strcat(list, line);
    }
    make_dir(SKIN_DIR);
    write_list(list);

    TEST_ASSERT_EQUAL_INT(0, assets_init(&assets));
    assets_load_skins(&assets, SKIN_DIR);
    TEST_ASSERT_EQUAL_INT(MAX_SKINS, assets.skin_count);
    TEST_ASSERT_EQUAL_INT(MAX_SKINS - 1, assets_skin_index(&assets, "s9"));
    TEST_ASSERT_EQUAL_INT(-1, assets_skin_index(&assets, "s10"));
    assets_shutdown(&assets);

    remove(SKIN_DIR "/skins.txt");
    remove_dir(SKIN_DIR);
}

/* ---- Single-frame textures ---- */

static void test_texture_alloc_declares_one_frame(void)
{
    TEST_ASSERT_EQUAL_INT(0, texture_alloc(&tex, 128, 64));
    TEST_ASSERT_EQUAL_INT(128, tex.fw);
    TEST_ASSERT_EQUAL_INT(64, tex.fh);
    TEST_ASSERT_EQUAL_INT(1, tex.cols);
    TEST_ASSERT_EQUAL_INT(1, tex.rows);
    /* Out-of-range frame numbers fall back to frame 0 rather than reading
     * past the image: sprite.c and hud.c pass one straight through. */
    TEST_ASSERT_EQUAL_PTR(tex.pixels, texture_frame(&tex, 3, 0));
    TEST_ASSERT_EQUAL_PTR(tex.pixels, texture_frame(&tex, -1, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_frame_size_reads_suffix);
    RUN_TEST(test_parse_frame_size_rejects_names_without_one);
    RUN_TEST(test_sheet_splits_into_frames);
    RUN_TEST(test_sheet_keys_out_magenta_and_hardens_alpha);
    RUN_TEST(test_sheet_rejects_size_that_is_not_whole_frames);
    RUN_TEST(test_sheet_rejects_suffix_the_code_did_not_ask_for);
    RUN_TEST(test_rejected_sheet_leaves_the_slot_alone);
    RUN_TEST(test_without_the_skin_list_every_character_is_the_checker);
    RUN_TEST(test_skins_are_numbered_by_their_line);
    RUN_TEST(test_skin_list_stops_at_max_skins);
    RUN_TEST(test_texture_alloc_declares_one_frame);
    return UNITY_END();
}
