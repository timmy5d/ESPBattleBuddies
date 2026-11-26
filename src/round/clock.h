// BattleBuddies - the round board's real-time clock (PCF85063).
// Stands in for the CYD's light sensor: night forms appear when the clock
// says it's night (see NIGHT_FROM_HOUR / NIGHT_TO_HOUR in board.h).
#pragma once
#include <Arduino.h>

bool clockValid();                       // false until the time has been set (or after power loss)
bool clockNow(uint8_t& hour, uint8_t& minute, uint8_t& second);
void clockSet(uint8_t hour, uint8_t minute);
bool clockIsNight();                     // false while the clock isn't set
bool clockIsNightHour(uint8_t hour);
