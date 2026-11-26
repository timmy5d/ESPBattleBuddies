// BattleBuddies - procedural monster drawing.
#pragma once
#include <TFT_eSPI.h>
#include "monster.h"

// Draws a monster centred on (cx, cy), about `size` pixels tall.
// Works on the screen or on a TFT_eSprite (sprites are TFT_eSPI too).
// `blink` closes the eyes, for idle animation.
void drawMonster(TFT_eSPI& g, const Monster& m, int cx, int cy, int size, bool blink = false);
