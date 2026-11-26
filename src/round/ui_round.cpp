// BattleBuddies for the Waveshare ESP32-S3 1.43in round AMOLED (466x466).
//
// The same game as src/main.cpp (the CYD version): the same screens, rules,
// buddies and saves, laid out for a round touch screen. Game rules, buddy
// DNA and drawing, scanning and saving are shared with the CYD build; only
// this screen layer and the hardware layer (hw_round.cpp, display.cpp) are
// specific to this board.
//
// Layout notes: everything sits inside a 466 px circle. Text uses the free
// fonts (crisp at this resolution); the main actions sit in one "action row"
// near the bottom, where the circle is still wide enough for two buttons.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "battle.h"
#include "board.h"
#include "clock.h"
#include "config.h"
#include "display.h"
#include "hw.h"
#include "monster.h"
#include "render.h"
#include "scanner.h"
#include "storage.h"
#include "splash_logo.h"

Canvas& tft = canvas();
const int CX = LCD_W / 2, CY = LCD_H / 2;

// ---------------------------------------------------------------- theme
const uint16_t BG     = rgb565(16, 20, 40);
const uint16_t PANEL  = rgb565(34, 40, 72);
const uint16_t INK    = rgb565(235, 238, 255);
const uint16_t DIM    = rgb565(140, 150, 190);
const uint16_t GOLD   = rgb565(255, 205, 60);
const uint16_t MINT   = rgb565(90, 230, 190);
const uint16_t RED    = rgb565(235, 80, 80);
const uint16_t BTN    = rgb565(70, 110, 220);
const uint16_t BTN2   = rgb565(80, 80, 120);
const uint16_t GO     = rgb565(60, 180, 100);

// Fonts, smallest to largest. F_TINY and F_F2 are the built-in bitmap fonts,
// used only when a name doesn't fit a small space in the free fonts.
enum FontId : uint8_t { F_TINY, F_F2, F_XS, F_SM, F_SMB, F_MD, F_LG };
void useFont(uint8_t f) {
  switch (f) {
    case F_TINY: tft.setTextFont(1); break;
    case F_F2:   tft.setTextFont(2); break;
    case F_XS:   tft.setFreeFont(&FreeSans9pt7b); break;
    case F_SM:   tft.setFreeFont(&FreeSans12pt7b); break;
    case F_SMB:  tft.setFreeFont(&FreeSansBold12pt7b); break;
    case F_MD:   tft.setFreeFont(&FreeSansBold18pt7b); break;
    default:     tft.setFreeFont(&FreeSansBold24pt7b); break;
  }
  tft.setTextSize(1);
}
// The largest font up to `f` in which `s` fits in `maxW` pixels.
uint8_t fitFont(const char* s, uint8_t f, int maxW) {
  for (;; f--) {
    useFont(f);
    if (f == F_TINY || tft.textWidth(s) <= maxW) return f;
  }
}

void text(const char* s, int x, int y, uint8_t font, uint16_t fg, uint8_t datum = MC_DATUM, int maxW = 0) {
  if (maxW > 0) fitFont(s, font, maxW); else useFont(font);
  tft.setTextDatum(datum);
  tft.setTextColor(fg);
  tft.drawString(s, x, y);
}

// Half the width of the screen circle at row y (minus a small margin).
int halfWidthAt(int y) {
  int dy = abs(y - CY);
  int r = CX - 10;
  return dy >= r ? 0 : (int)sqrtf((float)(r * r - dy * dy));
}

// Clear a band of rows (just inside the circle) before redrawing text there.
void clearBand(int y0, int y1, uint16_t bg = BG) {
  int hw = max(halfWidthAt(y0), halfWidthAt(y1));
  if (y0 <= CY && y1 >= CY) hw = CX - 10;
  tft.fillRect(CX - hw, y0, 2 * hw, y1 - y0, bg);
}

struct Btn { int16_t x, y, w, h; const char* label; uint16_t color; uint8_t font; };

void drawBtn(const Btn& b, const char* label = nullptr) {
  int r = b.h / 2;
  tft.fillRoundRect(b.x, b.y + 4, b.w, b.h, r, shade565(b.color, 45));
  tft.fillRoundRect(b.x, b.y, b.w, b.h, r, b.color);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, r, shade565(b.color, 150));
  text(label ? label : b.label, b.x + b.w / 2, b.y + b.h / 2, b.font, INK, MC_DATUM, b.w - 14);
}
bool hit(const Btn& b, int x, int y) {
  return x >= b.x - 6 && x < b.x + b.w + 6 && y >= b.y - 6 && y < b.y + b.h + 8;
}

// The action row: one wide button and one narrow one, low on the circle.
const int ACT_Y = 362, ACT_H = 48;
Btn actMain(const char* label, uint16_t color, uint8_t font = F_SMB) { return {96, ACT_Y, 172, ACT_H, label, color, font}; }
Btn actSide(const char* label, uint16_t color = BTN2) { return {276, ACT_Y, 96, ACT_H, label, color, F_SMB}; }

void drawStars(int x, int y, int n, int of) {   // rarity gems, centred on x
  const int step = 18, w = of * step;
  for (int i = 0; i < of; i++) {
    int cx = x - w / 2 + i * step + step / 2;
    uint16_t c = i < n ? GOLD : shade565(DIM, 50);
    tft.fillTriangle(cx - 7, y, cx + 7, y, cx, y - 9, c);
    tft.fillTriangle(cx - 7, y, cx + 7, y, cx, y + 9, c);
  }
}

void elementTag(uint8_t e, int x, int y) {   // centred pill
  const char* n = elementName(e);
  useFont(F_XS);
  int w = tft.textWidth(n) + 22;
  tft.fillRoundRect(x - w / 2, y - 13, w, 26, 13, elementColor(e));
  text(n, x, y, F_XS, rgb565(15, 15, 25));
}

uint32_t starSeed = 0;
void starfield(uint32_t seed) {
  starSeed = seed;
  for (int i = 0; i < 110; i++) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    int x = seed % LCD_W, y = (seed >> 9) % LCD_H;
    if (halfWidthAt(y) < abs(x - CX) + 6) continue;   // keep stars off the edge ring
    tft.drawPixel(x, y, (seed >> 20) & 1 ? DIM : shade565(DIM, 50));
  }
}

void ledForElement(uint8_t e) {
  switch (e) {
    case EMBER: ledColor(1, 0, 0); break;
    case TIDE:  ledColor(0, 0, 1); break;
    case MOSS:  ledColor(0, 1, 0); break;
    case VOLT:  ledColor(1, 1, 0); break;
    case SHADE: ledColor(1, 0, 1); break;
    default:    ledColor(0, 1, 1); break;
  }
}

// Redraws a monster in place with a little bob and blink. The canvas is
// off-screen, so clearing and redrawing never flickers.
void monsterBox(int cx, int cy, int size, int& x, int& y, int& w, int& h) {
  x = cx - size * 68 / 100; w = size * 136 / 100;
  y = cy - size * 64 / 100; h = size * 124 / 100;
}
void showMonster(const Monster& m, int cx, int cy, int size, uint32_t t, uint16_t bg = BG,
                 int dx = 0, bool blinkNow = false, int bobOverride = -99) {
  int bob = bobOverride != -99 ? bobOverride : (int)(sinf(t / 260.0f) * 4);
  bool blink = blinkNow || (t % 3200) < 140;
  int x, y, w, h;
  monsterBox(cx, cy, size, x, y, w, h);
  tft.setViewport(x, y, w, h, false);   // clip to the box; coordinates stay absolute
  tft.fillRect(x, y, w, h, bg);
  if (bg == BG && starSeed) starfield(starSeed);
  drawMonster(tft, m, cx + dx, cy + bob, size, blink);
  tft.resetViewport();
}

// ---------------------------------------------------------------- state
enum Screen { S_TITLE, S_SCAN, S_PICK, S_ENCOUNTER, S_RESULT, S_COLLECTION, S_DETAIL, S_TOUCHTEST,
              S_FIGHTER, S_BATTLE, S_BOT_INTRO, S_BOT_ROUND, S_CLOCK };
Screen screen = S_TITLE;
uint32_t screenStart = 0;
void go(Screen s);

Monster wild;
uint8_t wildLv = 1, wildSource = SRC_WIFI;
int8_t  wildRssi = 0;
enum Outcome { OUT_CAUGHT, OUT_LEVELUP, OUT_FLED, OUT_FULL, OUT_LOST, OUT_STARTER } outcome;
uint8_t resultLevel = 1;
uint16_t fighterXpGained = 0;
bool     fighterLevelUp = false;
int collectionPage = 0;
int detailIndex = 0;
bool releaseArmed = false;

const Monster MASCOT = makeMonster(0x0B0DD1E5u, false);

// ---------------------------------------------------------------- title
const Btn B_WIFI  {70, 296, 158, 48, "WiFi scan", GO, F_SMB};
const Btn B_BT    {238, 296, 158, 48, "Bluetooth", rgb565(60, 120, 230), F_SMB};
const Btn B_COLL  {84, 352, 144, 46, "Collection", BTN, F_SMB};
const Btn B_BOT   {238, 352, 144, 46, "Bot 9000", rgb565(200, 70, 70), F_SMB};
const Btn B_CLOCK {140, 406, 110, 36, "Clock", BTN2, F_XS};
const Btn B_TEST  {258, 406, 48, 36, "?", BTN2, F_SMB};
ScanMode scanMode = MODE_WIFI;
bool scanForBot = false;
void startBotMatch();
uint32_t clockDrawnAt = 0;

void drawTitleClock() {
  clearBand(266, 292);
  uint8_t h, m, s;
  char buf[40];
  if (clockNow(h, m, s)) {
    snprintf(buf, sizeof(buf), clockIsNight() ? "%02u:%02u  -  night forms are out" : "%02u:%02u", h, m);
    text(buf, CX, 279, F_XS, clockIsNight() ? rgb565(190, 140, 255) : DIM);
  } else {
    text("Clock not set: no night forms", CX, 279, F_XS, DIM);
  }
  clockDrawnAt = millis();
}

void enterTitle() {
  ledOff();
  tft.clear(BG);
  starfield(1234);
  text("BATTLE", CX, 70, F_LG, GOLD);
  text("BUDDIES", CX, 116, F_LG, MINT);
  drawTitleClock();
  drawBtn(B_WIFI);
  drawBtn(B_BT);
  drawBtn(B_COLL);
  drawBtn(B_BOT);
  drawBtn(B_CLOCK);
  drawBtn(B_TEST);
}

void updateTitle(uint32_t t) {
  showMonster(MASCOT, CX, 202, 96, t);
  if (millis() - clockDrawnAt > 5000) drawTitleClock();
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_WIFI, x, y))      { sfxClick(); scanMode = MODE_WIFI; scanForBot = false; go(S_SCAN); }
  else if (hit(B_BT, x, y))   { sfxClick(); scanMode = MODE_BLE; scanForBot = false; go(S_SCAN); }
  else if (hit(B_COLL, x, y)) { sfxClick(); collectionPage = 0; go(S_COLLECTION); }
  else if (hit(B_BOT, x, y)) {
    sfxClick();
    if (collectionSize() == 0) { drawBtn(B_BOT, "Need a buddy"); return; }
    startBotMatch();
  }
  else if (hit(B_CLOCK, x, y)) { sfxClick(); go(S_CLOCK); }
  else if (hit(B_TEST, x, y))  { go(S_TOUCHTEST); }
}

// ---------------------------------------------------------------- scanning
uint32_t lastPing = 0;
const int MAX_BLIPS = 16;
struct Blip { int16_t x, y; uint16_t color; bool owned; };
Blip blips[MAX_BLIPS];
int blipCount = 0;
int pickSel = -1;
bool pickNight = false;
bool encounterFromPick = false;
int radarCx = CX, radarCy = 238, radarR = 100;

void enterScan() {
  tft.clear(BG);
  starSeed = 0;
  if (scanForBot) {
    text("BOT 9000", CX, 70, F_LG, RED);
    text("is scanning for its team...", CX, 108, F_SM, DIM);
  } else {
    text("SCANNING", CX, 70, F_LG, GOLD);
    text(scanMode == MODE_WIFI ? "Looking for WiFi networks" : "Looking for Bluetooth gadgets", CX, 108, F_SM, DIM, MC_DATUM, 330);
  }
  radarCx = CX; radarCy = 238; radarR = 100;
  blipCount = 0;
  pickSel = -1;
  encounterFromPick = false;
  scannerStart(scanForBot ? MODE_BOTH : scanMode);
  countScan();
  lastPing = 0;
}

void drawRadar(uint32_t t) {
  const int c = radarCx, cy = radarCy, R = radarR;
  const uint16_t green = rgb565(60, 220, 120);
  tft.fillCircle(c, cy, R + 3, BG);
  tft.fillCircle(c, cy, R, rgb565(10, 40, 30));
  for (int r = R; r > 0; r -= R / 3) tft.drawCircle(c, cy, r, shade565(green, 45));
  tft.drawFastHLine(c - R, cy, 2 * R, shade565(green, 35));
  tft.drawFastVLine(c, cy - R, 2 * R, shade565(green, 35));
  float a = t / 300.0f;
  for (int i = 0; i < 10; i++) {
    float ai = a - i * 0.05f;
    tft.drawLine(c, cy, c + cosf(ai) * R, cy + sinf(ai) * R, shade565(green, 100 - i * 9));
  }
  if (blipCount == 0) {
    uint32_t s = 0xC0FFEEu ^ (t / 900);
    for (int i = 0; i < 4; i++) {
      s ^= s << 13; s ^= s >> 17; s ^= s << 5;
      float ba = (s % 628) / 100.0f, br = 20 + (s >> 10) % (R - 28);
      tft.fillCircle(c + cosf(ba) * br, cy + sinf(ba) * br, 4, rgb565(180, 255, 200));
    }
  } else {
    for (int i = blipCount - 1; i >= 0; i--) {
      const Blip& b = blips[i];
      tft.fillCircle(b.x, b.y, 7, b.color);
      tft.drawCircle(b.x, b.y, 7, b.owned ? INK : shade565(b.color, 50));
      if (b.owned) tft.drawCircle(b.x, b.y, 8, INK);
    }
    if (pickSel >= 0) {
      const Blip& b = blips[pickSel];
      int pr = 11 + (t / 120) % 4;
      tft.drawCircle(b.x, b.y, pr, GOLD);
      tft.drawCircle(b.x, b.y, pr + 1, GOLD);
    }
  }
}

void updateScan(uint32_t t) {
  ScanPhase p = scannerPoll();
  drawRadar(t);
  if (t - lastPing > 1050) { sfxScanPing(); lastPing = t; }
  clearBand(350, 378);
  text(p == SCAN_WIFI ? "Listening to WiFi..." : "Listening to Bluetooth...", CX, 364, F_SMB, INK);
  if (p == SCAN_DONE && t >= WILD_SCAN_MIN_MS) {
    ScanResult r = scannerResult();
    char buf[40];
    if (scanForBot)                 snprintf(buf, sizeof(buf), "%u WiFi + %u Bluetooth signals", r.wifiCount, r.bleCount);
    else if (scanMode == MODE_WIFI) snprintf(buf, sizeof(buf), "%u WiFi networks nearby", r.wifiCount);
    else                            snprintf(buf, sizeof(buf), "%u Bluetooth gadgets nearby", r.bleCount);
    text(buf, CX, 394, F_SM, DIM, MC_DATUM, 300);
    displayFlush();
    delay(500);
    if (scanForBot) { go(S_BOT_INTRO); return; }
    pickNight = r.night;
    if (scannerSignalCount() > 0) { go(S_PICK); return; }
    wild = makeMonster(r.seed, r.night);
    wildLv = wildLevel(wild);
    wildSource = r.source;
    wildRssi = r.rssi;
    go(S_ENCOUNTER);
  }
}

// ---------------------------------------------------------------- pick a signal
const Btn B_TRACK = actMain("Track it!", GO, F_MD);
const Btn B_PBACK = actSide("Back");

void placeBlips() {
  blipCount = min(MAX_BLIPS, scannerSignalCount());
  for (int i = 0; i < blipCount; i++) {
    ScanSignal s = scannerSignal(i);
    Monster m = makeMonster(s.seed, pickNight);
    int r = map(constrain(s.rssi, -95, -30), -30, -95, 14, radarR - 10);
    float a = (s.seed % 628) / 100.0f;
    blips[i].x = radarCx + (int)(cosf(a) * r);
    blips[i].y = radarCy + (int)(sinf(a) * r);
    blips[i].color = elementColor(m.element);
    blips[i].owned = findOwned(s.seed, pickNight) >= 0;
  }
}

void drawPickInfo() {
  clearBand(308, 358);
  if (pickSel < 0) {
    text("Tap a blip to pick a signal", CX, 322, F_SMB, INK, MC_DATUM, 360);
    text("middle = strongest, colour = element", CX, 346, F_XS, DIM, MC_DATUM, 340);
    return;
  }
  ScanSignal s = scannerSignal(pickSel);
  char name[24];
  if (s.name[0]) snprintf(name, sizeof(name), "%s", s.name);
  else           snprintf(name, sizeof(name), s.source == SRC_WIFI ? "(hidden network)" : "(unnamed gadget)");
  text(name, CX, 322, F_SMB, INK, MC_DATUM, 360);
  char buf[44];
  snprintf(buf, sizeof(buf), "%d dBm%s", s.rssi, blips[pickSel].owned ? "  -  already on your team" : "");
  text(buf, CX, 346, F_XS, DIM, MC_DATUM, 340);
}

void drawTrackBtn() {
  if (pickSel >= 0) drawBtn(B_TRACK);
  else { Btn off = B_TRACK; off.color = shade565(BTN2, 70); drawBtn(off); }
}

void enterPick() {
  tft.clear(BG);
  starSeed = 0;
  text("PICK A SIGNAL", CX, 74, F_MD, GOLD);
  char buf[44];
  int n = scannerSignalCount();
  if (scanMode == MODE_WIFI) snprintf(buf, sizeof(buf), "%d WiFi network%s found", n, n == 1 ? "" : "s");
  else                       snprintf(buf, sizeof(buf), "%d Bluetooth gadget%s found", n, n == 1 ? "" : "s");
  if (n > MAX_BLIPS) snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " (%d closest)", MAX_BLIPS);
  text(buf, CX, 104, F_SM, DIM, MC_DATUM, 330);
  radarCx = CX; radarCy = 212; radarR = 90;
  placeBlips();
  if (pickSel >= blipCount) pickSel = -1;
  drawPickInfo();
  drawTrackBtn();
  drawBtn(B_PBACK);
}

void trackSelected() {
  ScanSignal s = scannerSignal(pickSel);
  wild = makeMonster(s.seed, pickNight);
  wildLv = wildLevel(wild);
  wildSource = s.source;
  wildRssi = s.rssi;
  encounterFromPick = true;
  go(S_ENCOUNTER);
}

void updatePick(uint32_t t) {
  drawRadar(t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_PBACK, x, y)) { sfxClick(); go(S_TITLE); return; }
  if (hit(B_TRACK, x, y)) { if (pickSel >= 0) { sfxClick(); trackSelected(); } return; }
  int best = -1, bestD = 30 * 30;   // nearest blip within a fingertip
  for (int i = 0; i < blipCount; i++) {
    int dx = blips[i].x - x, dy = blips[i].y - y, d = dx * dx + dy * dy;
    if (d < bestD) { bestD = d; best = i; }
  }
  if (best < 0) return;
  sfxClick();
  if (best == pickSel) { trackSelected(); return; }
  pickSel = best;
  ledColor(0, 1, 0);
  drawPickInfo();
  drawTrackBtn();
}

// ---------------------------------------------------------------- encounter
const Btn B_CATCH = actMain("BATTLE!", GO, F_MD);
const Btn B_RUN   = actSide("Run");

enum BattleKind : uint8_t { BK_WILD, BK_BOT };
BattleKind battleKind = BK_WILD;
void startWildBattle();

const int ENC_MON_Y = 180, ENC_MON_SIZE = 100;

void enterEncounter() {
  ledForElement(wild.element);
  sfxFound();
  tft.clear(BG);
  starfield(wild.seed);
  text(wild.night ? "A night form appears!" : "A wild buddy appears!", CX, 66, F_SMB, GOLD, MC_DATUM, 290);
  char buf[44];
  if (wildSource == SRC_WIFI)      snprintf(buf, sizeof(buf), "from a WiFi signal (%d dBm)", wildRssi);
  else if (wildSource == SRC_BLE)  snprintf(buf, sizeof(buf), "from a Bluetooth signal (%d dBm)", wildRssi);
  else                             snprintf(buf, sizeof(buf), "out of the static...");
  text(buf, CX, 94, F_XS, DIM, MC_DATUM, 340);
  if (findOwned(wild.seed, wild.night) < 0) {
    tft.fillRoundRect(306, 116, 64, 26, 13, RED);
    text("NEW!", 338, 129, F_XS, INK);
  }
  text(wild.name, CX, 262, F_MD, INK, MC_DATUM, 360);
  elementTag(wild.element, 150, 294);
  snprintf(buf, sizeof(buf), "Lv %u", wildLv);
  text(buf, CX, 294, F_SMB, INK);
  drawStars(316, 292, wild.rarity + 1, 4);
  text(rarityName(wild.rarity), 316, 316, F_XS, DIM);
  if (collectionSize() == 0) {
    text("It wants to be your first buddy!", CX, 342, F_XS, MINT);
    drawBtn(B_CATCH, "Befriend");
  } else {
    text("Beat it in battle to catch it", CX, 342, F_XS, DIM);
    drawBtn(B_CATCH);
  }
  drawBtn(B_RUN);
}

void updateEncounter(uint32_t t) {
  showMonster(wild, CX, ENC_MON_Y, ENC_MON_SIZE, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_CATCH, x, y)) {
    sfxClick();
    if (collectionSize() == 0) {
      Owned o{wild.seed, (uint8_t)((wild.night ? 1 : 0) | (wildSource << 1)), wildLv, 0};
      addOwned(o);
      sfxCaught();
      outcome = OUT_STARTER;
      resultLevel = wildLv;
      fighterXpGained = 0;
      go(S_RESULT);
    } else {
      startWildBattle();
    }
  }
  else if (hit(B_RUN, x, y)) { sfxClick(); go(encounterFromPick ? S_PICK : S_TITLE); }
}

bool giveXp(int idx, uint16_t xp) {
  if (idx < 0 || idx >= (int)collectionSize()) return false;
  Owned o = collectionAt(idx);
  uint8_t before = o.level;
  o.xp += xp;
  while (o.level < 99 && o.xp >= xpForNext(o.level)) { o.xp -= xpForNext(o.level); o.level++; }
  updateOwned(idx, o);
  return o.level > before;
}

// ---------------------------------------------------------------- result (wild battles)
const Btn B_AGAIN = actMain("Scan again", GO);
const Btn B_HOME  = actSide("Home");
int fighterIndex = 0;

void enterResult() {
  tft.clear(BG);
  starfield(wild.seed ^ 77);
  char buf[48];
  switch (outcome) {
    case OUT_STARTER:
      text("New friend!", CX, 72, F_LG, MINT);
      snprintf(buf, sizeof(buf), "%s is your first buddy (Lv %u)", wild.name, resultLevel);
      break;
    case OUT_CAUGHT:
      text("Caught!", CX, 72, F_LG, MINT);
      snprintf(buf, sizeof(buf), "%s joined your team at Lv %u", wild.name, resultLevel);
      break;
    case OUT_LEVELUP:
      text("Power up!", CX, 72, F_LG, GOLD);
      snprintf(buf, sizeof(buf), "Your %s got stronger (Lv %u)", wild.name, resultLevel);
      break;
    case OUT_FULL:
      text("Team full!", CX, 72, F_LG, RED);
      snprintf(buf, sizeof(buf), "Release a buddy to make room");
      break;
    case OUT_LOST:
      text("It got away", CX, 72, F_LG, RED);
      snprintf(buf, sizeof(buf), "%s won and ran off", wild.name);
      break;
    default:
      text("It got away", CX, 72, F_LG, RED);
      snprintf(buf, sizeof(buf), "%s ran back into the signals", wild.name);
      break;
  }
  text(buf, CX, 278, F_SMB, INK, MC_DATUM, 410);
  if (fighterXpGained && fighterIndex < (int)collectionSize()) {
    Owned o = collectionAt(fighterIndex);
    Monster m = makeMonster(o.seed, ownedNight(o));
    if (fighterLevelUp) snprintf(buf, sizeof(buf), "%s +%u XP - Level up! Lv %u", m.name, fighterXpGained, o.level);
    else                snprintf(buf, sizeof(buf), "%s +%u XP", m.name, fighterXpGained);
    text(buf, CX, 310, F_SM, fighterLevelUp ? GOLD : MINT, MC_DATUM, 390);
  }
  drawBtn(B_AGAIN);
  drawBtn(B_HOME);
}

void updateResult(uint32_t t) {
  if (outcome != OUT_FLED && outcome != OUT_LOST) showMonster(wild, CX, 178, 110, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_AGAIN, x, y)) { sfxClick(); scanForBot = false; go(S_SCAN); }
  else if (hit(B_HOME, x, y)) { sfxClick(); go(S_TITLE); }
}

// ---------------------------------------------------------------- buddy grid (collection + fighter picker)
// 3x3 cards in the middle of the circle, page arrows at 9 and 3 o'clock.
const int GRID = 9, CELL = 92, GAP = 6, GRID_X = 89, GRID_Y = 98;
const Btn B_PREV {14, 210, 64, 64, "<", BTN, F_MD};
const Btn B_NEXT {388, 210, 64, 64, ">", BTN, F_MD};
const Btn B_BACK {163, 396, 140, 40, "Back", BTN2, F_SMB};

int pageCount() { return max(1, (int)((collectionSize() + GRID - 1) / GRID)); }

void drawBuddyCard(size_t idx, int slot, bool out) {
  int x = GRID_X + (slot % 3) * (CELL + GAP), y = GRID_Y + (slot / 3) * (CELL + GAP);
  Owned o = collectionAt(idx);
  Monster m = makeMonster(o.seed, ownedNight(o));
  uint16_t cellBg = out ? shade565(PANEL, 50) : ownedNight(o) ? rgb565(26, 22, 52) : PANEL;
  tft.fillRoundRect(x, y, CELL, CELL, 12, cellBg);
  tft.fillRect(x + 10, y + CELL - 5, CELL - 20, 3, out ? DIM : elementColor(m.element));
  drawMonster(tft, m, x + CELL / 2, y + 38, 50);
  text(m.name, x + CELL / 2, y + 76, F_XS, out ? DIM : INK, MC_DATUM, CELL - 8);
  char buf[8];
  snprintf(buf, sizeof(buf), "Lv%u", o.level);
  text(buf, x + 8, y + 12, F_F2, DIM, ML_DATUM);
  if (out) {
    tft.fillRoundRect(x + CELL - 34, y + 4, 30, 18, 9, RED);
    text("KO", x + CELL - 19, y + 13, F_F2, INK);
  }
}

// Which card (index into the collection) is under (x, y), or -1.
int cardAt(int x, int y) {
  if (x < GRID_X || y < GRID_Y) return -1;
  int col = (x - GRID_X) / (CELL + GAP), row = (y - GRID_Y) / (CELL + GAP);
  if (col > 2 || row > 2) return -1;
  size_t idx = collectionPage * GRID + row * 3 + col;
  return idx < collectionSize() ? (int)idx : -1;
}

void enterCollection() {
  ledOff();
  tft.clear(BG);
  starSeed = 0;
  collectionPage = constrain(collectionPage, 0, pageCount() - 1);
  char buf[32];
  text("Collection", CX, 54, F_MD, GOLD);
  snprintf(buf, sizeof(buf), "%u buddies", (unsigned)collectionSize());
  text(buf, CX, 82, F_XS, DIM);
  if (collectionSize() == 0) {
    text("No buddies yet.", CX, 220, F_SMB, INK);
    text("Go scan something!", CX, 252, F_SM, DIM);
  }
  for (int i = 0; i < GRID; i++) {
    size_t idx = collectionPage * GRID + i;
    if (idx >= collectionSize()) break;
    drawBuddyCard(idx, i, false);
  }
  drawBtn(B_PREV);
  snprintf(buf, sizeof(buf), "Back  %d/%d", collectionPage + 1, pageCount());
  drawBtn(B_BACK, buf);
  drawBtn(B_NEXT);
}

void updateCollection(uint32_t) {
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_BACK, x, y)) { sfxClick(); go(S_TITLE); return; }
  if (hit(B_PREV, x, y)) { sfxClick(); collectionPage = (collectionPage + pageCount() - 1) % pageCount(); go(S_COLLECTION); return; }
  if (hit(B_NEXT, x, y)) { sfxClick(); collectionPage = (collectionPage + 1) % pageCount(); go(S_COLLECTION); return; }
  int idx = cardAt(x, y);
  if (idx >= 0) { sfxClick(); detailIndex = idx; go(S_DETAIL); }
}

// ---------------------------------------------------------------- detail
Monster detailMon;
const Btn B_DBACK = actMain("Back", BTN2);
const Btn B_FREE  = actSide("Release", RED);

void statBar(const char* label, uint16_t v, int y, uint16_t c) {
  char buf[8];
  text(label, 106, y, F_XS, DIM, ML_DATUM);
  tft.fillRoundRect(150, y - 5, 164, 10, 5, shade565(PANEL, 60));
  tft.fillRoundRect(150, y - 5, max(10, min(164, (int)v * 164 / 120)), 10, 5, c);
  snprintf(buf, sizeof(buf), "%u", v);
  text(buf, 360, y, F_XS, INK, MR_DATUM);
}

void enterDetail() {
  releaseArmed = false;
  Owned o = collectionAt(detailIndex);
  detailMon = makeMonster(o.seed, ownedNight(o));
  ledForElement(detailMon.element);
  tft.clear(BG);
  starfield(o.seed);
  static const char* const SRC[] = {"Found via WiFi", "Found via Bluetooth", "Found in the static"};
  char buf[44];
  snprintf(buf, sizeof(buf), "%s  -  XP %u/%u", SRC[min<uint8_t>(2, ownedSource(o))], o.xp, xpForNext(o.level));
  text(buf, CX, 58, F_XS, DIM, MC_DATUM, 280);
  text(detailMon.name, CX, 218, F_MD, INK, MC_DATUM, 400);
  elementTag(detailMon.element, 156, 250);
  snprintf(buf, sizeof(buf), "Lv %u", o.level);
  text(buf, CX, 250, F_SMB, INK);
  drawStars(310, 249, detailMon.rarity + 1, 4);
  const uint8_t lv = o.level;
  statBar("HP",  statAtLevel(detailMon.hp, lv),  276, rgb565(90, 220, 120));
  statBar("ATK", statAtLevel(detailMon.atk, lv), 296, rgb565(240, 110, 80));
  statBar("DEF", statAtLevel(detailMon.def, lv), 316, rgb565(110, 160, 240));
  statBar("SPD", statAtLevel(detailMon.spd, lv), 336, rgb565(240, 210, 80));
  drawBtn(B_DBACK);
  drawBtn(B_FREE);
}

void updateDetail(uint32_t t) {
  showMonster(detailMon, CX, 136, 100, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_DBACK, x, y)) { sfxClick(); go(S_COLLECTION); }
  else if (hit(B_FREE, x, y)) {
    sfxClick();
    if (!releaseArmed) { releaseArmed = true; drawBtn(B_FREE, "Sure?"); }
    else { removeOwned(detailIndex); sfxFled(); go(S_COLLECTION); }
  }
}

// ---------------------------------------------------------------- battle bot 9000 (state)
const int BOT_TEAM = 3;
Monster botTeam[BOT_TEAM];
uint8_t botLv[BOT_TEAM];
int     botNext = 0;
int     botSignals = 0;
int     roundsPlayed = 0;
int8_t  roundWinner = -1;
uint16_t roundXp = 0;
bool    roundLevelUp = false;
bool    koOut[MAX_COLLECTION];
bool    fought[MAX_COLLECTION];
const uint16_t BOT_RED = rgb565(200, 70, 70);

void startBotMatch() {
  memset(koOut, 0, sizeof(koOut));
  memset(fought, 0, sizeof(fought));
  botNext = 0;
  roundsPlayed = 0;
  scanForBot = true;
  go(S_SCAN);
}

uint8_t teamLevel() {
  uint8_t top[3] = {0, 0, 0};
  for (size_t i = 0; i < collectionSize(); i++) {
    uint8_t lv = collectionAt(i).level;
    for (int k = 0; k < 3; k++) if (lv > top[k]) { for (int j = 2; j > k; j--) top[j] = top[j - 1]; top[k] = lv; break; }
  }
  int n = 0, sum = 0;
  for (int k = 0; k < 3; k++) if (top[k]) { sum += top[k]; n++; }
  return n ? (uint8_t)((sum + n / 2) / n) : 1;
}

void buildBotTeam() {
  int n = scannerSignalCount();
  botSignals = n;
  bool night = scannerResult().night;
  int used[BOT_TEAM] = {-1, -1, -1};
  uint8_t base = teamLevel();
  for (int i = 0; i < BOT_TEAM; i++) {
    uint32_t seed;
    if (i < n) {
      int pick;
      do { pick = esp_random() % n; } while ((pick == used[0] || pick == used[1]) && n > i);
      used[i] = pick;
      seed = scannerSignal(pick).seed;
    } else {
      seed = esp_random();
    }
    botTeam[i] = makeMonster(seed, night);
    botLv[i] = opponentLevel(base, esp_random());
  }
}

int fightersLeft() {
  int n = 0;
  for (size_t i = 0; i < collectionSize(); i++) if (!(battleKind == BK_BOT && koOut[i])) n++;
  return n;
}

void drawBot(int cx, int cy, int s, bool antennaOn) {
  const uint16_t steel = rgb565(150, 160, 180), dark = rgb565(70, 78, 98);
  int w = s, h = s * 4 / 5, x = cx - w / 2, y = cy - h / 2;
  tft.fillRect(cx - 1, y - s / 4, 3, s / 4, dark);
  tft.fillCircle(cx, y - s / 4, s / 14 + 2, antennaOn ? RED : shade565(RED, 40));
  tft.fillRect(x - s / 10, cy - s / 8, s / 10, s / 4, dark);
  tft.fillRect(x + w, cy - s / 8, s / 10, s / 4, dark);
  tft.fillRoundRect(x, y, w, h, s / 8, steel);
  tft.drawRoundRect(x, y, w, h, s / 8, dark);
  tft.fillRoundRect(x + s / 10, y + s / 10, w - s / 5, h * 9 / 20, s / 12, rgb565(20, 26, 44));
  int ey = y + s / 10 + h * 9 / 40, er = s / 11;
  tft.fillCircle(cx - w / 4 + s / 20, ey, er, rgb565(80, 230, 255));
  tft.fillCircle(cx + w / 4 - s / 20, ey, er, rgb565(80, 230, 255));
  tft.fillCircle(cx - w / 4 + s / 20, ey, er / 2, INK);
  tft.fillCircle(cx + w / 4 - s / 20, ey, er / 2, INK);
  int gy = y + h * 7 / 10, gw = w / 2, gh = h / 6;
  tft.fillRoundRect(cx - gw / 2, gy, gw, gh, 3, dark);
  for (int i = 1; i < 5; i++) tft.drawFastVLine(cx - gw / 2 + i * gw / 5, gy + 2, gh - 4, steel);
}

void drawBotBanner() {
  tft.fillRoundRect(106, 44, 254, 42, 21, BOT_RED);
  tft.drawRoundRect(106, 44, 254, 42, 21, shade565(BOT_RED, 150));
  text("BATTLE BOT 9000", CX, 65, F_SMB, INK);
}

// ---------------------------------------------------------------- fighter picker
Monster foeMon;
uint8_t foeLv = 1;
const Btn B_FCANCEL {163, 396, 140, 40, "Cancel", BTN2, F_SMB};

void chooseFighter() {
  int only = -1, n = 0;
  for (size_t i = 0; i < collectionSize(); i++)
    if (!(battleKind == BK_BOT && koOut[i])) { only = i; n++; }
  if (n == 1) { fighterIndex = only; go(S_BATTLE); return; }
  collectionPage = 0;
  go(S_FIGHTER);
}

void startWildBattle() {
  battleKind = BK_WILD;
  foeMon = wild;
  foeLv = wildLv;
  chooseFighter();
}

void enterFighter() {
  ledForElement(foeMon.element);
  tft.clear(BG);
  starSeed = 0;
  collectionPage = constrain(collectionPage, 0, pageCount() - 1);
  text("Pick a fighter", CX, 54, F_MD, GOLD);
  char buf[44];
  snprintf(buf, sizeof(buf), "vs %s  Lv %u  %s", foeMon.name, foeLv, elementName(foeMon.element));
  text(buf, CX, 82, F_XS, DIM, MC_DATUM, 300);
  for (int i = 0; i < GRID; i++) {
    size_t idx = collectionPage * GRID + i;
    if (idx >= collectionSize()) break;
    drawBuddyCard(idx, i, battleKind == BK_BOT && koOut[idx]);
  }
  drawBtn(B_PREV);
  drawBtn(B_FCANCEL);
  drawBtn(B_NEXT);
}

void cancelFight() {
  if (battleKind == BK_WILD) go(S_ENCOUNTER);
  else { roundWinner = -1; go(S_BOT_ROUND); }
}

void updateFighter(uint32_t) {
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_FCANCEL, x, y)) { sfxClick(); cancelFight(); return; }
  if (hit(B_PREV, x, y)) { sfxClick(); collectionPage = (collectionPage + pageCount() - 1) % pageCount(); go(S_FIGHTER); return; }
  if (hit(B_NEXT, x, y)) { sfxClick(); collectionPage = (collectionPage + 1) % pageCount(); go(S_FIGHTER); return; }
  int idx = cardAt(x, y);
  if (idx < 0) return;
  if (battleKind == BK_BOT && koOut[idx]) { sfxMiss(); return; }
  sfxClick();
  fighterIndex = idx;
  go(S_BATTLE);
}

// ---------------------------------------------------------------- battle
// Foe top right, your buddy lower left, the five throws (and Run) along the
// bottom of the circle. The rules live in battle.*.
Battle battle;
TurnResult lastTurn;
enum BattlePhase { BP_CHOOSE, BP_REVEAL, BP_RESULT } bPhase;
uint32_t bPhaseAt;
uint8_t  bMove[2];
float    shownHp[2];
bool     battleFled;

const Btn B_MOVE[MOVE_COUNT] = {
  {78, 342, 100, 38, "Rock", BTN, F_SM},  {183, 342, 100, 38, "Paper", BTN, F_SM},
  {288, 342, 100, 38, "Scissors", BTN, F_SM},
  {108, 386, 82, 38, "Lizard", BTN, F_SM}, {194, 386, 82, 38, "Spock", BTN, F_SM},
};
const Btn B_FLEE {280, 386, 78, 38, "Run", BTN2, F_SM};

const int MON_X[2] = {156, 306}, MON_Y[2] = {230, 128}, MON_SIZE[2] = {88, 84};
const int PNL_X[2] = {230, 92},  PNL_Y[2] = {196, 78};
const int PNL_W = 156, PNL_H = 64;

void drawHpBar(int side) {
  const Fighter& f = battle.f[side];
  int x = PNL_X[side] + 10, y = PNL_Y[side] + 46, w = PNL_W - 20;
  int hp = (int)(shownHp[side] + 0.5f);
  int fill = f.maxHp ? hp * w / f.maxHp : 0;
  uint16_t c = hp * 2 > f.maxHp ? rgb565(90, 220, 120) : hp * 5 > f.maxHp ? GOLD : RED;
  tft.fillRect(x, y, w, 8, shade565(PANEL, 50));
  tft.fillRect(x, y, fill, 8, c);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d/%u", hp, f.maxHp);
  tft.fillRect(PNL_X[side] + PNL_W - 62, PNL_Y[side] + 26, 54, 16, PANEL);
  text(buf, PNL_X[side] + PNL_W - 10, PNL_Y[side] + 34, F_F2, INK, MR_DATUM);
}

void drawFighterPanel(int side) {
  const Fighter& f = battle.f[side];
  int x = PNL_X[side], y = PNL_Y[side];
  tft.fillRoundRect(x, y, PNL_W, PNL_H, 12, PANEL);
  tft.fillRect(x + 12, y + PNL_H - 5, PNL_W - 24, 3, elementColor(f.mon.element));
  text(f.mon.name, x + 10, y + 14, F_SMB, INK, ML_DATUM, PNL_W - 16);
  char buf[24];
  snprintf(buf, sizeof(buf), "Lv%u %s", f.level, elementName(f.mon.element));
  text(buf, x + 10, y + 34, F_F2, side ? DIM : MINT, ML_DATUM);
  drawHpBar(side);
}

void drawMoveButtons(bool active) {
  uint8_t fav = favouredMove(battle.f[0].mon.element);
  for (uint8_t m = 0; m < MOVE_COUNT; m++) {
    Btn b = B_MOVE[m];
    if (m == fav) b.color = shade565(elementColor(battle.f[0].mon.element), 70);
    if (!active && m != bMove[0]) b.color = shade565(b.color, 45);
    drawBtn(b);
  }
  Btn f = B_FLEE;
  if (!active) f.color = shade565(f.color, 45);
  drawBtn(f);
}

void battleMessage(const char* line1, uint16_t c1, const char* line2, uint16_t c2) {
  clearBand(284, 336);
  if (line1) text(line1, CX, 297, F_SMB, c1, MC_DATUM, 400);
  if (line2) text(line2, CX, 323, F_SM, c2, MC_DATUM, 380);
}

void enterBattle() {
  Owned o = collectionAt(fighterIndex);
  Monster mine = makeMonster(o.seed, ownedNight(o));
  battleBegin(battle, mine, o.level, foeMon, foeLv, esp_random());
  shownHp[0] = battle.f[0].hp;
  shownHp[1] = battle.f[1].hp;
  bPhase = BP_CHOOSE;
  bMove[0] = bMove[1] = NO_MOVE;
  battleFled = false;

  ledForElement(foeMon.element);
  sfxFound();
  tft.clear(BG);
  starfield(foeMon.seed);
  char buf[48];
  if (battleKind == BK_BOT) {
    snprintf(buf, sizeof(buf), "BOT 9000 sends out %s! (%d/%d)", foeMon.name, botNext + 1, BOT_TEAM);
    text(buf, CX, 52, F_XS, BOT_RED, MC_DATUM, 270);
  } else {
    snprintf(buf, sizeof(buf), "Win to catch %s!", foeMon.name);
    text(buf, CX, 52, F_SM, GOLD, MC_DATUM, 270);
  }
  drawFighterPanel(1);
  drawFighterPanel(0);
  battleMessage("Pick a throw!", INK, nullptr, INK);
  drawMoveButtons(true);
}

void drawBattler(int side, uint32_t t) {
  const Fighter& f = battle.f[side];
  int dx = 0;
  uint16_t bg = BG;
  bool hurt = bPhase == BP_RESULT && lastTurn.damage[side] > 0;
  uint32_t since = millis() - bPhaseAt;
  if (hurt && since < 450) {
    dx = (since / 45) % 2 ? 5 : -5;
    if (since < 140) bg = shade565(RED, 45);
  }
  bool down = f.hp == 0 && bPhase == BP_RESULT && since >= 450;
  int bob = down ? 12 : (int)(sinf((t + side * 700) / 260.0f) * 4);
  bool blink = down || ((t + side * 1100) % 3200) < 140;
  int size = MON_SIZE[side];
  if (down) {   // shrink into the bottom of the same box
    int x, y, w, h;
    monsterBox(MON_X[side], MON_Y[side], size, x, y, w, h);
    tft.setViewport(x, y, w, h, false);
    tft.fillRect(x, y, w, h, bg);
    if (starSeed) starfield(starSeed);
    drawMonster(tft, f.mon, MON_X[side], MON_Y[side] + bob, size * 3 / 4, true);
    tft.resetViewport();
    return;
  }
  showMonster(f.mon, MON_X[side], MON_Y[side], size, t, bg, dx, blink, bob);
}

void showTurnResult() {
  const TurnResult& r = lastTurn;
  char l1[40], l2[44];
  uint16_t c1 = INK, c2 = INK;
  if (r.winner >= 0) {
    uint8_t w = r.move[r.winner], l = r.move[1 - r.winner];
    snprintf(l1, sizeof(l1), "%s %s %s!", moveName(w), moveVerb(w, l), moveName(l));
    uint16_t d = r.damage[1 - r.winner];
    const char* who = r.winner == 0 ? "Foe takes" : "You take";
    if (r.favoured)
      snprintf(l2, sizeof(l2), "%s bonus! %s %u", elementName(battle.f[r.winner].mon.element), who, d);
    else
      snprintf(l2, sizeof(l2), "%s %u damage", who, d);
    c1 = r.winner == 0 ? MINT : RED;
    c2 = r.favoured ? GOLD : INK;
    if (r.winner == 0) { sfxThrow(); ledColor(0, 1, 0); } else { sfxMiss(); ledColor(1, 0, 0); }
  } else {
    snprintf(l1, sizeof(l1), "%s vs %s: a tie!", moveName(r.move[0]), moveName(r.move[1]));
    bool tide0 = battle.f[0].mon.element == TIDE, tide1 = battle.f[1].mon.element == TIDE;
    if (r.damage[0] && r.damage[1])
      snprintf(l2, sizeof(l2), "Both get scraped (-%u / -%u)", r.damage[0], r.damage[1]);
    else if (r.damage[1])
      snprintf(l2, sizeof(l2), tide0 ? "Tide flows! Foe -%u" : "You're faster! Foe -%u", r.damage[1]);
    else if (r.damage[0])
      snprintf(l2, sizeof(l2), tide1 ? "Foe's Tide flows! You -%u" : "Foe is faster! You -%u", r.damage[0]);
    else
      snprintf(l2, sizeof(l2), "Nobody budges");
    c1 = GOLD;
    sfxClick();
    ledColor(1, 1, 0);
  }
  battleMessage(l1, c1, l2, c2);
}

void finishBattle() {
  bool won = !battleFled && battleWinner(battle) == 0;
  uint16_t xp = won ? battleXp(battle.f[1]) : 0;
  bool lvUp = won && giveXp(fighterIndex, xp);

  if (battleKind == BK_WILD) {
    fighterXpGained = xp;
    fighterLevelUp = lvUp;
    if (battleFled) { outcome = OUT_FLED; }
    else if (!won)  { outcome = OUT_LOST; sfxFled(); }
    else {
      sfxCaught();
      int idx = findOwned(wild.seed, wild.night);
      if (idx >= 0) {
        giveXp(idx, 10 + wildLv * 5);
        resultLevel = collectionAt(idx).level;
        outcome = OUT_LEVELUP;
      } else {
        Owned o{wild.seed, (uint8_t)((wild.night ? 1 : 0) | (wildSource << 1)), wildLv, 0};
        outcome = addOwned(o) ? OUT_CAUGHT : OUT_FULL;
        resultLevel = wildLv;
      }
    }
    go(S_RESULT);
    return;
  }

  fought[fighterIndex] = true;
  roundsPlayed++;
  roundXp = xp;
  roundLevelUp = lvUp;
  if (battleFled) roundWinner = -1;
  else if (won)   { roundWinner = 0; botNext++; }
  else            { roundWinner = 1; koOut[fighterIndex] = true; }
  go(S_BOT_ROUND);
}

void updateBattle(uint32_t t) {
  drawBattler(1, t);
  drawBattler(0, t);
  for (int s = 0; s < 2; s++) {
    float target = battle.f[s].hp;
    if (shownHp[s] > target) {
      shownHp[s] = max(target, shownHp[s] - max(0.6f, (shownHp[s] - target) * 0.12f));
      drawHpBar(s);
    }
  }
  uint32_t now = millis(), since = now - bPhaseAt;
  int x, y;

  switch (bPhase) {
    case BP_CHOOSE:
      if (!touchTapped(x, y)) return;
      if (hit(B_FLEE, x, y)) {
        sfxFled();
        battleFled = true;
        finishBattle();
        return;
      }
      for (uint8_t m = 0; m < MOVE_COUNT; m++) {
        if (!hit(B_MOVE[m], x, y)) continue;
        sfxClick();
        bMove[0] = m;
        bMove[1] = aiChooseMove(battle, 1, esp_random());
        drawMoveButtons(false);
        char buf[40];
        snprintf(buf, sizeof(buf), "You: %s   Foe: %s", moveName(bMove[0]), moveName(bMove[1]));
        battleMessage("Ready... throw!", DIM, buf, INK);
        bPhase = BP_REVEAL;
        bPhaseAt = now;
        return;
      }
      return;

    case BP_REVEAL:
      if (since < 750) return;
      lastTurn = battleTurn(battle, bMove[0], bMove[1]);
      showTurnResult();
      bPhase = BP_RESULT;
      bPhaseAt = now;
      return;

    case BP_RESULT:
      if (battleOver(battle)) {
        if (since > 1900) finishBattle();
        return;
      }
      if (since < 1500) return;
      ledForElement(battle.f[1].mon.element);
      bPhase = BP_CHOOSE;
      bMove[0] = NO_MOVE;
      touchTapped(x, y);
      battleMessage("Pick a throw!", INK, nullptr, INK);
      drawMoveButtons(true);
      return;
  }
}

// ---------------------------------------------------------------- battle bot 9000 (screens)
const Btn B_BOTGO   = actMain("Fight!", GO, F_MD);
const Btn B_BOTBACK = actSide("Back");
const Btn B_NEXTRND = actMain("Next round", GO);
const Btn B_GIVEUP  = actSide("Give up");
const Btn B_REMATCH = actMain("Rematch", GO);
bool botAntenna = false;
uint32_t botBlinkAt = 0;
const int BOT_X = CX, BOT_Y = 140, BOT_S = 64;

void drawTeamDots(int cx, int y) {
  for (int i = 0; i < BOT_TEAM; i++) {
    int x = cx + (i - 1) * 26;
    if (i < botNext) { tft.fillCircle(x, y, 10, shade565(DIM, 40)); text("x", x, y, F_F2, RED); }
    else tft.fillCircle(x, y, 10, elementColor(botTeam[i].element));
  }
}

void enterBotIntro() {
  buildBotTeam();
  ledColor(1, 0, 0);
  sfxFound();
  tft.clear(BG);
  starfield(botTeam[0].seed);
  drawBotBanner();
  drawBot(BOT_X, BOT_Y, BOT_S, true);
  char buf[44];
  if (botSignals > 0) snprintf(buf, sizeof(buf), "Beep! My team comes from %d signals.", botSignals);
  else                snprintf(buf, sizeof(buf), "Beep! I pulled my team from static.");
  text(buf, CX, 192, F_SM, INK, MC_DATUM, 400);
  text("Beat all three to win!", CX, 216, F_XS, DIM);
  for (int i = 0; i < BOT_TEAM; i++) {
    int x = 80 + i * 104, y = 232;
    tft.fillRoundRect(x, y, 98, 114, 12, PANEL);
    tft.fillRect(x + 10, y + 109, 78, 3, elementColor(botTeam[i].element));
    drawMonster(tft, botTeam[i], x + 49, y + 42, 50);
    text(botTeam[i].name, x + 49, y + 80, F_XS, INK, MC_DATUM, 90);
    snprintf(buf, sizeof(buf), "Lv%u %s", botLv[i], elementName(botTeam[i].element));
    text(buf, x + 49, y + 98, F_F2, DIM);
  }
  drawBtn(B_BOTGO);
  drawBtn(B_BOTBACK);
  botBlinkAt = millis();
}

void blinkAntenna() {
  if (millis() - botBlinkAt < 450) return;
  botBlinkAt = millis();
  botAntenna = !botAntenna;
}

void nextBotRound() {
  battleKind = BK_BOT;
  foeMon = botTeam[botNext];
  foeLv = botLv[botNext];
  chooseFighter();
}

void updateBotIntro(uint32_t) {
  blinkAntenna();
  int top = BOT_Y - (BOT_S * 4 / 5) / 2 - BOT_S / 4;   // matches drawBot()
  tft.fillCircle(BOT_X, top, BOT_S / 14 + 2, botAntenna ? RED : shade565(RED, 40));
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_BOTGO, x, y)) { sfxClick(); nextBotRound(); }
  else if (hit(B_BOTBACK, x, y)) { sfxClick(); go(S_TITLE); }
}

bool botMatchWon()  { return botNext >= BOT_TEAM; }
bool botMatchLost() { return roundWinner == -1 || fightersLeft() == 0; }

void enterBotRound() {
  tft.clear(BG);
  starSeed = 0;
  drawBotBanner();
  char buf[48];
  if (botMatchWon()) {
    for (size_t i = 0; i < collectionSize(); i++) if (fought[i]) giveXp(i, 20);
    sfxCaught();
    ledColor(0, 1, 0);
    drawBot(CX, 158, 70, false);
    text("You beat BOT 9000!", CX, 230, F_MD, MINT, MC_DATUM, 400);
    text("Bzzt... system overload!", CX, 264, F_SM, DIM);
    text("+20 XP for every buddy who fought", CX, 292, F_XS, GOLD, MC_DATUM, 400);
    if (roundXp) {
      Owned o = collectionAt(fighterIndex);
      snprintf(buf, sizeof(buf), "%s +%u XP for the last round%s", makeMonster(o.seed, ownedNight(o)).name,
               roundXp, roundLevelUp ? " - Level up!" : "");
      text(buf, CX, 318, F_XS, roundLevelUp ? GOLD : MINT, MC_DATUM, 390);
    }
    drawBtn(B_REMATCH);
    drawBtn(B_HOME);
    return;
  }
  if (botMatchLost()) {
    sfxFled();
    ledColor(1, 0, 0);
    drawBot(CX, 158, 70, true);
    text("BOT 9000 wins", CX, 230, F_MD, RED);
    text(roundWinner == -1 ? "You gave up the match" : "All your fighters are knocked out", CX, 264, F_SM, INK, MC_DATUM, 400);
    text("Beep boop. Level up and try again!", CX, 292, F_XS, DIM);
    drawBtn(B_REMATCH);
    drawBtn(B_HOME);
    return;
  }
  // Mid-match: who won the round, and who Bot sends next.
  snprintf(buf, sizeof(buf), "Round %d done", roundsPlayed);
  text(buf, 96, 112, F_SMB, GOLD, ML_DATUM);
  drawTeamDots(338, 112);
  Owned o = collectionAt(fighterIndex);
  Monster mine = makeMonster(o.seed, ownedNight(o));
  if (roundWinner == 0) {
    ledColor(0, 1, 0);
    snprintf(buf, sizeof(buf), "%s won the round!", mine.name);
    text(buf, CX, 262, F_SMB, MINT, MC_DATUM, 410);
    snprintf(buf, sizeof(buf), "+%u XP%s", roundXp, roundLevelUp ? " - Level up!" : "");
    text(buf, CX, 288, F_XS, roundLevelUp ? GOLD : MINT);
  } else {
    ledColor(1, 0, 0);
    snprintf(buf, sizeof(buf), "%s was knocked out", mine.name);
    text(buf, CX, 262, F_SMB, RED, MC_DATUM, 410);
    snprintf(buf, sizeof(buf), "%d fighter%s left", fightersLeft(), fightersLeft() == 1 ? "" : "s");
    text(buf, CX, 288, F_XS, DIM);
  }
  snprintf(buf, sizeof(buf), "Next up: %s  Lv %u", botTeam[botNext].name, botLv[botNext]);
  text(buf, CX, 322, F_SMB, INK, MC_DATUM, 390);
  drawBtn(B_NEXTRND);
  drawBtn(B_GIVEUP);
}

void updateBotRound(uint32_t t) {
  bool over = botMatchWon() || botMatchLost();
  if (!over) showMonster(botTeam[botNext], CX, 188, 80, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (over) {
    if (hit(B_REMATCH, x, y)) { sfxClick(); startBotMatch(); }
    else if (hit(B_HOME, x, y)) { sfxClick(); go(S_TITLE); }
  } else {
    if (hit(B_NEXTRND, x, y)) { sfxClick(); nextBotRound(); }
    else if (hit(B_GIVEUP, x, y)) { sfxClick(); roundWinner = -1; go(S_BOT_ROUND); }
  }
}

// ---------------------------------------------------------------- clock (night forms)
// This board has no light sensor, so night forms follow the clock instead.
const Btn B_HUP   {110, 126, 96, 44, "+", BTN, F_MD};
const Btn B_MUP   {260, 126, 96, 44, "+", BTN, F_MD};
const Btn B_HDOWN {110, 282, 96, 44, "-", BTN, F_MD};
const Btn B_MDOWN {260, 282, 96, 44, "-", BTN, F_MD};
const Btn B_CSAVE = actMain("Save", GO);
const Btn B_CBACK = actSide("Cancel");
uint8_t editH = 12, editM = 0;

void drawClockDigits() {
  tft.fillRect(100, 178, 266, 98, BG);
  char buf[4];
  tft.setTextFont(7);
  tft.setTextSize(2);
  tft.setTextColor(clockIsNightHour(editH) ? rgb565(190, 140, 255) : GOLD);
  snprintf(buf, sizeof(buf), "%02u", editH);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(buf, 222, 228);
  snprintf(buf, sizeof(buf), "%02u", editM);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(buf, 244, 228);
  tft.setTextSize(1);
  tft.fillRect(229, 206, 8, 8, GOLD);
  tft.fillRect(229, 242, 8, 8, GOLD);
  clearBand(330, 354);
  text(clockIsNightHour(editH) ? "Night: night forms come out" : "Day: regular buddies", CX, 342, F_XS,
       clockIsNightHour(editH) ? rgb565(190, 140, 255) : DIM);
}

void enterClock() {
  ledOff();
  tft.clear(BG);
  starSeed = 0;
  uint8_t s;
  if (!clockNow(editH, editM, s)) { editH = 12; editM = 0; }
  text("Set the clock", CX, 62, F_MD, GOLD);
  char buf[44];
  snprintf(buf, sizeof(buf), "Night forms appear %d:00 - %d:00", NIGHT_FROM_HOUR, NIGHT_TO_HOUR);
  text(buf, CX, 96, F_XS, DIM, MC_DATUM, 330);
  drawBtn(B_HUP); drawBtn(B_MUP); drawBtn(B_HDOWN); drawBtn(B_MDOWN);
  drawClockDigits();
  drawBtn(B_CSAVE);
  drawBtn(B_CBACK);
}

void updateClock(uint32_t) {
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_HUP, x, y))        { editH = (editH + 1) % 24; drawClockDigits(); }
  else if (hit(B_HDOWN, x, y)) { editH = (editH + 23) % 24; drawClockDigits(); }
  else if (hit(B_MUP, x, y))   { editM = (editM + 1) % 60; drawClockDigits(); }
  else if (hit(B_MDOWN, x, y)) { editM = (editM + 59) % 60; drawClockDigits(); }
  else if (hit(B_CSAVE, x, y)) { sfxClick(); clockSet(editH, editM); go(S_TITLE); }
  else if (hit(B_CBACK, x, y)) { sfxClick(); go(S_TITLE); }
}

// ---------------------------------------------------------------- touch test
const Btn B_EXIT {173, 210, 120, 46, "Exit", BTN2, F_SMB};

void enterTouchTest() {
  ledOff();
  tft.clear(TFT_BLACK);
  starSeed = 0;
  text("Touch test: tap around", CX, 70, F_SMB, INK);
  text("dots should land under your finger", CX, 98, F_XS, DIM);
  tft.drawCircle(CX, CY, CX - 10, DIM);
  drawBtn(B_EXIT);
}

void updateTouchTest(uint32_t) {
  int x, y;
  if (touchTapped(x, y) && hit(B_EXIT, x, y)) { go(S_TITLE); return; }
  if (touchDown(x, y)) {
    tft.fillCircle(x, y, 5, GOLD);
    char buf[24];
    snprintf(buf, sizeof(buf), "x=%3d y=%3d", x, y);
    tft.fillRect(150, 384, 166, 30, TFT_BLACK);
    text(buf, CX, 398, F_SMB, INK);
  }
}

// ---------------------------------------------------------------- main
void go(Screen s) {
  screen = s;
  screenStart = millis();
  int x, y;
  touchTapped(x, y);   // swallow the press that changed screens
  switch (s) {
    case S_TITLE:      enterTitle(); break;
    case S_SCAN:       enterScan(); break;
    case S_PICK:       enterPick(); break;
    case S_ENCOUNTER:  enterEncounter(); break;
    case S_RESULT:     enterResult(); break;
    case S_COLLECTION: enterCollection(); break;
    case S_DETAIL:     enterDetail(); break;
    case S_TOUCHTEST:  enterTouchTest(); break;
    case S_FIGHTER:    enterFighter(); break;
    case S_BATTLE:     enterBattle(); break;
    case S_BOT_INTRO:  enterBotIntro(); break;
    case S_BOT_ROUND:  enterBotRound(); break;
    case S_CLOCK:      enterClock(); break;
  }
}

// The 320x240 logo, scaled up 1.25x (smoothly) into the middle of the circle.
void drawSplash() {
  const float k = 1.25f;
  const int w = 320 * k, h = 240 * k, x0 = CX - w / 2, y0 = CY - h / 2;
  tft.clear(pgm_read_word(&splash_logo[0]));
  for (int y = 0; y < h; y++) {
    float sy = (y + 0.5f) / k - 0.5f;
    int iy = constrain((int)sy, 0, 238);
    float fy = constrain(sy - iy, 0.0f, 1.0f);
    for (int x = 0; x < w; x++) {
      float sx = (x + 0.5f) / k - 0.5f;
      int ix = constrain((int)sx, 0, 318);
      float fx = constrain(sx - ix, 0.0f, 1.0f);
      uint16_t p[4] = {pgm_read_word(&splash_logo[iy * 320 + ix]), pgm_read_word(&splash_logo[iy * 320 + ix + 1]),
                       pgm_read_word(&splash_logo[(iy + 1) * 320 + ix]), pgm_read_word(&splash_logo[(iy + 1) * 320 + ix + 1])};
      float wt[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
      float r = 0, g = 0, b = 0;
      for (int i = 0; i < 4; i++) {
        r += wt[i] * ((p[i] >> 11) & 31);
        g += wt[i] * ((p[i] >> 5) & 63);
        b += wt[i] * (p[i] & 31);
      }
      tft.drawPixel(x0 + x, y0 + y, ((uint16_t)(r + 0.5f) << 11) | ((uint16_t)(g + 0.5f) << 5) | (uint16_t)(b + 0.5f));
    }
  }
}

void setup() {
  Serial.begin(115200);
  if (!displayBegin()) Serial.println("Not enough memory for the screen canvas (is PSRAM enabled?)");
  hwBegin();

  // Splash logo while the radio wakes up: at least 2.5 s, a tap skips the rest.
  uint32_t splashAt = millis();
  drawSplash();
  displayFlush();
  storageBegin();
  setSoundOn(loadSoundSetting());
  scannerBegin();
  int tx, ty;
  touchTapped(tx, ty);
  while (millis() - splashAt < 2500 && !touchTapped(tx, ty)) delay(20);
  Serial.printf("BattleBuddies v0.1 (round AMOLED)  collection=%u  scans=%u  clock %s\n",
                (unsigned)collectionSize(), (unsigned)totalScans(), clockValid() ? "set" : "not set");
  go(S_TITLE);
  displayFlush();
}

void loop() {
  uint32_t t = millis() - screenStart;
  switch (screen) {
    case S_TITLE:      updateTitle(t); break;
    case S_SCAN:       updateScan(t); break;
    case S_PICK:       updatePick(t); break;
    case S_ENCOUNTER:  updateEncounter(t); break;
    case S_RESULT:     updateResult(t); break;
    case S_COLLECTION: updateCollection(t); break;
    case S_DETAIL:     updateDetail(t); break;
    case S_TOUCHTEST:  updateTouchTest(t); break;
    case S_FIGHTER:    updateFighter(t); break;
    case S_BATTLE:     updateBattle(t); break;
    case S_BOT_INTRO:  updateBotIntro(t); break;
    case S_BOT_ROUND:  updateBotRound(t); break;
    case S_CLOCK:      updateClock(t); break;
  }
  displayFlush();
  delay(8);
}
