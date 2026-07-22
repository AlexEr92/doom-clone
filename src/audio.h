#ifndef AUDIO_H
#define AUDIO_H

#include <SDL.h>
#include <SDL_mixer.h>

typedef enum {
    SND_PISTOL = 0,
    SND_SHOTGUN,
    SND_ENEMY_HURT,
    SND_ENEMY_DEATH,
    SND_PLAYER_HURT,
    SND_PICKUP,
    SND_DOOR,
    SND_NO_AMMO,
    SND_COUNT
} SoundId;

typedef struct Audio {
    int available;        /* 1 if SDL_mixer initialized ok */
    int muted;
    Mix_Chunk *chunks[SND_COUNT];
    Mix_Music *music;
} Audio;

int  audio_init(Audio *a);
void audio_shutdown(Audio *a);

/* Play a sound with optional 3D position relative to player.
 * dist == 0 => full volume; falls off linearly to 0 at maxdist. */
void audio_play(Audio *a, SoundId id, float dist, float maxdist);

/* Play at fixed volume (0..1). */
void audio_play_volume(Audio *a, SoundId id, float vol);

void audio_toggle_mute(Audio *a);

#endif