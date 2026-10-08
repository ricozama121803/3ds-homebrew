// Grand Theft Otto - drawing (citro2d textured tiles/sprites)
#pragma once
#include "game.h"

void render_init(void);
void render_exit(void);
void render_prepare(const Game *g);                          // once per frame: builds text
void render_top(const Game *g, float eye, float slider);     // eye: -1 left, +1 right; slider: 0..1
void render_top_hud(const Game *g, float eye, float slider);  // health, wanted stars, banners: drawn after render_top or after the 3D scene
void render_bottom(const Game *g);
