// Grand Theft Otto II - HUD and bottom screen (citro2d; the PC preview compiles it against host/c2d_shim.h)
#pragma once
#include "game.h"
#include "scene.h"

void hud_init(void);
void hud_exit(void);
void hud_frame_begin(void);                                // once per frame: reset the text buffer
void hud_top(const Game *g, const Scene *s);               // top screen overlay (drawn once per eye)
void hud_bottom(const Game *g);                            // touch screen: map, controls
