#ifndef AUDIO_H
#define AUDIO_H

#include "event.h"
#include <SDL.h>
#include <SDL_mixer.h>

struct World;

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
    int available; /* 1 if SDL_mixer initialized ok */
    int muted;
    Mix_Chunk *chunks[SND_COUNT];
    Mix_Music *music;
} Audio;

int audio_init(Audio *a);
void audio_shutdown(Audio *a);

/* Play at fixed volume (0..1). Each sound gets the volume of its own channel,
 * so one played quieter does not quieten the same sound already playing. A
 * sound with every channel busy is dropped. */
void audio_play_volume(Audio *a, SoundId id, float vol);

/* The sound `e` makes in world `w` and how loud it reaches w's local player,
 * 0..1: their own at its level wherever it happened, the rest fading out with
 * distance (event_gain()). 0 is not heard, and *id is then left alone. A shot
 * sounds like the weapon its shooter holds now. */
float audio_event_sound(const struct World *w, const GameEvent *e, SoundId *id);

/* Play what w->events holds as w's local player hears it: the last tick's
 * events, or those of every snapshot a network client's last poll applied.
 * What is not heard takes no channel. */
void audio_play_events(Audio *a, const struct World *w);

void audio_toggle_mute(Audio *a);

#endif
