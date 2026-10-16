/* audio.c: the synthesised chunks last as long as they were synthesised.
 *
 * The sounds are generated as mono 22050 Hz WAVs, but the device is opened
 * stereo, and in future at another rate: a chunk is only right if the mixer
 * converted it to the device format. Its length in the device format is the
 * one thing that shows whether it did, so what is checked is the duration,
 * computed from Mix_QuerySpec() rather than from a byte count fixed here.
 *
 * Timbre is not checked — that is done by ear. The dummy driver opens the
 * mixer without a sound card; where even that fails the tests are skipped. */

/* SDL.h would otherwise rename main() on platforms that need SDL2main. */
#define SDL_MAIN_HANDLED
#include "unity.h"
#include "audio.h"
#include <SDL.h>

/* The lengths audio_init() synthesises, in seconds. */
static const struct {
    SoundId id;
    const char *name;
    float seconds;
} LENGTHS[] = {
        {SND_PISTOL, "SND_PISTOL", 0.18f},
        {SND_SHOTGUN, "SND_SHOTGUN", 0.35f},
        {SND_ENEMY_HURT, "SND_ENEMY_HURT", 0.20f},
        {SND_ENEMY_DEATH, "SND_ENEMY_DEATH", 0.55f},
        {SND_PLAYER_HURT, "SND_PLAYER_HURT", 0.25f},
        {SND_PICKUP, "SND_PICKUP", 0.22f},
        {SND_DOOR, "SND_DOOR", 0.30f},
        {SND_NO_AMMO, "SND_NO_AMMO", 0.08f},
};

static Audio audio;
static int opened;

void setUp(void)
{
    opened = audio_init(&audio) == 0;
}

void tearDown(void)
{
    if (opened) {
        audio_shutdown(&audio);
    }
}

static void skip_without_mixer(void)
{
    if (!opened) {
        TEST_IGNORE_MESSAGE("SDL_mixer could not open the dummy driver");
    }
}

/* Row i describes SoundId i: a count alone would let a duplicated row hide
 * a sound the other tests then never look at. */
static void test_every_sound_has_a_length(void)
{
    TEST_ASSERT_EQUAL_INT(SND_COUNT, (int)(sizeof(LENGTHS) / sizeof(LENGTHS[0])));
    for (int i = 0; i < SND_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(i, LENGTHS[i].id, LENGTHS[i].name);
    }
}

static void test_every_sound_is_synthesised(void)
{
    skip_without_mixer();
    for (int i = 0; i < SND_COUNT; i++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(audio.chunks[LENGTHS[i].id], LENGTHS[i].name);
    }
}

static void test_every_sound_lasts_its_synthesis_length(void)
{
    skip_without_mixer();
    int freq = 0;
    int channels = 0;
    Uint16 format = 0;
    TEST_ASSERT_NOT_EQUAL(0, Mix_QuerySpec(&freq, &format, &channels));
    float frame_bytes = (float)(channels * (SDL_AUDIO_BITSIZE(format) / 8));

    for (int i = 0; i < SND_COUNT; i++) {
        const Mix_Chunk *c = audio.chunks[LENGTHS[i].id];
        TEST_ASSERT_NOT_NULL_MESSAGE(c, LENGTHS[i].name);
        float seconds = (float)c->alen / ((float)freq * frame_bytes);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(LENGTHS[i].seconds * 0.01f, LENGTHS[i].seconds, seconds,
                                         LENGTHS[i].name);
    }
}

int main(void)
{
    /* Overwrite, not default: the length check needs no sound card, and a
     * real one is not something a test run should make noise on. */
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);

    UNITY_BEGIN();
    RUN_TEST(test_every_sound_has_a_length);
    RUN_TEST(test_every_sound_is_synthesised);
    RUN_TEST(test_every_sound_lasts_its_synthesis_length);
    int failures = UNITY_END();
    SDL_Quit();
    return failures;
}
