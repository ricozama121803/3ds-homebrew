// Heads-up display: health/stamina bars, timer and banners on the top screen; a live control chart on the bottom screen.
#pragma once
#include "game.h"

void hud_top(const Game *g);                                        // 400x240 overlay drawn over the 3D view
void hud_bottom(const Game *g, const FighterInput *in, int paused); // 320x240 touch-screen panel
