// BattleBuddies - touch, sound, LED and light sensor.
#pragma once
#include <Arduino.h>

void hwBegin();

// Touch in screen coordinates (portrait, 240x320).
bool touchDown(int& x, int& y);     // finger is on the screen now
bool touchTapped(int& x, int& y);   // true once per new press

// Sound: notes play in the background so the UI keeps animating.
struct Note { uint16_t freq; uint16_t ms; };   // freq 0 = rest
void playNotes(const Note* notes, size_t count);
void setSoundOn(bool on);
bool soundOn();
void sfxClick();
void sfxScanPing();
void sfxFound();
void sfxThrow();
void sfxCaught();
void sfxMiss();
void sfxFled();

// RGB LED (each channel just on or off).
void ledColor(bool r, bool g, bool b);
void ledOff();

// True when the light sensor says it's dark.
bool isDark();
int  lightReading();
