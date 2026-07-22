#ifndef HUD_H
#define HUD_H

#include "engine.h"
#include "player.h"
#include "weapon.h"

void hud_draw(Framebuffer *fb, const Player *pl, const WeaponSystem *ws);

/* Weapon sprite at the bottom-center, with recoil offset from anim. */
void hud_draw_weapon(Framebuffer *fb, const Texture *base, float anim);

#endif