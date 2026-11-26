// BattleBuddies v0.1 - scan, capture and collect monsters on an ESP32 CYD.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "battle.h"
#include "config.h"
#include "hw.h"
#include "monster.h"
#include "render.h"
#include "scanner.h"
#include "storage.h"

TFT_eSPI tft;
TFT_eSprite spr(&tft);
bool haveSprite = false;
const int SPR = 150;   // sprite used for animated monsters and the radar

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

struct Btn { int16_t x, y, w, h; const char* label; uint16_t color; uint8_t font; };

void drawBtn(const Btn& b, const char* label = nullptr) {
  tft.fillRoundRect(b.x, b.y + 3, b.w, b.h, 8, shade565(b.color, 45));
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, b.color);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, shade565(b.color, 150));
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(INK, b.color);
  tft.drawString(label ? label : b.label, b.x + b.w / 2, b.y + b.h / 2 + 1, b.font);
}
bool hit(const Btn& b, int x, int y) {
  return x >= b.x - 4 && x < b.x + b.w + 4 && y >= b.y - 4 && y < b.y + b.h + 6;
}

void text(const char* s, int x, int y, uint8_t font, uint16_t fg, uint8_t datum = MC_DATUM, uint16_t bg = BG) {
  tft.setTextDatum(datum);
  tft.setTextColor(fg, bg);
  tft.drawString(s, x, y, font);
}

void drawStars(int x, int y, int n, int of, uint16_t bg = BG) {   // rarity gems, centred on x
  int w = of * 12;
  for (int i = 0; i < of; i++) {
    int cx = x - w / 2 + i * 12 + 6;
    uint16_t c = i < n ? GOLD : shade565(DIM, 50);
    tft.fillTriangle(cx - 5, y, cx + 5, y, cx, y - 6, c);
    tft.fillTriangle(cx - 5, y, cx + 5, y, cx, y + 6, c);
  }
  (void)bg;
}

void elementTag(uint8_t e, int x, int y) {   // centred pill
  const char* n = elementName(e);
  int w = tft.textWidth(n, 2) + 16;
  tft.fillRoundRect(x - w / 2, y - 9, w, 18, 9, elementColor(e));
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(rgb565(15, 15, 25), elementColor(e));
  tft.drawString(n, x, y + 1, 2);
}

void starfield(uint32_t seed) {
  for (int i = 0; i < 40; i++) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    tft.drawPixel(seed % 240, (seed >> 9) % 320, (seed >> 20) & 1 ? DIM : shade565(DIM, 50));
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

// Draws a monster in the sprite with a little bob and blink, then shows it.
void showMonster(const Monster& m, int cx, int cy, int size, uint32_t t, uint16_t bg = BG) {
  int bob = (int)(sinf(t / 260.0f) * 3);
  bool blink = (t % 3200) < 140;
  if (!haveSprite) {
    static uint32_t drawn = 0xFFFFFFFF;   // no sprite: draw once, no animation
    if (drawn != m.seed) { drawMonster(tft, m, cx, cy, size); drawn = m.seed; }
    return;
  }
  spr.fillSprite(bg);
  drawMonster(spr, m, SPR / 2, SPR / 2 + bob, size, blink);
  spr.pushSprite(cx - SPR / 2, cy - SPR / 2);
}

// ---------------------------------------------------------------- state
enum Screen { S_TITLE, S_SCAN, S_ENCOUNTER, S_CAPTURE, S_RESULT, S_COLLECTION, S_DETAIL, S_TOUCHTEST, S_BATTLE, S_BATTLE_END };
Screen screen = S_TITLE;
uint32_t screenStart = 0;
void go(Screen s);

Monster wild;
uint8_t wildLv = 1, wildSource = SRC_WIFI;
int8_t  wildRssi = 0;
enum Outcome { OUT_CAUGHT, OUT_LEVELUP, OUT_FLED, OUT_FULL } outcome;
uint8_t resultLevel = 1;
int collectionPage = 0;
int detailIndex = 0;
bool releaseArmed = false;

const Monster MASCOT = makeMonster(0x0B0DD1E5u, false);

// ---------------------------------------------------------------- title
const Btn B_WIFI  {14, 214, 102, 44, "WiFi scan", GO, 2};
const Btn B_BT    {124, 214, 102, 44, "Bluetooth", rgb565(60, 120, 230), 2};
ScanMode scanMode = MODE_WIFI;
const Btn B_COLL  {30, 266, 180, 30, "Collection", BTN, 2};
const Btn B_SOUND {6, 300, 74, 18, "", BTN2, 1};
const Btn B_TEST  {214, 300, 20, 18, "?", BTN2, 1};

void drawSoundBtn() { drawBtn(B_SOUND, soundOn() ? "Sound: on" : "Sound: off"); }

void enterTitle() {
  ledOff();
  tft.fillScreen(BG);
  starfield(1234);
  text("BATTLE", 120, 24, 4, GOLD);
  text("BUDDIES", 120, 50, 4, MINT);
  char buf[32];
  snprintf(buf, sizeof(buf), "Collection (%u)", (unsigned)collectionSize());
  drawBtn(B_WIFI);
  drawBtn(B_BT);
  drawBtn(B_COLL, buf);
  drawSoundBtn();
  drawBtn(B_TEST);
  text("v0.1", 160, 309, 1, DIM);
}

void updateTitle(uint32_t t) {
  showMonster(MASCOT, 120, 136, 100, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_WIFI, x, y))      { sfxClick(); scanMode = MODE_WIFI; go(S_SCAN); }
  else if (hit(B_BT, x, y))   { sfxClick(); scanMode = MODE_BLE; go(S_SCAN); }
  else if (hit(B_COLL, x, y)) { sfxClick(); collectionPage = 0; go(S_COLLECTION); }
  else if (hit(B_SOUND, x, y)) { setSoundOn(!soundOn()); saveSoundSetting(soundOn()); sfxClick(); drawSoundBtn(); }
  else if (hit(B_TEST, x, y)) { go(S_TOUCHTEST); }
}

// ---------------------------------------------------------------- scanning
uint32_t lastPing = 0;

void enterScan() {
  tft.fillScreen(BG);
  text("SCANNING", 120, 22, 4, GOLD);
  text(scanMode == MODE_WIFI ? "Looking for WiFi networks" : "Looking for Bluetooth gadgets", 120, 48, 2, DIM);
  scannerStart(scanMode);
  countScan();
  lastPing = 0;
}

void drawRadar(uint32_t t) {
  if (!haveSprite) return;
  const int c = SPR / 2, R = 70;
  const uint16_t green = rgb565(60, 220, 120);
  spr.fillSprite(BG);
  spr.fillCircle(c, c, R, rgb565(10, 40, 30));
  for (int r = R; r > 0; r -= 23) spr.drawCircle(c, c, r, shade565(green, 45));
  spr.drawFastHLine(c - R, c, 2 * R, shade565(green, 35));
  spr.drawFastVLine(c, c - R, 2 * R, shade565(green, 35));
  float a = t / 300.0f;
  for (int i = 0; i < 8; i++) {   // sweep with a fading trail
    float ai = a - i * 0.06f;
    spr.drawLine(c, c, c + cosf(ai) * R, c + sinf(ai) * R, shade565(green, 100 - i * 11));
  }
  uint32_t s = 0xC0FFEEu ^ (t / 900);   // a few blips that change now and then
  for (int i = 0; i < 4; i++) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    float ba = (s % 628) / 100.0f, br = 15 + (s >> 10) % (R - 20);
    spr.fillCircle(c + cosf(ba) * br, c + sinf(ba) * br, 3, rgb565(180, 255, 200));
  }
  spr.pushSprite(120 - c, 150 - c);
}

void updateScan(uint32_t t) {
  ScanPhase p = scannerPoll();
  drawRadar(t);
  if (t - lastPing > 1050) { sfxScanPing(); lastPing = t; }
  text(p == SCAN_WIFI ? "  Listening to WiFi...  " : "Listening to Bluetooth...", 120, 250, 2, INK);
  if (p == SCAN_DONE && t >= WILD_SCAN_MIN_MS) {
    ScanResult r = scannerResult();
    char buf[40];
    if (scanMode == MODE_WIFI) snprintf(buf, sizeof(buf), "%u WiFi networks nearby", r.wifiCount);
    else                       snprintf(buf, sizeof(buf), "%u Bluetooth gadgets nearby", r.bleCount);
    text(buf, 120, 272, 2, DIM);
    wild = makeMonster(r.seed, r.night);
    wildLv = wildLevel(wild);
    wildSource = r.source;
    wildRssi = r.rssi;
    delay(500);
    go(S_ENCOUNTER);
  }
}

// ---------------------------------------------------------------- encounter
const Btn B_CATCH {10, 272, 140, 40, "CAPTURE", GO, 4};
const Btn B_RUN   {160, 272, 70, 40, "Run", BTN2, 2};

void enterEncounter() {
  ledForElement(wild.element);
  sfxFound();
  tft.fillScreen(BG);
  starfield(wild.seed);
  text(wild.night ? "A night form appears!" : "A wild buddy appears!", 120, 14, 2, GOLD);
  char buf[40];
  if (wildSource == SRC_WIFI)      snprintf(buf, sizeof(buf), "from a WiFi signal (%d dBm)", wildRssi);
  else if (wildSource == SRC_BLE)  snprintf(buf, sizeof(buf), "from a Bluetooth signal (%d dBm)", wildRssi);
  else                             snprintf(buf, sizeof(buf), "out of the static...");
  text(buf, 120, 32, 1, DIM);

  tft.fillRoundRect(8, 190, 224, 74, 10, PANEL);
  text(wild.name, 120, 208, 4, INK, MC_DATUM, PANEL);
  elementTag(wild.element, 64, 238);
  snprintf(buf, sizeof(buf), "Lv %u", wildLv);
  text(buf, 120, 239, 2, INK, MC_DATUM, PANEL);
  drawStars(186, 238, wild.rarity + 1, 4);
  text(rarityName(wild.rarity), 186, 254, 1, DIM, MC_DATUM, PANEL);
  if (findOwned(wild.seed, wild.night) < 0) {
    tft.fillRoundRect(184, 44, 48, 18, 9, RED);
    text("NEW!", 208, 54, 2, INK, MC_DATUM, RED);
  }
  drawBtn(B_CATCH);
  drawBtn(B_RUN);
}

void updateEncounter(uint32_t t) {
  showMonster(wild, 120, 116, 104, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_CATCH, x, y)) { sfxClick(); go(S_CAPTURE); }
  else if (hit(B_RUN, x, y)) { sfxClick(); go(S_TITLE); }
}

// ---------------------------------------------------------------- capture mini-game
// A marker slides along a bar; tap while it is inside the target zone.
// Rarer buddies have a smaller zone and a faster marker. Three tries.
const int BAR_X = 20, BAR_Y = 222, BAR_W = 200, BAR_H = 22;
int zoneX, zoneW, tries;
float marker, speed;
uint32_t lastFrame, freezeUntil;
bool captureDone;

void newZone() {
  static const int ZONE_W[4] = {56, 42, 30, 22};
  zoneW = ZONE_W[min<uint8_t>(3, wild.rarity)];
  zoneX = BAR_X + 10 + esp_random() % (BAR_W - zoneW - 20);
}

void drawTries() {
  for (int i = 0; i < 3; i++) {
    int x = 92 + i * 28;
    uint16_t c = i < tries ? MINT : shade565(DIM, 40);
    tft.fillCircle(x, 268, 9, c);
    tft.drawFastHLine(x - 9, 268, 18, BG);
    tft.fillCircle(x, 268, 3, i < tries ? INK : shade565(DIM, 40));
  }
}

void drawBar() {
  tft.fillRoundRect(BAR_X - 2, BAR_Y - 2, BAR_W + 4, BAR_H + 4, 6, INK);
  tft.fillRect(BAR_X, BAR_Y, BAR_W, BAR_H, PANEL);
  tft.fillRect(zoneX, BAR_Y, zoneW, BAR_H, GO);
  tft.fillRect(zoneX + zoneW / 2 - 3, BAR_Y, 6, BAR_H, GOLD);   // perfect spot
  int mx = BAR_X + (int)marker;
  tft.fillRect(mx - 2, BAR_Y - 6, 5, BAR_H + 12, RED);
}

void enterCapture() {
  tft.fillScreen(BG);
  text("CAPTURE!", 120, 16, 4, GOLD);
  text("Tap when the marker is in the green", 120, 300, 1, DIM);
  tries = 3;
  marker = 0;
  speed = 2.2f + wild.rarity * 0.9f + wildLv * 0.1f;
  captureDone = false;
  freezeUntil = 0;
  lastFrame = millis();
  newZone();
  drawTries();
}

void updateCapture(uint32_t t) {
  showMonster(wild, 120, 120, 96, t);
  uint32_t now = millis();
  if (captureDone) {
    if (now > freezeUntil) go(S_RESULT);
    return;
  }
  if (now >= freezeUntil) {
    float dt = (now - lastFrame) / 16.0f;
    marker += speed * dt;
    if (marker >= BAR_W) { marker = BAR_W; speed = -fabsf(speed); }
    if (marker <= 0)     { marker = 0;     speed =  fabsf(speed); }
  }
  lastFrame = now;
  drawBar();

  int x, y;
  if (now < freezeUntil || !touchTapped(x, y)) return;
  sfxThrow();
  int mx = BAR_X + (int)marker;
  bool inZone = mx >= zoneX && mx < zoneX + zoneW;
  if (inZone) {
    sfxCaught();
    ledColor(0, 1, 0);
    bool perfect = abs(mx - (zoneX + zoneW / 2)) <= 4;
    text(perfect ? "  PERFECT!  " : "   Gotcha!   ", 120, 194, 4, perfect ? GOLD : MINT);
    // Save it: a new buddy, or XP for one you already have.
    int idx = findOwned(wild.seed, wild.night);
    if (idx >= 0) {
      Owned o = collectionAt(idx);
      o.xp += 10 + wildLv * 5 + (perfect ? 10 : 0);
      while (o.level < 99 && o.xp >= xpForNext(o.level)) { o.xp -= xpForNext(o.level); o.level++; }
      updateOwned(idx, o);
      resultLevel = o.level;
      outcome = OUT_LEVELUP;
    } else {
      Owned o{wild.seed, (uint8_t)((wild.night ? 1 : 0) | (wildSource << 1)), wildLv, 0};
      outcome = addOwned(o) ? OUT_CAUGHT : OUT_FULL;
      resultLevel = wildLv;
    }
    captureDone = true;
    freezeUntil = now + 1400;
  } else {
    tries--;
    drawTries();
    if (tries == 0) {
      sfxFled();
      ledColor(1, 0, 0);
      text("  It broke free!  ", 120, 194, 4, RED);
      outcome = OUT_FLED;
      captureDone = true;
      freezeUntil = now + 1400;
    } else {
      sfxMiss();
      text("   Missed!   ", 120, 194, 4, RED);
      freezeUntil = now + 700;
      tft.fillRect(0, 182, 240, 26, BG);
      newZone();
    }
  }
}

// ---------------------------------------------------------------- result
const Btn B_AGAIN {10, 270, 130, 40, "Scan again", GO, 2};   // same mode as last time
const Btn B_HOME  {150, 270, 80, 40, "Home", BTN2, 2};

void enterResult() {
  tft.fillScreen(BG);
  starfield(wild.seed ^ 77);
  char buf[48];
  switch (outcome) {
    case OUT_CAUGHT:
      text("Caught!", 120, 24, 4, MINT);
      snprintf(buf, sizeof(buf), "%s joined your team at Lv %u", wild.name, resultLevel);
      break;
    case OUT_LEVELUP:
      text("Power up!", 120, 24, 4, GOLD);
      snprintf(buf, sizeof(buf), "Your %s got stronger (Lv %u)", wild.name, resultLevel);
      break;
    case OUT_FULL:
      text("Team full!", 120, 24, 4, RED);
      snprintf(buf, sizeof(buf), "Release a buddy to make room");
      break;
    default:
      text("It got away", 120, 24, 4, RED);
      snprintf(buf, sizeof(buf), "%s ran back into the signals", wild.name);
      break;
  }
  text(buf, 120, 236, 2, INK);
  drawBtn(B_AGAIN);
  drawBtn(B_HOME);
}

void updateResult(uint32_t t) {
  if (outcome != OUT_FLED) showMonster(wild, 120, 134, 110, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_AGAIN, x, y)) { sfxClick(); go(S_SCAN); }
  else if (hit(B_HOME, x, y)) { sfxClick(); go(S_TITLE); }
}

// ---------------------------------------------------------------- collection
const int GRID = 9;
const Btn B_PREV {4, 282, 50, 34, "<", BTN, 4};
const Btn B_BACK {70, 282, 100, 34, "Back", BTN2, 2};
const Btn B_NEXT {186, 282, 50, 34, ">", BTN, 4};

int pageCount() { return max(1, (int)((collectionSize() + GRID - 1) / GRID)); }

void enterCollection() {
  ledOff();
  tft.fillScreen(BG);
  collectionPage = constrain(collectionPage, 0, pageCount() - 1);
  char buf[32];
  snprintf(buf, sizeof(buf), "%u buddies", (unsigned)collectionSize());
  text("Collection", 8, 16, 4, GOLD, ML_DATUM);
  text(buf, 232, 18, 2, DIM, MR_DATUM);
  if (collectionSize() == 0) {
    text("No buddies yet.", 120, 140, 2, INK);
    text("Go scan something!", 120, 162, 2, DIM);
  }
  for (int i = 0; i < GRID; i++) {
    size_t idx = collectionPage * GRID + i;
    if (idx >= collectionSize()) break;
    int col = i % 3, row = i / 3;
    int x = 4 + col * 78, y = 34 + row * 82;
    Owned o = collectionAt(idx);
    Monster m = makeMonster(o.seed, ownedNight(o));
    uint16_t cellBg = ownedNight(o) ? rgb565(26, 22, 52) : PANEL;
    tft.fillRoundRect(x, y, 76, 80, 8, cellBg);
    tft.fillRect(x + 6, y + 76, 64, 3, elementColor(m.element));
    drawMonster(tft, m, x + 38, y + 32, 50);
    text(m.name, x + 38, y + 64, 1, INK, MC_DATUM, cellBg);
    snprintf(buf, sizeof(buf), "Lv%u", o.level);
    text(buf, x + 5, y + 8, 1, DIM, ML_DATUM, cellBg);
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
  if (y >= 34 && y < 34 + 3 * 82) {
    int col = min(2, (x - 4) / 78), row = (y - 34) / 82;
    size_t idx = collectionPage * GRID + row * 3 + col;
    if (idx < collectionSize()) { sfxClick(); detailIndex = idx; go(S_DETAIL); }
  }
}

// ---------------------------------------------------------------- detail
Monster detailMon;
const Btn B_DBACK {6, 284, 70, 30, "Back", BTN2, 2};
const Btn B_FIGHT {84, 284, 72, 30, "Battle", GO, 2};
const Btn B_FREE  {164, 284, 70, 30, "Release", RED, 2};

void statBar(const char* label, uint16_t v, int y, uint16_t c) {
  char buf[8];
  text(label, 14, y, 1, DIM, ML_DATUM, PANEL);
  tft.fillRect(44, y - 4, 150, 8, shade565(PANEL, 60));
  tft.fillRect(44, y - 4, min(150, (int)v * 150 / 120), 8, c);
  snprintf(buf, sizeof(buf), "%u", v);
  text(buf, 226, y, 1, INK, MR_DATUM, PANEL);
}

void enterDetail() {
  releaseArmed = false;
  Owned o = collectionAt(detailIndex);
  detailMon = makeMonster(o.seed, ownedNight(o));
  ledForElement(detailMon.element);
  tft.fillScreen(BG);
  starfield(o.seed);
  tft.fillRoundRect(6, 170, 228, 108, 10, PANEL);
  text(detailMon.name, 120, 186, 4, INK, MC_DATUM, PANEL);
  char buf[40];
  elementTag(detailMon.element, 52, 210);
  snprintf(buf, sizeof(buf), "Lv %u", o.level);
  text(buf, 116, 211, 2, INK, MC_DATUM, PANEL);
  drawStars(186, 210, detailMon.rarity + 1, 4);
  const uint8_t lv = o.level;
  statBar("HP",  statAtLevel(detailMon.hp, lv),  228, rgb565(90, 220, 120));
  statBar("ATK", statAtLevel(detailMon.atk, lv), 240, rgb565(240, 110, 80));
  statBar("DEF", statAtLevel(detailMon.def, lv), 252, rgb565(110, 160, 240));
  statBar("SPD", statAtLevel(detailMon.spd, lv), 264, rgb565(240, 210, 80));
  static const char* const SRC[] = {"Found via WiFi", "Found via Bluetooth", "Found in the static"};
  snprintf(buf, sizeof(buf), "%s  -  XP %u/%u", SRC[min<uint8_t>(2, ownedSource(o))], o.xp, xpForNext(o.level));
  text(buf, 120, 10, 1, DIM);
  drawBtn(B_DBACK);
  drawBtn(B_FIGHT);
  drawBtn(B_FREE);
}

void updateDetail(uint32_t t) {
  showMonster(detailMon, 120, 92, 120, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_DBACK, x, y)) { sfxClick(); go(S_COLLECTION); }
  else if (hit(B_FIGHT, x, y)) { sfxClick(); go(S_BATTLE); }
  else if (hit(B_FREE, x, y)) {
    sfxClick();
    if (!releaseArmed) { releaseArmed = true; drawBtn(B_FREE, "Sure?"); }
    else { removeOwned(detailIndex); sfxFled(); go(S_COLLECTION); }
  }
}

// ---------------------------------------------------------------- battle
// Player's buddy (from the detail screen) vs a computer-controlled wild buddy.
// The rules live in battle.*; this section only draws and reads taps.
// Two small sprites would not fit next to the big one for long, so the battle
// borrows a 96x96 sprite while it runs and frees it again afterwards.
const int BSPR = 96;
TFT_eSprite bspr(&tft);
bool haveBspr = false;

Battle battle;
TurnResult lastTurn;
enum BattlePhase { BP_CHOOSE, BP_REVEAL, BP_RESULT } bPhase;
uint32_t bPhaseAt;
uint8_t  bMove[2];
float    shownHp[2];
bool     battleFled;
uint16_t battleXpGained;
bool     battleLevelUp;

const Btn B_MOVE[MOVE_COUNT] = {
  {6, 244, 72, 34, "Rock", BTN, 2},   {84, 244, 72, 34, "Paper", BTN, 2},
  {162, 244, 72, 34, "Scissors", BTN, 2},
  {6, 284, 72, 34, "Lizard", BTN, 2}, {84, 284, 72, 34, "Spock", BTN, 2},
};
const Btn B_FLEE {162, 284, 72, 34, "Run", BTN2, 2};

// Where each side's monster and info panel sit (0 = player, bottom-left).
const int MON_X[2] = {58, 182}, MON_Y[2] = {162, 70};
const int PNL_X[2] = {112, 6},  PNL_Y[2] = {132, 28};
const int PNL_W = 122, PNL_H = 58;

void battleFreeSprite() {
  if (haveBspr) { bspr.deleteSprite(); haveBspr = false; }
}

void drawHpBar(int side) {
  const Fighter& f = battle.f[side];
  int x = PNL_X[side] + 8, y = PNL_Y[side] + 40, w = PNL_W - 16;
  int hp = (int)(shownHp[side] + 0.5f);
  int fill = f.maxHp ? hp * w / f.maxHp : 0;
  uint16_t c = hp * 2 > f.maxHp ? rgb565(90, 220, 120) : hp * 5 > f.maxHp ? GOLD : RED;
  tft.fillRect(x, y, w, 7, shade565(PANEL, 50));
  tft.fillRect(x, y, fill, 7, c);
  char buf[16];
  snprintf(buf, sizeof(buf), "%3d/%u", hp, f.maxHp);
  text(buf, PNL_X[side] + PNL_W - 8, PNL_Y[side] + 28, 1, INK, MR_DATUM, PANEL);
}

void drawFighterPanel(int side) {
  const Fighter& f = battle.f[side];
  int x = PNL_X[side], y = PNL_Y[side];
  tft.fillRoundRect(x, y, PNL_W, PNL_H, 8, PANEL);
  tft.fillRect(x + 8, y + PNL_H - 6, PNL_W - 16, 3, elementColor(f.mon.element));
  text(f.mon.name, x + 8, y + 11, 2, INK, ML_DATUM, PANEL);
  char buf[24];
  snprintf(buf, sizeof(buf), "Lv%u %s", f.level, elementName(f.mon.element));
  text(buf, x + 8, y + 28, 1, side ? DIM : MINT, ML_DATUM, PANEL);
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
  tft.fillRect(0, 210, 240, 32, BG);
  if (line1) text(line1, 120, 218, 2, c1);
  if (line2) text(line2, 120, 234, 2, c2);
}

void enterBattle() {
  Owned o = collectionAt(detailIndex);
  Monster mine = makeMonster(o.seed, ownedNight(o));
  Monster foe  = makeMonster(esp_random(), false);
  battleBegin(battle, mine, o.level, foe, opponentLevel(o.level, esp_random()), esp_random());
  shownHp[0] = battle.f[0].hp;
  shownHp[1] = battle.f[1].hp;
  bPhase = BP_CHOOSE;
  bMove[0] = bMove[1] = NO_MOVE;
  battleFled = false;
  if (!haveBspr) {
    bspr.setColorDepth(16);
    haveBspr = bspr.createSprite(BSPR, BSPR) != nullptr;
  }

  ledForElement(foe.element);
  sfxFound();
  tft.fillScreen(BG);
  starfield(foe.seed);
  text("A wild buddy wants to battle!", 120, 10, 1, GOLD);
  drawFighterPanel(1);
  drawFighterPanel(0);
  if (!haveBspr) {   // no memory for the sprite: draw once, no animation
    drawMonster(tft, battle.f[0].mon, MON_X[0], MON_Y[0], 72);
    drawMonster(tft, battle.f[1].mon, MON_X[1], MON_Y[1], 72);
  }
  battleMessage("Pick a throw!", INK, nullptr, INK);
  drawMoveButtons(true);
}

// Draws one fighter; the side that just lost the throw shakes and flashes.
void drawBattler(int side, uint32_t t) {
  if (!haveBspr) return;
  const Fighter& f = battle.f[side];
  int dx = 0;
  uint16_t bg = BG;
  bool hurt = bPhase == BP_RESULT && lastTurn.damage[side] > 0;
  uint32_t since = millis() - bPhaseAt;
  if (hurt && since < 450) {
    dx = (since / 45) % 2 ? 4 : -4;
    if (since < 140) bg = shade565(RED, 45);
  }
  bool down = f.hp == 0 && bPhase == BP_RESULT && since >= 450;
  int bob = down ? 10 : (int)(sinf((t + side * 700) / 260.0f) * 3);
  bool blink = down || ((t + side * 1100) % 3200) < 140;
  bspr.fillSprite(bg);
  drawMonster(bspr, f.mon, BSPR / 2 + dx, BSPR / 2 + bob, down ? 60 : 72, blink);
  bspr.pushSprite(MON_X[side] - BSPR / 2, MON_Y[side] - BSPR / 2);
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

void updateBattle(uint32_t t) {
  drawBattler(1, t);
  drawBattler(0, t);
  for (int s = 0; s < 2; s++) {   // HP bars slide down instead of jumping
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
        go(S_BATTLE_END);
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
        if (since > 1900) go(S_BATTLE_END);
        return;
      }
      if (since < 1500) return;
      ledForElement(battle.f[1].mon.element);
      bPhase = BP_CHOOSE;
      bMove[0] = NO_MOVE;
      touchTapped(x, y);   // ignore taps made during the reveal
      battleMessage("Pick a throw!", INK, nullptr, INK);
      drawMoveButtons(true);
      return;
  }
}

// ---------------------------------------------------------------- battle end
const Btn B_REMATCH {10, 270, 130, 40, "Battle again", GO, 2};
const Btn B_BDONE   {150, 270, 80, 40, "Back", BTN2, 2};

void enterBattleEnd() {
  battleFreeSprite();
  bool won = !battleFled && battleWinner(battle) == 0;
  battleXpGained = 0;
  battleLevelUp = false;
  if (won) {   // XP for the winner, same rules as re-capturing a buddy
    int idx = findOwned(battle.f[0].mon.seed, battle.f[0].mon.night);
    if (idx >= 0) {
      Owned o = collectionAt(idx);
      battleXpGained = battleXp(battle.f[1]);
      o.xp += battleXpGained;
      uint8_t before = o.level;
      while (o.level < 99 && o.xp >= xpForNext(o.level)) { o.xp -= xpForNext(o.level); o.level++; }
      updateOwned(idx, o);
      battleLevelUp = o.level > before;
      detailIndex = idx;
    }
  }
  tft.fillScreen(BG);
  starfield(battle.f[1].mon.seed ^ 99);
  char buf[48];
  if (battleFled) {
    ledOff();
    text("Got away", 120, 24, 4, DIM);
    snprintf(buf, sizeof(buf), "You left %s behind", battle.f[1].mon.name);
    text(buf, 120, 228, 2, INK);
  } else if (won) {
    sfxCaught();
    ledColor(0, 1, 0);
    text("Victory!", 120, 24, 4, MINT);
    snprintf(buf, sizeof(buf), "%s beat %s (Lv %u)", battle.f[0].mon.name, battle.f[1].mon.name, battle.f[1].level);
    text(buf, 120, 222, 2, INK);
    Owned o = collectionAt(detailIndex);
    if (battleLevelUp) snprintf(buf, sizeof(buf), "+%u XP - Level up! Now Lv %u", battleXpGained, o.level);
    else               snprintf(buf, sizeof(buf), "+%u XP  (%u/%u)", battleXpGained, o.xp, xpForNext(o.level));
    text(buf, 120, 244, 2, battleLevelUp ? GOLD : MINT);
  } else {
    sfxFled();
    ledColor(1, 0, 0);
    text("Defeated", 120, 24, 4, RED);
    snprintf(buf, sizeof(buf), "%s was too strong", battle.f[1].mon.name);
    text(buf, 120, 222, 2, INK);
    text("Level up your buddy and try again", 120, 244, 1, DIM);
  }
  drawBtn(B_REMATCH);
  drawBtn(B_BDONE);
}

void updateBattleEnd(uint32_t t) {
  // Winner (or your buddy, if you ran) takes the stage.
  bool lost = !battleFled && battleWinner(battle) == 1;
  showMonster(battle.f[lost ? 1 : 0].mon, 120, 128, 110, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_REMATCH, x, y)) { sfxClick(); go(S_BATTLE); }
  else if (hit(B_BDONE, x, y)) { sfxClick(); go(S_DETAIL); }
}

// ---------------------------------------------------------------- touch test
const Btn B_EXIT {80, 140, 80, 36, "Exit", BTN2, 2};

void enterTouchTest() {
  tft.fillScreen(TFT_BLACK);
  text("Touch test: tap around", 120, 12, 2, INK, MC_DATUM, TFT_BLACK);
  text("dots should land under your finger", 120, 30, 1, DIM, MC_DATUM, TFT_BLACK);
  tft.drawRect(0, 0, 240, 320, DIM);
  drawBtn(B_EXIT);
}

void updateTouchTest(uint32_t) {
  int x, y;
  if (touchTapped(x, y) && hit(B_EXIT, x, y)) { go(S_TITLE); return; }
  if (touchDown(x, y)) {
    tft.fillCircle(x, y, 3, GOLD);
    char buf[24];
    snprintf(buf, sizeof(buf), "  x=%3d y=%3d  ", x, y);
    text(buf, 120, 304, 2, INK, MC_DATUM, TFT_BLACK);
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
    case S_ENCOUNTER:  enterEncounter(); break;
    case S_CAPTURE:    enterCapture(); break;
    case S_RESULT:     enterResult(); break;
    case S_COLLECTION: enterCollection(); break;
    case S_DETAIL:     enterDetail(); break;
    case S_TOUCHTEST:  enterTouchTest(); break;
    case S_BATTLE:     enterBattle(); break;
    case S_BATTLE_END: enterBattleEnd(); break;
  }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(0);   // portrait, like a handheld
  tft.fillScreen(BG);
  spr.setColorDepth(16);
  haveSprite = spr.createSprite(SPR, SPR) != nullptr;
  hwBegin();
  storageBegin();
  setSoundOn(loadSoundSetting());
  text("Waking up the radio...", 120, 160, 2, DIM);
  scannerBegin();
  Serial.printf("BattleBuddies v0.1  collection=%u  scans=%u  light=%d\n",
                (unsigned)collectionSize(), (unsigned)totalScans(), lightReading());
  go(S_TITLE);
}

void loop() {
  uint32_t t = millis() - screenStart;
  switch (screen) {
    case S_TITLE:      updateTitle(t); break;
    case S_SCAN:       updateScan(t); break;
    case S_ENCOUNTER:  updateEncounter(t); break;
    case S_CAPTURE:    updateCapture(t); break;
    case S_RESULT:     updateResult(t); break;
    case S_COLLECTION: updateCollection(t); break;
    case S_DETAIL:     updateDetail(t); break;
    case S_TOUCHTEST:  updateTouchTest(t); break;
    case S_BATTLE:     updateBattle(t); break;
    case S_BATTLE_END: updateBattleEnd(t); break;
  }
  delay(16);
}
