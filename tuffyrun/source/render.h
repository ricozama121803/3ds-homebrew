// Tuffy Run - drawing (citro2d primitives only, no image assets)
#pragma once
#include <stdint.h>
#include "game.h"

void render_init(void);
void render_exit(void);
void render_prepare(const Game *g);                          // once per frame: builds text
void render_top(const Game *g, float eye, float slider);     // eye: -1 left, +1 right; slider: 0..1
void render_bottom(const Game *g);
uint32_t render_haze(const Game *g);                             // colour to clear the top screen with (matches the distant haze)
