#ifndef HUD_H
#define HUD_H

#include "engine.h"
#include "assets.h"
#include "player.h"
#include "weapon.h"

void hud_draw(Framebuffer *fb, const PlayerState *pl);

/* Weapon sprite at the bottom-center, in the frame and with the recoil offset
 * that Weapon.anim gives. */
void hud_draw_weapon(Framebuffer *fb, const Texture *base, float anim);

/* Frame of a weapon sheet `cols` frames long for Weapon.anim: frame 0 at rest,
 * the shot spread over the frames from the first to the last while anim runs
 * down from 1. */
int hud_weapon_frame(int cols, float anim);

#endif
