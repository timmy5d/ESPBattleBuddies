// Host stand-ins for the board: display flush, touch, clock, scanner, saves.
#include <Arduino.h>
#include "display.h"
#include "hw.h"
#include "clock.h"
#include "scanner.h"
#include "storage.h"
#include "monster.h"
#include "config.h"

// ---- canvas (same dirty tracking as src/round/display.cpp)
void Canvas::mark(int32_t x, int32_t y, int32_t w, int32_t h) {
  if (w <= 0 || h <= 0) return;
  int32_t x1 = x + w - 1, y1 = y + h - 1;
  x = max(x, 0); y = max(y, 0); x1 = min(x1, (int32_t)width_ - 1); y1 = min(y1, (int32_t)height_ - 1);
  if (x > x1 || y > y1) return;
  dx0 = min((int32_t)dx0, x); dy0 = min((int32_t)dy0, y); dx1 = max((int32_t)dx1, x1); dy1 = max((int32_t)dy1, y1);
}
bool Canvas::takeDirty(int16_t& x0, int16_t& y0, int16_t& x1, int16_t& y1) {
  if (dx1 < dx0 || dy1 < dy0) return false;
  x0 = dx0; y0 = dy0; x1 = dx1; y1 = dy1; dx0 = dy0 = 32767; dx1 = dy1 = -1; return true;
}
void Canvas::clear(uint16_t c) { fillSprite(c); markAll(); }
void Canvas::drawPixel(int32_t x, int32_t y, uint32_t c) { mark(x, y, 1, 1); TFT_eSprite::drawPixel(x, y, c); }
void Canvas::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) { TFT_eSprite::drawLine(x0, y0, x1, y1, c); }
void Canvas::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) { TFT_eSprite::drawFastVLine(x, y, h, c); }
void Canvas::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) { TFT_eSprite::drawFastHLine(x, y, w, c); }
void Canvas::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) { TFT_eSprite::fillRect(x, y, w, h, c); }
void Canvas::drawChar(int32_t x, int32_t y, uint16_t ch, uint32_t c, uint32_t bg, uint8_t s) { TFT_eSprite::drawChar(x, y, ch, c, bg, s); }
int16_t Canvas::drawChar(uint16_t u, int32_t x, int32_t y, uint8_t f) { return TFT_eSprite::drawChar(u, x, y, f); }

static TFT_eSPI parentTft;
static Canvas cv(&parentTft);
static uint16_t ringColor = 0;
Canvas& canvas() { return cv; }
uint8_t displayPanelId() { return 0xFF; }
void displayRing(uint16_t c) { ringColor = c; }
bool displayBegin() { cv.createSprite(466, 466); cv.setSize(466, 466); return true; }
void displayFlush() {
  const float R = 233, rIn = R - 6;
  for (int y = 0; y < 466; y++) {
    float dy = y + 0.5f - R;
    int outer = (int)sqrtf(R * R - dy * dy);
    int inner = fabsf(dy) < rIn ? (int)sqrtf(rIn * rIn - dy * dy) : 0;
    cv.drawFastHLine(233 - outer, y, outer - inner, ringColor);
    cv.drawFastHLine(233 + inner, y, outer - inner, ringColor);
  }
  int16_t a, b, c, d; cv.takeDirty(a, b, c, d);
}

// ---- touch: the preview injects one tap at a time
int tapX = -1, tapY = -1;
bool touchDown(int& x, int& y) { if (tapX < 0) return false; x = tapX; y = tapY; return true; }
bool touchTapped(int& x, int& y) { bool d = touchDown(x, y); tapX = tapY = -1; return d; }
void hwBegin() {}
void playNotes(const Note*, size_t) {}
void setSoundOn(bool) {}
bool soundOn() { return true; }
void sfxClick() {} void sfxScanPing() {} void sfxFound() {} void sfxThrow() {} void sfxCaught() {} void sfxMiss() {} void sfxFled() {}
void ledColor(bool r, bool g, bool b) { displayRing(rgb565(r ? 235 : 40, g ? 210 : 40, b ? 240 : 40)); }
void ledOff() { displayRing(0); }
bool isDark() { return false; }
int lightReading() { return 0; }

// ---- clock
int fakeHour = 21, fakeMin = 4;
bool clockNow(uint8_t& h, uint8_t& m, uint8_t& s) { if (fakeHour < 0) return false; h = fakeHour; m = fakeMin; s = 0; return true; }
bool clockValid() { return fakeHour >= 0; }
void clockSet(uint8_t h, uint8_t m) { fakeHour = h; fakeMin = m; }
bool clockIsNightHour(uint8_t h) { return h >= 20 || h < 6; }
bool clockIsNight() { return fakeHour >= 0 && clockIsNightHour(fakeHour); }

// ---- scanner: a made-up neighbourhood
static const char* NAMES[] = {"KitchenRouter", "Neighbour-5G", "", "Pixel Watch", "CoffeeShopGuest", "TV-Living", "", "Printer_HP"};
static const int8_t RSSI[] = {-38, -52, -61, -66, -70, -77, -84, -90};
ScanPhase scanPhase = SCAN_WIFI;
void scannerBegin() {}
void scannerStart(ScanMode) {}
ScanPhase scannerPoll() { return scanPhase; }
int scannerSignalCount() { return 8; }
ScanSignal scannerSignal(int i) {
  ScanSignal s{};
  s.seed = 0x51A7u * (i + 3) ^ 0xBEEF1234u;
  s.source = i == 3 ? SRC_BLE : SRC_WIFI;
  s.rssi = RSSI[i];
  strlcpy(s.name, NAMES[i], sizeof(s.name));
  return s;
}
ScanResult scannerResult() { ScanResult r{}; r.found = true; r.seed = scannerSignal(0).seed; r.wifiCount = 7; r.bleCount = 1; return r; }

// ---- storage: a dozen buddies in RAM
static Owned items[MAX_COLLECTION];
static size_t count = 0;
void storageBegin() {
  for (int i = 0; i < 12; i++) items[count++] = Owned{0xC0DE0000u + i * 7919u, (uint8_t)((i == 4) ? 1 : 0), (uint8_t)(3 + i % 6), (uint16_t)(i * 3)};
}
size_t collectionSize() { return count; }
Owned collectionAt(size_t i) { return items[i]; }
int findOwned(uint32_t seed, bool night) { for (size_t i = 0; i < count; i++) if (items[i].seed == seed && ownedNight(items[i]) == night) return i; return -1; }
bool addOwned(const Owned& o) { if (count >= MAX_COLLECTION) return false; items[count++] = o; return true; }
void updateOwned(size_t i, const Owned& o) { items[i] = o; }
void removeOwned(size_t i) { for (size_t j = i; j + 1 < count; j++) items[j] = items[j + 1]; count--; }
uint32_t totalScans() { return 42; }
void countScan() {}
bool loadSoundSetting() { return true; }
void saveSoundSetting(bool) {}

// ---- friend link: pretend a friend is always there
#include "link.h"
LinkState linkStateNow = LINK_OFF;
void linkStart() { linkStateNow = LINK_SEARCHING; }
void linkStop(bool) { linkStateNow = LINK_OFF; }
void linkPoll() {}
LinkState linkState() { return linkStateNow; }
bool linkPeerLeft() { return false; }
bool linkCheated() { return false; }
void linkSendTeam(uint32_t, bool, uint8_t) {}
bool linkHaveTeam() { return false; }
uint32_t linkPeerSeed() { return 0xF00DCAFEu; }
bool linkPeerNight() { return false; }
uint8_t linkPeerLevel() { return 6; }
uint32_t linkBattleSeed() { return 77; }
void linkCommit(uint16_t, uint8_t) {}
bool linkTurnReady(uint16_t, uint8_t&) { return false; }
