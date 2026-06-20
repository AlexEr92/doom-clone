#ifndef RAYCAST_H
#define RAYCAST_H

#include "engine.h"
#include "player.h"
#include "map.h"
#include "assets.h"

extern float zBuffer[SCREEN_W];

void raycast_render(Framebuffer *fb, const Player *p, const Map *m, const Assets *a);

#endif