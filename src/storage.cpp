#include "storage.h"
#include <Preferences.h>
#include "config.h"

namespace {
Preferences prefs;
Owned items[MAX_COLLECTION];
size_t count = 0;
uint32_t scans = 0;

void save() { prefs.putBytes("col", items, count * sizeof(Owned)); }
}  // namespace

void storageBegin() {
  prefs.begin("bbuddies", false);
  size_t len = prefs.getBytesLength("col");
  count = min(len / sizeof(Owned), (size_t)MAX_COLLECTION);
  if (count) prefs.getBytes("col", items, count * sizeof(Owned));
  scans = prefs.getUInt("scans", 0);
}

size_t collectionSize() { return count; }
Owned collectionAt(size_t i) { return items[i]; }

int findOwned(uint32_t seed, bool night) {
  for (size_t i = 0; i < count; i++)
    if (items[i].seed == seed && ownedNight(items[i]) == night) return (int)i;
  return -1;
}

bool addOwned(const Owned& o) {
  if (count >= MAX_COLLECTION) return false;
  items[count++] = o;
  save();
  return true;
}

void updateOwned(size_t i, const Owned& o) { if (i < count) { items[i] = o; save(); } }

void removeOwned(size_t i) {
  if (i >= count) return;
  for (size_t j = i; j + 1 < count; j++) items[j] = items[j + 1];
  count--;
  save();
}

uint32_t totalScans() { return scans; }
void countScan() { prefs.putUInt("scans", ++scans); }
bool loadSoundSetting() { return prefs.getBool("sound", true); }
void saveSoundSetting(bool on) { prefs.putBool("sound", on); }
