#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Procedural WAV chunk synthesis ----
 * We build a 16-bit mono 22050 Hz WAV in memory, then load it with
 * Mix_QuickLoad_WAV. This avoids shipping external sound files. */

static void put_u32(unsigned char *p, Uint32 v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}
static void put_u16(unsigned char *p, Uint16 v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

/* Build a WAV byte buffer from 16-bit PCM samples. Caller frees the buffer. */
static unsigned char *build_wav(const Sint16 *samples, Uint32 n, Uint32 *out_len) {
    Uint32 data_len = n * 2u;
    Uint32 total = 44u + data_len;
    unsigned char *buf = (unsigned char *)malloc(total);
    if (!buf) return NULL;
    memcpy(buf, "RIFF", 4);
    put_u32(buf + 4, total - 8);
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    put_u32(buf + 16, 16);
    put_u16(buf + 20, 1);            /* PCM */
    put_u16(buf + 22, 1);            /* mono */
    put_u32(buf + 24, 22050);        /* sample rate */
    put_u32(buf + 28, 22050 * 2);    /* byte rate */
    put_u16(buf + 32, 2);            /* block align */
    put_u16(buf + 34, 16);           /* bits per sample */
    memcpy(buf + 36, "data", 4);
    put_u32(buf + 40, data_len);
    for (Uint32 i = 0; i < n; i++) {
        put_u16(buf + 44 + i * 2, (Uint16)samples[i]);
    }
    *out_len = total;
    return buf;
}

static Sint16 *alloc_samples(Uint32 n) {
    return (Sint16 *)malloc(n * sizeof(Sint16));
}

/* Enveloped noise burst (good for gunshots / impacts). */
static Sint16 *gen_noise(Uint32 n, float attack, float decay, int lp) {
    Sint16 *s = alloc_samples(n);
    if (!s) return NULL;
    float prev = 0.0f;
    for (Uint32 i = 0; i < n; i++) {
        float t = (float)i / (float)n;
        float env;
        if (t < attack) env = t / attack;
        else            env = expf(-(t - attack) * decay);
        float w = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        if (lp) { /* simple one-pole low-pass for body */
            prev = prev + 0.35f * (w - prev);
            w = prev;
        }
        s[i] = (Sint16)(w * env * 30000.0f);
    }
    return s;
}

/* Sine-ish blip with pitch drop (good for pickup / door). */
static Sint16 *gen_blip(Uint32 n, float f0, float f1, float decay) {
    Sint16 *s = alloc_samples(n);
    if (!s) return NULL;
    float ph = 0.0f;
    for (Uint32 i = 0; i < n; i++) {
        float t = (float)i / 22050.0f;
        float f = f0 + (f1 - f0) * t * decay;
        float env = expf(-t * decay);
        ph += f / 22050.0f * 2.0f * (float)M_PI;
        s[i] = (Sint16)(sinf(ph) * env * 26000.0f);
    }
    return s;
}

/* Low growl for enemy hurt/death (mixed saw + noise). */
static Sint16 *gen_growl(Uint32 n, float f0, float f1, float noise_mix) {
    Sint16 *s = alloc_samples(n);
    if (!s) return NULL;
    float ph = 0.0f;
    for (Uint32 i = 0; i < n; i++) {
        float t = (float)i / (float)n;
        float f = f0 + (f1 - f0) * t;
        ph += f / 22050.0f * 2.0f * (float)M_PI;
        float saw = 2.0f * (ph / (2.0f * (float)M_PI) - floorf(ph / (2.0f * (float)M_PI))) - 1.0f;
        float nz = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        float env = expf(-t * 4.0f);
        float w = (saw * (1.0f - noise_mix) + nz * noise_mix) * env;
        s[i] = (Sint16)(w * 22000.0f);
    }
    return s;
}

static Mix_Chunk *make_chunk(Sint16 *samples, Uint32 n) {
    if (!samples) return NULL;
    Uint32 len = 0;
    unsigned char *wav = build_wav(samples, n, &len);
    free(samples);
    if (!wav) return NULL;
    Mix_Chunk *c = Mix_QuickLoad_WAV(wav);
    /* Mix_QuickLoad_WAV copies? No — it does NOT copy the buffer; it keeps the
     * pointer. We must keep the WAV buffer alive for the chunk's lifetime.
     * Store the pointer in chunk->abuf (which IS the pointer) — that's fine,
     * but Mix_FreeChunk will free(abuf). So we must hand ownership over. */
    return c;
}

int audio_init(Audio *a) {
    memset(a, 0, sizeof(*a));
    if (Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 512) != 0) {
        fprintf(stderr, "audio: Mix_OpenAudio failed: %s\n", Mix_GetError());
        return -1;
    }
    Mix_AllocateChannels(16);
    a->available = 1;

    /* Synthesize each sound. */
    {
        /* pistol: short click + noise tail */
        Uint32 n = 22050 * 0.18f;
        Sint16 *s = alloc_samples(n);
        if (s) {
            for (Uint32 i = 0; i < n; i++) {
                float t = (float)i / (float)n;
                float env = expf(-t * 18.0f);
                float click = (i < 220) ? (((float)rand() / (float)RAND_MAX) * 2 - 1) * 0.9f : 0.0f;
                float nz = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
                float w = (click + nz * 0.7f) * env;
                s[i] = (Sint16)(w * 22000.0f);
            }
            a->chunks[SND_PISTOL] = make_chunk(s, n);
        }
        /* shotgun: longer noise burst, low-passed body */
        n = 22050 * 0.35f;
        a->chunks[SND_SHOTGUN] = make_chunk(gen_noise(n, 0.02f, 14.0f, 1), n);

        /* enemy hurt: short mid growl */
        n = 22050 * 0.20f;
        a->chunks[SND_ENEMY_HURT] = make_chunk(gen_growl(n, 320.0f, 180.0f, 0.3f), n);
        /* enemy death: longer descending growl */
        n = 22050 * 0.55f;
        a->chunks[SND_ENEMY_DEATH] = make_chunk(gen_growl(n, 280.0f, 80.0f, 0.45f), n);
        /* player hurt: low thump */
        n = 22050 * 0.25f;
        a->chunks[SND_PLAYER_HURT] = make_chunk(gen_growl(n, 160.0f, 60.0f, 0.25f), n);
        /* pickup: rising blip */
        n = 22050 * 0.22f;
        a->chunks[SND_PICKUP] = make_chunk(gen_blip(n, 440.0f, 880.0f, 6.0f), n);
        /* door: mechanical thunk */
        n = 22050 * 0.30f;
        a->chunks[SND_DOOR] = make_chunk(gen_growl(n, 90.0f, 40.0f, 0.5f), n);
        /* no ammo: dry click */
        n = 22050 * 0.08f;
        Sint16 *s2 = alloc_samples(n);
        if (s2) {
            for (Uint32 i = 0; i < n; i++) {
                float t = (float)i / (float)n;
                float env = expf(-t * 40.0f);
                float w = (((float)rand() / (float)RAND_MAX) * 2 - 1) * env;
                s2[i] = (Sint16)(w * 12000.0f);
            }
            a->chunks[SND_NO_AMMO] = make_chunk(s2, n);
        }
    }
    return 0;
}

void audio_shutdown(Audio *a) {
    if (!a->available) return;
    for (int i = 0; i < SND_COUNT; i++) {
        if (a->chunks[i]) Mix_FreeChunk(a->chunks[i]);
        a->chunks[i] = NULL;
    }
    if (a->music) { Mix_FreeMusic(a->music); a->music = NULL; }
    Mix_CloseAudio();
    a->available = 0;
}

void audio_play(Audio *a, SoundId id, float dist, float maxdist) {
    if (!a->available || a->muted) return;
    if (id < 0 || id >= SND_COUNT) return;
    Mix_Chunk *c = a->chunks[id];
    if (!c) return;
    float vol = 1.0f;
    if (maxdist > 0.0f) {
        vol = 1.0f - (dist / maxdist);
        if (vol < 0.0f) vol = 0.0f;
        if (vol > 1.0f) vol = 1.0f;
    }
    int mix_vol = (int)(vol * 128.0f);
    Mix_VolumeChunk(c, mix_vol);
    /* stereo pan by sign of dist? We don't carry direction here; keep center. */
    Mix_PlayChannel(-1, c, 0);
}

void audio_play_volume(Audio *a, SoundId id, float vol) {
    if (!a->available || a->muted) return;
    if (id < 0 || id >= SND_COUNT) return;
    Mix_Chunk *c = a->chunks[id];
    if (!c) return;
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    Mix_VolumeChunk(c, (int)(vol * 128.0f));
    Mix_PlayChannel(-1, c, 0);
}

void audio_toggle_mute(Audio *a) {
    a->muted = !a->muted;
    if (a->available) Mix_Volume(-1, a->muted ? 0 : MIX_MAX_VOLUME);
}