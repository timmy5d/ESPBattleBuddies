// BattleBuddies - the saved collection (kept in flash via Preferences/NVS).
#pragma once
#include <Arduino.h>

struct Owned {
  uint32_t seed;
  uint8_t  flags;   // bit0 night form, bits1-2 where it was found (Source)
  uint8_t  level;
  uint16_t xp;
};

void     storageBegin();
size_t   collectionSize();
Owned    collectionAt(size_t i);
int      findOwned(uint32_t seed, bool night);   // -1 if not owned
bool     addOwned(const Owned& o);               // false if full
void     updateOwned(size_t i, const Owned& o);
void     removeOwned(size_t i);
uint32_t totalScans();
void     countScan();
bool     loadSoundSetting();
void     saveSoundSetting(bool on);

inline bool ownedNight(const Owned& o) { return o.flags & 1; }
inline uint8_t ownedSource(const Owned& o) { return (o.flags >> 1) & 3; }
// XP needed to go from `level` to the next one.
inline uint16_t xpForNext(uint8_t level) { return 20 + level * 10; }
