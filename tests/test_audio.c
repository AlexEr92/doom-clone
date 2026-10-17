/* audio.c: the synthesised chunks last as long as they were synthesised, and
 * a tick's events play as the local player hears them.
 *
 * The sounds are generated as mono 22050 Hz WAVs, but the device is opened
 * stereo, and in future at another rate: a chunk is only right if the mixer
 * converted it to the device format. Its length in the device format is the
 * one thing that shows whether it did, so what is checked is the duration,
 * computed from Mix_QuerySpec() rather than from a byte count fixed here.
 *
 * Which sound an event makes and how loud needs no mixer. What it plays on
 * does: a channel of its own at that volume, and none for what is not heard.
 *
 * Timbre is not checked — that is done by ear. The dummy driver opens the
 * mixer without a sound card; where even that fails the tests that need it
 * are skipped. */

/* SDL.h would otherwise rename main() on platforms that need SDL2main. */
#define SDL_MAIN_HANDLED
#include "unity.h"
#include "audio.h"
#include "world.h"
#include <SDL.h>
#include <string.h>

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

#define ME 0
#define OTHER 1
#define OTHER_DIST 10.0f /* how far the other player stands */

/* Two players on one row: this one with the pistol, the other OTHER_DIST to
 * the east with the shotgun. Nothing else of the world is needed. */
static World world;

void setUp(void)
{
    opened = audio_init(&audio) == 0;

    memset(&world, 0, sizeof(world));
    world.local_player = ME;
    world.player_count = 2;
    for (int i = 0; i < 2; i++) {
        PlayerState *p = &world.players[i];
        p->id = (uint8_t)i;
        p->active = 1;
        p->alive = 1;
        p->x = 5.0f + (float)i * OTHER_DIST;
        p->y = 5.0f;
    }
    world.players[ME].weapons.current = WEAPON_PISTOL;
    world.players[OTHER].weapons.current = WEAPON_SHOTGUN;
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

/* ---- what an event sounds like ---- */

/* The sound of an event of `kind` by `actor` at (x, y), and its volume; *id
 * is SND_COUNT when nothing is heard. */
static float sound_of(GameEventKind kind, int actor, float x, float y, SoundId *id)
{
    GameEvent e = {(uint8_t)kind, (uint8_t)actor, x, y};
    *id = SND_COUNT;
    return audio_event_sound(&world, &e, id);
}

/* A player's own shot, pickup, click and pain play at the level of each,
 * however far from them the event was posted. */
static void test_own_events_play_at_their_level_from_anywhere(void)
{
    SoundId id;
    TEST_ASSERT_EQUAL_FLOAT(0.5f, sound_of(EV_SHOT, ME, 5.0f, 5.0f, &id));
    TEST_ASSERT_EQUAL_INT(SND_PISTOL, id);
    TEST_ASSERT_EQUAL_FLOAT(0.8f, sound_of(EV_PICKUP, ME, 6.0f, 5.0f, &id));
    TEST_ASSERT_EQUAL_INT(SND_PICKUP, id);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, sound_of(EV_NO_AMMO, ME, 5.0f, 5.0f, &id));
    TEST_ASSERT_EQUAL_INT(SND_NO_AMMO, id);
    /* pain is posted where the hit came from */
    TEST_ASSERT_EQUAL_FLOAT(1.0f, sound_of(EV_PLAYER_HURT, ME, 35.0f, 5.0f, &id));
    TEST_ASSERT_EQUAL_INT(SND_PLAYER_HURT, id);
}

/* Another player's shot sounds like the weapon in their hands, at most as
 * loud as one's own, and fades out with distance. */
static void test_another_players_shot_fades_with_distance(void)
{
    SoundId id;
    float x = 5.0f + OTHER_DIST;
    float fade = 1.0f - OTHER_DIST / event_range(EV_SHOT);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f * fade, sound_of(EV_SHOT, OTHER, x, 5.0f, &id));
    TEST_ASSERT_EQUAL_INT(SND_SHOTGUN, id);

    x = 5.0f + event_range(EV_SHOT);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sound_of(EV_SHOT, OTHER, x, 5.0f, &id));
    TEST_ASSERT_EQUAL_INT(SND_COUNT, id);
}

static void test_another_players_pickup_and_click_are_not_heard(void)
{
    SoundId id;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sound_of(EV_PICKUP, OTHER, 5.0f, 5.0f, &id));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sound_of(EV_NO_AMMO, OTHER, 5.0f, 5.0f, &id));
    TEST_ASSERT_EQUAL_INT(SND_COUNT, id);
}

/* Half the range away is half as loud: enemies, doors and others' pain. */
static void test_what_happens_elsewhere_fades_with_distance(void)
{
    const struct {
        GameEventKind kind;
        SoundId id;
    } cases[] = {
            {EV_ENEMY_HURT, SND_ENEMY_HURT},
            {EV_ENEMY_DEATH, SND_ENEMY_DEATH},
            {EV_DOOR, SND_DOOR},
            {EV_PLAYER_HURT, SND_PLAYER_HURT},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        SoundId id;
        float y = 5.0f + event_range(cases[i].kind) * 0.5f;
        TEST_ASSERT_EQUAL_FLOAT(0.5f, sound_of(cases[i].kind, OTHER, 5.0f, y, &id));
        TEST_ASSERT_EQUAL_INT(cases[i].id, id);
    }
}

/* A shooter the world does not hold sounds like the pistol, and a kind of
 * event there is no sound for is silence. */
static void test_what_the_world_cannot_place_is_played_safely(void)
{
    SoundId id;
    TEST_ASSERT_EQUAL_FLOAT(0.5f, sound_of(EV_SHOT, MAX_PLAYERS, 5.0f, 5.0f, &id));
    TEST_ASSERT_EQUAL_INT(SND_PISTOL, id);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sound_of(EV_COUNT, ME, 5.0f, 5.0f, &id));
    TEST_ASSERT_EQUAL_INT(SND_COUNT, id);
}

static void test_a_world_without_a_local_player_hears_nothing(void)
{
    SoundId id;
    world.local_player = -1;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sound_of(EV_ENEMY_DEATH, 3, 5.0f, 5.0f, &id));
}

/* ---- what it plays on ---- */

static int channels_playing(void)
{
    return Mix_Playing(-1);
}

/* The volumes of the channels playing, highest first, in `vol`; how many. */
static int playing_volumes(int vol[], int max)
{
    int n = 0;
    for (int ch = 0; ch < Mix_AllocateChannels(-1) && n < max; ch++) {
        if (Mix_Playing(ch)) {
            vol[n++] = Mix_Volume(ch, -1);
        }
    }
    if (n == 2 && vol[0] < vol[1]) {
        int t = vol[0];
        vol[0] = vol[1];
        vol[1] = t;
    }
    return n;
}

static void test_ten_players_firing_have_channels_to_spare(void)
{
    skip_without_mixer();
    TEST_ASSERT_EQUAL_INT(32, Mix_AllocateChannels(-1));
}

/* One's own pistol shot and another's, farther off, in the same frame: each
 * keeps its own volume, and the chunk they share is not turned down. */
static void test_each_sound_plays_on_a_channel_at_its_own_volume(void)
{
    skip_without_mixer();
    world.players[OTHER].weapons.current = WEAPON_PISTOL;
    event_push(&world.events, EV_SHOT, ME, 5.0f, 5.0f);
    event_push(&world.events, EV_SHOT, OTHER, 5.0f + OTHER_DIST, 5.0f);

    audio_play_events(&audio, &world);

    int vol[2];
    float fade = 1.0f - OTHER_DIST / event_range(EV_SHOT);
    TEST_ASSERT_EQUAL_INT(2, playing_volumes(vol, 2));
    TEST_ASSERT_EQUAL_INT((int)(0.5f * MIX_MAX_VOLUME), vol[0]);
    TEST_ASSERT_EQUAL_INT((int)(0.5f * fade * MIX_MAX_VOLUME), vol[1]);
    TEST_ASSERT_EQUAL_INT(MIX_MAX_VOLUME, Mix_VolumeChunk(audio.chunks[SND_PISTOL], -1));
}

static void test_a_volume_out_of_range_is_held_to_it(void)
{
    skip_without_mixer();
    int vol[2];
    audio_play_volume(&audio, SND_ENEMY_DEATH, 2.0f);
    audio_play_volume(&audio, SND_ENEMY_DEATH, -1.0f);
    TEST_ASSERT_EQUAL_INT(2, playing_volumes(vol, 2));
    TEST_ASSERT_EQUAL_INT(MIX_MAX_VOLUME, vol[0]);
    TEST_ASSERT_EQUAL_INT(0, vol[1]);
}

/* With every channel taken the next sound is dropped: none playing is cut
 * short for it. */
static void test_a_sound_with_every_channel_busy_is_dropped(void)
{
    skip_without_mixer();
    int channels = Mix_AllocateChannels(-1);
    for (int i = 0; i <= channels; i++) {
        audio_play_volume(&audio, SND_ENEMY_DEATH, 1.0f);
    }
    TEST_ASSERT_EQUAL_INT(channels, channels_playing());
}

static void test_what_is_not_heard_takes_no_channel(void)
{
    skip_without_mixer();
    event_push(&world.events, EV_PICKUP, OTHER, 5.0f, 5.0f);
    event_push(&world.events, EV_DOOR, 0, 5.0f + event_range(EV_DOOR), 5.0f);

    audio_play_events(&audio, &world);

    TEST_ASSERT_EQUAL_INT(0, channels_playing());
}

static void test_muting_cuts_off_what_plays_and_plays_nothing_new(void)
{
    skip_without_mixer();
    event_push(&world.events, EV_ENEMY_DEATH, 0, 5.0f, 5.0f);
    audio_play_events(&audio, &world);
    TEST_ASSERT_EQUAL_INT(1, channels_playing());

    audio_toggle_mute(&audio);
    TEST_ASSERT_EQUAL_INT(0, channels_playing());
    audio_play_events(&audio, &world);
    TEST_ASSERT_EQUAL_INT(0, channels_playing());

    audio_toggle_mute(&audio);
    audio_play_events(&audio, &world);
    TEST_ASSERT_EQUAL_INT(1, channels_playing());
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
    RUN_TEST(test_own_events_play_at_their_level_from_anywhere);
    RUN_TEST(test_another_players_shot_fades_with_distance);
    RUN_TEST(test_another_players_pickup_and_click_are_not_heard);
    RUN_TEST(test_what_happens_elsewhere_fades_with_distance);
    RUN_TEST(test_what_the_world_cannot_place_is_played_safely);
    RUN_TEST(test_a_world_without_a_local_player_hears_nothing);
    RUN_TEST(test_ten_players_firing_have_channels_to_spare);
    RUN_TEST(test_each_sound_plays_on_a_channel_at_its_own_volume);
    RUN_TEST(test_a_volume_out_of_range_is_held_to_it);
    RUN_TEST(test_a_sound_with_every_channel_busy_is_dropped);
    RUN_TEST(test_what_is_not_heard_takes_no_channel);
    RUN_TEST(test_muting_cuts_off_what_plays_and_plays_nothing_new);
    int failures = UNITY_END();
    SDL_Quit();
    return failures;
}
