#ifndef HUD_H
#define HUD_H

#include "engine.h"
#include "assets.h"
#include "player.h"
#include "weapon.h"

/* Height of the status bar along the bottom of the screen: the 48 rows of the
 * face and its frame. The weapon in hand stands on it. */
#define HUD_PANEL_H 52

/* How long the face grimaces after a hit, in seconds. */
#define HUD_PAIN_TIME 0.5f

/* Steps of health the face shows, 20 hp each, from full to nearly none. */
#define FACE_HEALTH_STEPS 5

/* The frames of assets/hud/face_48x48.png, left to right. */
typedef enum {
    FACE_HEALTH = 0,               /* the first of FACE_HEALTH_STEPS, at full health */
    FACE_PAIN = FACE_HEALTH_STEPS, /* a hit */
    FACE_DEAD,
    FACE_COUNT
} FaceFrame;

/* What the status bar remembers of the local player between frames, which is
 * how it tells a hit the way a character's animation does (sprite.h): hp
 * lower than the last frame saw it. The world keeps no record of damage. */
typedef struct {
    float hp;        /* as the last frame saw it */
    float pain_left; /* seconds of grimace the last drop in hp is still owed */
    int seen;        /* 0 until a frame has seen the player: nothing to compare */
} HudFace;

/* Forget the last player seen: called again when the world is reloaded. */
void hud_face_init(HudFace *f);

/* Advance the face by a frame of `dt` seconds, comparing pl with the last
 * frame. A dt of 0 holds the grimace where it is. */
void hud_face_update(HudFace *f, const PlayerState *pl, float dt);

/* FaceFrame to show for pl: dead, else a hit still owed, else the step of
 * health. */
int hud_face_frame(const HudFace *f, const PlayerState *pl);

/* The status bar along the bottom of the screen. What the files in `a`
 * provide is drawn from them, and the rest the way the code draws it. */
void hud_draw(Framebuffer *fb, const PlayerState *pl, const HudFace *face, const Assets *a);

/* Weapon sprite at the center, standing on the status bar, in the frame and
 * with the recoil offset that Weapon.anim gives. */
void hud_draw_weapon(Framebuffer *fb, const Texture *base, float anim);

/* Frame of a weapon sheet `cols` frames long for Weapon.anim: frame 0 at rest,
 * the shot spread over the frames from the first to the last while anim runs
 * down from 1. */
int hud_weapon_frame(int cols, float anim);

#endif
