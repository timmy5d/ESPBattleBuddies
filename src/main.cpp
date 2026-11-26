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
#include "link.h"
#include "splash_logo.h"

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
enum Screen { S_TITLE, S_SCAN, S_PICK, S_ENCOUNTER, S_RESULT, S_COLLECTION, S_DETAIL, S_TOUCHTEST,
              S_FIGHTER, S_BATTLE, S_BOT_INTRO, S_BOT_ROUND, S_FRIEND_FIND, S_FRIEND_WAIT, S_FRIEND_END };
Screen screen = S_TITLE;
uint32_t screenStart = 0;
void go(Screen s);

Monster wild;
uint8_t wildLv = 1, wildSource = SRC_WIFI;
int8_t  wildRssi = 0;
enum Outcome { OUT_CAUGHT, OUT_LEVELUP, OUT_FLED, OUT_FULL, OUT_LOST, OUT_STARTER } outcome;
uint8_t resultLevel = 1;
uint16_t fighterXpGained = 0;   // XP your fighter earned in the last wild battle
bool     fighterLevelUp = false;
int collectionPage = 0;
int detailIndex = 0;
bool releaseArmed = false;

const Monster MASCOT = makeMonster(0x0B0DD1E5u, false);

// ---------------------------------------------------------------- title
const Btn B_WIFI  {14, 214, 102, 44, "WiFi scan", GO, 2};
const Btn B_BT    {124, 214, 102, 44, "Bluetooth", rgb565(60, 120, 230), 2};
ScanMode scanMode = MODE_WIFI;
const Btn B_COLL  {6, 266, 74, 30, "Collection", BTN, 2};
const Btn B_BOT   {83, 266, 74, 30, "Bot 9000", rgb565(200, 70, 70), 2};
const Btn B_FRIEND{160, 266, 74, 30, "Friend", rgb565(170, 90, 210), 2};
bool scanForBot = false;   // the next scan builds Battle Bot 9000's team
void startBotMatch();      // battle bot section
void startFriendMatch();   // friend battle section
const Btn B_SOUND {6, 300, 74, 18, "", BTN2, 1};
const Btn B_TEST  {214, 300, 20, 18, "?", BTN2, 1};

void drawSoundBtn() { drawBtn(B_SOUND, soundOn() ? "Sound: on" : "Sound: off"); }

void enterTitle() {
  ledOff();
  tft.fillScreen(BG);
  starfield(1234);
  text("BATTLE", 120, 24, 4, GOLD);
  text("BUDDIES", 120, 50, 4, MINT);
  drawBtn(B_WIFI);
  drawBtn(B_BT);
  drawBtn(B_COLL);
  drawBtn(B_BOT);
  drawBtn(B_FRIEND);
  drawSoundBtn();
  drawBtn(B_TEST);
  text("v0.1", 160, 309, 1, DIM);
}

void updateTitle(uint32_t t) {
  showMonster(MASCOT, 120, 136, 100, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_WIFI, x, y))      { sfxClick(); scanMode = MODE_WIFI; scanForBot = false; go(S_SCAN); }
  else if (hit(B_BT, x, y))   { sfxClick(); scanMode = MODE_BLE; scanForBot = false; go(S_SCAN); }
  else if (hit(B_COLL, x, y)) { sfxClick(); collectionPage = 0; go(S_COLLECTION); }
  else if (hit(B_BOT, x, y)) {
    sfxClick();
    if (collectionSize() == 0) { drawBtn(B_BOT, "No buddy"); return; }   // nothing to fight with yet
    startBotMatch();
  }
  else if (hit(B_FRIEND, x, y)) {
    sfxClick();
    if (collectionSize() == 0) { drawBtn(B_FRIEND, "No buddy"); return; }
    startFriendMatch();
  }
  else if (hit(B_SOUND, x, y)) { setSoundOn(!soundOn()); saveSoundSetting(soundOn()); sfxClick(); drawSoundBtn(); }
  else if (hit(B_TEST, x, y)) { go(S_TOUCHTEST); }
}

// ---------------------------------------------------------------- scanning
uint32_t lastPing = 0;

// Real signals shown on the radar once a scan is done (see the pick screen).
const int MAX_BLIPS = 16;
struct Blip { int16_t x, y; uint16_t color; bool owned; };
Blip blips[MAX_BLIPS];
int blipCount = 0;      // 0 while scanning: the radar shows decoy blips instead
int pickSel = -1;       // selected blip, -1 = none
bool pickNight = false;
bool encounterFromPick = false;   // "Run" goes back to the radar instead of home
const int RADAR_CX = 120, RADAR_CY = 150, RADAR_R = 70;

void enterScan() {
  tft.fillScreen(BG);
  if (scanForBot) {
    text("BOT 9000", 120, 22, 4, RED);
    text("is scanning for its team...", 120, 48, 2, DIM);
  } else {
    text("SCANNING", 120, 22, 4, GOLD);
    text(scanMode == MODE_WIFI ? "Looking for WiFi networks" : "Looking for Bluetooth gadgets", 120, 48, 2, DIM);
  }
  blipCount = 0;
  pickSel = -1;
  encounterFromPick = false;
  scannerStart(scanForBot ? MODE_BOTH : scanMode);
  countScan();
  lastPing = 0;
}

void drawRadar(uint32_t t) {
  if (!haveSprite) return;
  const int c = SPR / 2, R = RADAR_R;
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
  if (blipCount == 0) {
    uint32_t s = 0xC0FFEEu ^ (t / 900);   // a few blips that change now and then
    for (int i = 0; i < 4; i++) {
      s ^= s << 13; s ^= s >> 17; s ^= s << 5;
      float ba = (s % 628) / 100.0f, br = 15 + (s >> 10) % (R - 20);
      spr.fillCircle(c + cosf(ba) * br, c + sinf(ba) * br, 3, rgb565(180, 255, 200));
    }
  } else {
    // Weakest first, so the strong (close) ones are drawn on top.
    for (int i = blipCount - 1; i >= 0; i--) {
      const Blip& b = blips[i];
      int bx = b.x - (RADAR_CX - c), by = b.y - (RADAR_CY - c);
      spr.fillCircle(bx, by, 5, b.color);
      spr.drawCircle(bx, by, 5, b.owned ? INK : shade565(b.color, 50));
    }
    if (pickSel >= 0) {   // pulsing ring on the chosen one
      const Blip& b = blips[pickSel];
      int bx = b.x - (RADAR_CX - c), by = b.y - (RADAR_CY - c);
      int pr = 8 + (t / 120) % 4;
      spr.drawCircle(bx, by, pr, GOLD);
      spr.drawCircle(bx, by, pr + 1, GOLD);
    }
  }
  spr.pushSprite(RADAR_CX - c, RADAR_CY - c);
}

void updateScan(uint32_t t) {
  ScanPhase p = scannerPoll();
  drawRadar(t);
  if (t - lastPing > 1050) { sfxScanPing(); lastPing = t; }
  text(p == SCAN_WIFI ? "  Listening to WiFi...  " : "Listening to Bluetooth...", 120, 250, 2, INK);
  if (p == SCAN_DONE && t >= WILD_SCAN_MIN_MS) {
    ScanResult r = scannerResult();
    char buf[40];
    if (scanForBot)                 snprintf(buf, sizeof(buf), "%u WiFi + %u Bluetooth signals", r.wifiCount, r.bleCount);
    else if (scanMode == MODE_WIFI) snprintf(buf, sizeof(buf), "%u WiFi networks nearby", r.wifiCount);
    else                            snprintf(buf, sizeof(buf), "%u Bluetooth gadgets nearby", r.bleCount);
    text(buf, 120, 272, 2, DIM);
    delay(500);
    if (scanForBot) { go(S_BOT_INTRO); return; }
    pickNight = r.night;
    if (scannerSignalCount() > 0) { go(S_PICK); return; }
    // Nothing heard at all: straight to a buddy "in the static".
    wild = makeMonster(r.seed, r.night);
    wildLv = wildLevel(wild);
    wildSource = r.source;
    wildRssi = r.rssi;
    go(S_ENCOUNTER);
  }
}

// ---------------------------------------------------------------- pick a signal
// After a scan, every signal heard becomes a blip on the radar: closer to the
// middle = stronger signal, colour = the element of the buddy living there,
// white ring = you already own it. Tap a blip, then "Track it!".
const Btn B_TRACK {10, 272, 140, 40, "Track it!", GO, 4};
const Btn B_PBACK {160, 272, 70, 40, "Back", BTN2, 2};

void placeBlips() {
  blipCount = min(MAX_BLIPS, scannerSignalCount());
  for (int i = 0; i < blipCount; i++) {
    ScanSignal s = scannerSignal(i);
    Monster m = makeMonster(s.seed, pickNight);
    // Strength -30 dBm (right here) .. -95 dBm (far away) maps to the radius.
    int r = map(constrain(s.rssi, -95, -30), -30, -95, 10, RADAR_R - 8);
    float a = (s.seed % 628) / 100.0f;   // the same signal always sits at the same bearing
    blips[i].x = RADAR_CX + (int)(cosf(a) * r);
    blips[i].y = RADAR_CY + (int)(sinf(a) * r);
    blips[i].color = elementColor(m.element);
    blips[i].owned = findOwned(s.seed, pickNight) >= 0;
  }
}

void drawPickInfo() {
  tft.fillRect(0, 230, 240, 38, BG);
  if (pickSel < 0) {
    text("Tap a blip to pick a signal", 120, 240, 2, INK);
    text("middle = strongest, colour = element", 120, 258, 1, DIM);
    return;
  }
  ScanSignal s = scannerSignal(pickSel);
  char name[24];
  if (s.name[0]) snprintf(name, sizeof(name), "%s", s.name);
  else           snprintf(name, sizeof(name), s.source == SRC_WIFI ? "(hidden network)" : "(unnamed gadget)");
  text(name, 120, 240, 2, INK);
  char buf[40];
  snprintf(buf, sizeof(buf), "%d dBm%s", s.rssi, blips[pickSel].owned ? "  -  already on your team" : "");
  text(buf, 120, 258, 1, DIM);
}

void drawTrackBtn() {
  if (pickSel >= 0) drawBtn(B_TRACK);
  else {
    Btn off = B_TRACK; off.color = shade565(BTN2, 70);
    drawBtn(off);
  }
}

void enterPick() {
  tft.fillScreen(BG);
  text("PICK A SIGNAL", 120, 22, 4, GOLD);
  char buf[40];
  int n = scannerSignalCount();
  if (scanMode == MODE_WIFI) snprintf(buf, sizeof(buf), "%d WiFi network%s found", n, n == 1 ? "" : "s");
  else                       snprintf(buf, sizeof(buf), "%d Bluetooth gadget%s found", n, n == 1 ? "" : "s");
  if (n > MAX_BLIPS) snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " (%d closest)", MAX_BLIPS);
  text(buf, 120, 48, 2, DIM);
  placeBlips();
  if (pickSel >= blipCount) pickSel = -1;
  if (!haveSprite) {   // no sprite memory: draw the blips straight on the screen once
    for (int i = 0; i < blipCount; i++) tft.fillCircle(blips[i].x, blips[i].y, 5, blips[i].color);
  }
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
  // Nearest blip within reach of the finger.
  int best = -1, bestD = 18 * 18;
  for (int i = 0; i < blipCount; i++) {
    int dx = blips[i].x - x, dy = blips[i].y - y, d = dx * dx + dy * dy;
    if (d < bestD) { bestD = d; best = i; }
  }
  if (best < 0) return;
  sfxClick();
  if (best == pickSel) { trackSelected(); return; }   // second tap on the same blip tracks it
  pickSel = best;
  ledColor(0, 1, 0);
  drawPickInfo();
  drawTrackBtn();
}

// ---------------------------------------------------------------- encounter
// Wild buddies are caught by beating them in a battle, the same battle you
// fight against Battle Bot 9000 (and, later, a friend's board). With an empty
// collection the first buddy you meet joins you for free as your starter.
const Btn B_CATCH {10, 272, 140, 40, "BATTLE!", GO, 4};
const Btn B_RUN   {160, 272, 70, 40, "Run", BTN2, 2};

enum BattleKind : uint8_t { BK_WILD, BK_BOT, BK_FRIEND };
BattleKind battleKind = BK_WILD;
void startWildBattle();   // fighter picker section

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
  if (collectionSize() == 0) {
    text("It wants to be your first buddy!", 120, 182, 1, MINT);
    drawBtn(B_CATCH, "Befriend");
  } else {
    text("Beat it in battle to catch it", 120, 182, 1, DIM);
    drawBtn(B_CATCH);
  }
  drawBtn(B_RUN);
}

void updateEncounter(uint32_t t) {
  showMonster(wild, 120, 108, 104, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_CATCH, x, y)) {
    sfxClick();
    if (collectionSize() == 0) {   // free starter
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

// Adds XP to a collection buddy; returns true if it levelled up.
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
const Btn B_AGAIN {10, 270, 130, 40, "Scan again", GO, 2};   // same mode as last time
const Btn B_HOME  {150, 270, 80, 40, "Home", BTN2, 2};
int fighterIndex = 0;   // which collection buddy is (or was) fighting

void enterResult() {
  tft.fillScreen(BG);
  starfield(wild.seed ^ 77);
  char buf[48];
  switch (outcome) {
    case OUT_STARTER:
      text("New friend!", 120, 24, 4, MINT);
      snprintf(buf, sizeof(buf), "%s is your first buddy (Lv %u)", wild.name, resultLevel);
      break;
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
    case OUT_LOST:
      text("It got away", 120, 24, 4, RED);
      snprintf(buf, sizeof(buf), "%s won and ran off", wild.name);
      break;
    default:
      text("It got away", 120, 24, 4, RED);
      snprintf(buf, sizeof(buf), "%s ran back into the signals", wild.name);
      break;
  }
  text(buf, 120, 232, 2, INK);
  if (fighterXpGained && fighterIndex < (int)collectionSize()) {
    Owned o = collectionAt(fighterIndex);
    Monster m = makeMonster(o.seed, ownedNight(o));
    if (fighterLevelUp) snprintf(buf, sizeof(buf), "%s +%u XP - Level up! Lv %u", m.name, fighterXpGained, o.level);
    else                snprintf(buf, sizeof(buf), "%s +%u XP", m.name, fighterXpGained);
    text(buf, 120, 252, 1, fighterLevelUp ? GOLD : MINT);
  }
  drawBtn(B_AGAIN);
  drawBtn(B_HOME);
}

void updateResult(uint32_t t) {
  if (outcome != OUT_FLED && outcome != OUT_LOST) showMonster(wild, 120, 134, 110, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_AGAIN, x, y)) { sfxClick(); scanForBot = false; go(S_SCAN); }
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
const Btn B_DBACK {10, 284, 100, 30, "Back", BTN2, 2};
const Btn B_FREE  {130, 284, 100, 30, "Release", RED, 2};

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
  drawBtn(B_FREE);
}

void updateDetail(uint32_t t) {
  showMonster(detailMon, 120, 92, 120, t);
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
// The computer opponent. Every match it runs a fresh WiFi + Bluetooth scan and
// builds a brand-new team of three buddies from the real signals around it,
// levelled near your best buddies. You send in one buddy per round; a buddy
// that gets knocked out sits out the rest of the match.
const int BOT_TEAM = 3;
Monster botTeam[BOT_TEAM];
uint8_t botLv[BOT_TEAM];
int     botNext = 0;            // index of Bot's next (or current) buddy
int     botSignals = 0;         // signals its team was built from
int     roundsPlayed = 0;
int8_t  roundWinner = -1;       // 0 you, 1 Bot, -1 you gave up
uint16_t roundXp = 0;
bool    roundLevelUp = false;
bool    koOut[MAX_COLLECTION];  // knocked out this match
bool    fought[MAX_COLLECTION]; // took part this match
const uint16_t BOT_RED = rgb565(200, 70, 70);

void startBotMatch() {
  memset(koOut, 0, sizeof(koOut));
  memset(fought, 0, sizeof(fought));
  botNext = 0;
  roundsPlayed = 0;
  scanForBot = true;
  go(S_SCAN);
}

// Average of your three highest levels: Bot scales its team to that.
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
    if (i < n) {   // a different real signal for each team member
      int pick;
      do { pick = esp_random() % n; } while ((pick == used[0] || pick == used[1]) && n > i);
      used[i] = pick;
      seed = scannerSignal(pick).seed;
    } else {
      seed = esp_random();   // not enough signals: one from the static
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

// The robot himself: a boxy steel head with glowing eyes and an antenna.
void drawBot(int cx, int cy, int s, bool antennaOn) {
  const uint16_t steel = rgb565(150, 160, 180), dark = rgb565(70, 78, 98);
  int w = s, h = s * 4 / 5, x = cx - w / 2, y = cy - h / 2;
  tft.drawFastVLine(cx, y - s / 4, s / 4, dark);                     // antenna
  tft.fillCircle(cx, y - s / 4, s / 14 + 2, antennaOn ? RED : shade565(RED, 40));
  tft.fillRect(x - s / 10, cy - s / 8, s / 10, s / 4, dark);         // ear bolts
  tft.fillRect(x + w, cy - s / 8, s / 10, s / 4, dark);
  tft.fillRoundRect(x, y, w, h, s / 8, steel);
  tft.drawRoundRect(x, y, w, h, s / 8, dark);
  tft.fillRoundRect(x + s / 10, y + s / 10, w - s / 5, h * 9 / 20, s / 12, rgb565(20, 26, 44));   // visor
  int ey = y + s / 10 + h * 9 / 40, er = s / 11;
  tft.fillCircle(cx - w / 4 + s / 20, ey, er, rgb565(80, 230, 255));   // eyes
  tft.fillCircle(cx + w / 4 - s / 20, ey, er, rgb565(80, 230, 255));
  tft.fillCircle(cx - w / 4 + s / 20, ey, er / 2, INK);
  tft.fillCircle(cx + w / 4 - s / 20, ey, er / 2, INK);
  int gy = y + h * 7 / 10, gw = w / 2, gh = h / 6;                    // mouth grille
  tft.fillRoundRect(cx - gw / 2, gy, gw, gh, 3, dark);
  for (int i = 1; i < 5; i++) tft.drawFastVLine(cx - gw / 2 + i * gw / 5, gy + 2, gh - 4, steel);
}

void drawBotBanner() {
  tft.fillRoundRect(6, 4, 228, 32, 8, BOT_RED);
  tft.drawRoundRect(6, 4, 228, 32, 8, shade565(BOT_RED, 150));
  text("BATTLE BOT 9000", 120, 21, 4, INK, MC_DATUM, BOT_RED);
}

// ---------------------------------------------------------------- fighter picker
// Your collection, in "choose a fighter" mode. Knocked-out buddies (in a Bot
// match) are greyed out. With only one buddy able to fight it goes straight in.
Monster foeMon;
uint8_t foeLv = 1;
const Btn B_FCANCEL {70, 282, 100, 34, "Cancel", BTN2, 2};

void fighterChosen() {
  if (battleKind == BK_FRIEND) {   // tell the friend's board who's coming, then wait for theirs
    Owned o = collectionAt(fighterIndex);
    linkSendTeam(o.seed, ownedNight(o), o.level);
    go(S_FRIEND_WAIT);
  } else {
    go(S_BATTLE);
  }
}

void chooseFighter() {
  int only = -1, n = 0;
  for (size_t i = 0; i < collectionSize(); i++)
    if (!(battleKind == BK_BOT && koOut[i])) { only = i; n++; }
  if (n == 1) { fighterIndex = only; fighterChosen(); return; }
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
  tft.fillScreen(BG);
  collectionPage = constrain(collectionPage, 0, pageCount() - 1);
  text("Pick a fighter", 8, 14, 4, GOLD, ML_DATUM);
  char buf[40];
  if (battleKind == BK_FRIEND) snprintf(buf, sizeof(buf), "Friend battle - your friend is picking too");
  else snprintf(buf, sizeof(buf), "vs %s  Lv %u  %s", foeMon.name, foeLv, elementName(foeMon.element));
  text(buf, 120, 32, 1, DIM);
  for (int i = 0; i < GRID; i++) {
    size_t idx = collectionPage * GRID + i;
    if (idx >= collectionSize()) break;
    int col = i % 3, row = i / 3;
    int x = 4 + col * 78, y = 40 + row * 80;
    Owned o = collectionAt(idx);
    Monster m = makeMonster(o.seed, ownedNight(o));
    bool out = battleKind == BK_BOT && koOut[idx];
    uint16_t cellBg = out ? shade565(PANEL, 50) : PANEL;
    tft.fillRoundRect(x, y, 76, 78, 8, cellBg);
    tft.fillRect(x + 6, y + 74, 64, 3, out ? DIM : elementColor(m.element));
    drawMonster(tft, m, x + 38, y + 32, 48);
    text(m.name, x + 38, y + 62, 1, out ? DIM : INK, MC_DATUM, cellBg);
    snprintf(buf, sizeof(buf), "Lv%u", o.level);
    text(buf, x + 5, y + 8, 1, DIM, ML_DATUM, cellBg);
    if (out) {
      tft.fillRoundRect(x + 46, y + 2, 28, 14, 7, RED);
      text("KO", x + 60, y + 9, 1, INK, MC_DATUM, RED);
    }
  }
  drawBtn(B_PREV);
  drawBtn(B_FCANCEL);
  drawBtn(B_NEXT);
}

void cancelFight() {
  if (battleKind == BK_WILD) go(S_ENCOUNTER);
  else if (battleKind == BK_FRIEND) { linkStop(true); go(S_TITLE); }
  else { roundWinner = -1; go(S_BOT_ROUND); }   // giving up the Bot match
}

void updateFighter(uint32_t) {
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_FCANCEL, x, y)) { sfxClick(); cancelFight(); return; }
  if (hit(B_PREV, x, y)) { sfxClick(); collectionPage = (collectionPage + pageCount() - 1) % pageCount(); go(S_FIGHTER); return; }
  if (hit(B_NEXT, x, y)) { sfxClick(); collectionPage = (collectionPage + 1) % pageCount(); go(S_FIGHTER); return; }
  if (y >= 40 && y < 40 + 3 * 80) {
    int col = min(2, (x - 4) / 78), row = (y - 40) / 80;
    size_t idx = collectionPage * GRID + row * 3 + col;
    if (idx >= collectionSize()) return;
    if (battleKind == BK_BOT && koOut[idx]) { sfxMiss(); return; }
    sfxClick();
    fighterIndex = idx;
    fighterChosen();
  }
}

// ---------------------------------------------------------------- battle
// One battle screen for every opponent: wild buddies (win = capture),
// Battle Bot 9000's team, and later a friend's board over ESP-NOW.
// The rules live in battle.*; this section only draws and reads taps.
// The battle borrows a 96x96 sprite while it runs and frees it afterwards.
const int BSPR = 96;
TFT_eSprite bspr(&tft);
bool haveBspr = false;

Battle battle;
TurnResult lastTurn;
enum BattlePhase { BP_CHOOSE, BP_WAIT, BP_REVEAL, BP_RESULT } bPhase;   // BP_WAIT: friend's throw not in yet
uint32_t bPhaseAt;
uint8_t  bMove[2];
float    shownHp[2];
bool     battleFled;

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
  Owned o = collectionAt(fighterIndex);
  Monster mine = makeMonster(o.seed, ownedNight(o));
  // A friend battle uses a seed both boards share, so both compute the same damage.
  battleBegin(battle, mine, o.level, foeMon, foeLv, battleKind == BK_FRIEND ? linkBattleSeed() : esp_random());
  shownHp[0] = battle.f[0].hp;
  shownHp[1] = battle.f[1].hp;
  bPhase = BP_CHOOSE;
  bMove[0] = bMove[1] = NO_MOVE;
  battleFled = false;
  if (!haveBspr) {
    bspr.setColorDepth(16);
    haveBspr = bspr.createSprite(BSPR, BSPR) != nullptr;
  }

  ledForElement(foeMon.element);
  sfxFound();
  tft.fillScreen(BG);
  starfield(foeMon.seed);
  char buf[48];
  if (battleKind == BK_BOT) {
    snprintf(buf, sizeof(buf), "BOT 9000 sends out %s! (%d/%d)", foeMon.name, botNext + 1, BOT_TEAM);
    text(buf, 120, 10, 1, BOT_RED);
  } else if (battleKind == BK_FRIEND) {
    snprintf(buf, sizeof(buf), "Friend battle: your friend sent %s!", foeMon.name);
    text(buf, 120, 10, 1, rgb565(200, 150, 255));
  } else {
    snprintf(buf, sizeof(buf), "Win to catch %s!", foeMon.name);
    text(buf, 120, 10, 1, GOLD);
  }
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

// How a friend battle ended (friend battle section draws it).
enum FriendEnd : uint8_t { FE_WON, FE_LOST, FE_YOU_LEFT, FE_FRIEND_LEFT, FE_LINK_LOST, FE_CHEAT } friendEnd;
uint16_t friendXp = 0;
bool friendLevelUp = false;

// The battle is over (or you ran): hand the result to whoever started it.
void finishBattle() {
  battleFreeSprite();
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
      if (idx >= 0) {   // already on your team: that buddy gets stronger instead
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

  if (battleKind == BK_FRIEND) {
    friendXp = xp;
    friendLevelUp = lvUp;
    friendEnd = battleFled ? FE_YOU_LEFT : won ? FE_WON : FE_LOST;
    go(S_FRIEND_END);
    return;
  }

  // Battle Bot 9000 round.
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
        if (battleKind == BK_FRIEND) linkStop(true);   // your friend wins
        battleFled = true;
        finishBattle();
        return;
      }
      for (uint8_t m = 0; m < MOVE_COUNT; m++) {
        if (!hit(B_MOVE[m], x, y)) continue;
        sfxClick();
        bMove[0] = m;
        if (battleKind == BK_FRIEND) {   // lock in my throw, then wait for my friend's
          linkCommit(battle.turn, m);
          drawMoveButtons(false);
          battleMessage("Throw locked in!", MINT, "Waiting for your friend...", DIM);
          bPhase = BP_WAIT;
          bPhaseAt = now;
          return;
        }
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

    case BP_WAIT: {   // friend battle: both throws are locked in once the reveal arrives
      uint8_t theirs;
      if (!linkTurnReady(battle.turn, theirs)) return;
      bMove[1] = theirs;
      char buf[40];
      snprintf(buf, sizeof(buf), "You: %s   Friend: %s", moveName(bMove[0]), moveName(bMove[1]));
      battleMessage("Ready... throw!", DIM, buf, INK);
      bPhase = BP_REVEAL;
      bPhaseAt = now;
      return;
    }

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
      touchTapped(x, y);   // ignore taps made during the reveal
      battleMessage("Pick a throw!", INK, nullptr, INK);
      drawMoveButtons(true);
      return;
  }
}

// ---------------------------------------------------------------- battle bot 9000 (screens)
const Btn B_BOTGO   {10, 272, 140, 40, "Fight!", GO, 4};
const Btn B_BOTBACK {160, 272, 70, 40, "Back", BTN2, 2};
const Btn B_NEXTRND {10, 272, 140, 40, "Next round", GO, 2};
const Btn B_GIVEUP  {160, 272, 70, 40, "Give up", BTN2, 2};
const Btn B_REMATCH {10, 272, 140, 40, "Rematch", GO, 2};
bool botAntenna = false;
uint32_t botBlinkAt = 0;

void drawTeamDots(int y) {   // Bot's three buddies: bright = still standing
  for (int i = 0; i < BOT_TEAM; i++) {
    int x = 200 + (i - 1) * 20;
    if (i < botNext) { tft.fillCircle(x, y, 7, shade565(DIM, 40)); text("x", x, y, 1, RED, MC_DATUM, shade565(DIM, 40)); }
    else tft.fillCircle(x, y, 7, elementColor(botTeam[i].element));
  }
}

void enterBotIntro() {
  buildBotTeam();
  ledColor(1, 0, 0);
  sfxFound();
  tft.fillScreen(BG);
  starfield(botTeam[0].seed);
  drawBotBanner();
  drawBot(120, 100, 80, true);
  char buf[44];
  if (botSignals > 0) snprintf(buf, sizeof(buf), "Beep! My team comes from %d signals.", botSignals);
  else                snprintf(buf, sizeof(buf), "Beep! I pulled my team from static.");
  text(buf, 120, 150, 1, INK);
  text("Beat all three to win!", 120, 162, 1, DIM);
  for (int i = 0; i < BOT_TEAM; i++) {
    int x = 4 + i * 78, y = 174;
    tft.fillRoundRect(x, y, 76, 92, 8, PANEL);
    tft.fillRect(x + 6, y + 88, 64, 3, elementColor(botTeam[i].element));
    drawMonster(tft, botTeam[i], x + 38, y + 36, 50);
    text(botTeam[i].name, x + 38, y + 68, 1, INK, MC_DATUM, PANEL);
    snprintf(buf, sizeof(buf), "Lv%u %s", botLv[i], elementName(botTeam[i].element));
    text(buf, x + 38, y + 80, 1, DIM, MC_DATUM, PANEL);
  }
  drawBtn(B_BOTGO);
  drawBtn(B_BOTBACK);
  botBlinkAt = millis();
}

void blinkAntenna() {   // the antenna light pulses while Bot waits
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
  tft.fillCircle(120, 100 - 32 - 20, 80 / 14 + 2, botAntenna ? RED : shade565(RED, 40));   // matches drawBot(120, 100, 80)
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_BOTGO, x, y)) { sfxClick(); nextBotRound(); }
  else if (hit(B_BOTBACK, x, y)) { sfxClick(); go(S_TITLE); }
}

bool botMatchWon()  { return botNext >= BOT_TEAM; }
bool botMatchLost() { return roundWinner == -1 || fightersLeft() == 0; }

void enterBotRound() {
  tft.fillScreen(BG);
  drawBotBanner();
  char buf[48];
  if (botMatchWon()) {
    // Everyone who fought gets a bonus for beating the whole team.
    for (size_t i = 0; i < collectionSize(); i++) if (fought[i]) giveXp(i, 20);
    sfxCaught();
    ledColor(0, 1, 0);
    drawBot(120, 110, 80, false);
    text("You beat BOT 9000!", 120, 172, 4, MINT);
    text("Bzzt... system overload!", 120, 196, 2, DIM);
    text("+20 XP bonus for every buddy who fought", 120, 216, 1, GOLD);
    if (roundXp) {
      Owned o = collectionAt(fighterIndex);
      snprintf(buf, sizeof(buf), "%s +%u XP for the last round%s", makeMonster(o.seed, ownedNight(o)).name,
               roundXp, roundLevelUp ? " - Level up!" : "");
      text(buf, 120, 232, 1, roundLevelUp ? GOLD : MINT);
    }
    drawBtn(B_REMATCH);
    drawBtn(B_HOME);
    return;
  }
  if (botMatchLost()) {
    sfxFled();
    ledColor(1, 0, 0);
    drawBot(120, 110, 80, true);
    text("BOT 9000 wins", 120, 172, 4, RED);
    text(roundWinner == -1 ? "You gave up the match" : "All your fighters are knocked out", 120, 196, 2, INK);
    text("Beep boop. Level up and try again!", 120, 216, 1, DIM);
    drawBtn(B_REMATCH);
    drawBtn(B_HOME);
    return;
  }
  // Mid-match: who won the round, and who Bot sends next.
  // The buddy preview sprite covers y 66..216, so text sits above and below it.
  snprintf(buf, sizeof(buf), "Round %d done", roundsPlayed);
  text(buf, 10, 50, 2, GOLD, ML_DATUM);
  drawTeamDots(50);
  Owned o = collectionAt(fighterIndex);
  Monster mine = makeMonster(o.seed, ownedNight(o));
  if (roundWinner == 0) {
    ledColor(0, 1, 0);
    snprintf(buf, sizeof(buf), "%s won the round!", mine.name);
    text(buf, 120, 225, 2, MINT);
    snprintf(buf, sizeof(buf), "+%u XP%s", roundXp, roundLevelUp ? " - Level up!" : "");
    text(buf, 120, 241, 1, roundLevelUp ? GOLD : MINT);
  } else {
    ledColor(1, 0, 0);
    snprintf(buf, sizeof(buf), "%s was knocked out", mine.name);
    text(buf, 120, 225, 2, RED);
    snprintf(buf, sizeof(buf), "%d fighter%s left", fightersLeft(), fightersLeft() == 1 ? "" : "s");
    text(buf, 120, 241, 1, DIM);
  }
  snprintf(buf, sizeof(buf), "Next up: %s  Lv %u", botTeam[botNext].name, botLv[botNext]);
  text(buf, 120, 257, 2, INK);
  drawBtn(B_NEXTRND);
  drawBtn(B_GIVEUP);
}

void updateBotRound(uint32_t t) {
  bool over = botMatchWon() || botMatchLost();
  if (!over) showMonster(botTeam[botNext], 120, 141, 90, t);
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

// ---------------------------------------------------------------- friend battle
// Two boards battle over ESP-NOW (see link.h). Both players tap Friend, the
// boards find each other, each picks a buddy, and the shared battle engine
// plays out the same battle on both screens. Throws are locked in secretly
// (commit, then reveal), so nobody can peek. The winner's buddy gets XP.
const uint16_t FRIEND_PURPLE = rgb565(170, 90, 210);
const Btn B_FCANCEL2 {70, 272, 100, 40, "Cancel", BTN2, 2};
const Btn B_FAGAIN   {10, 272, 140, 40, "Battle again", GO, 2};
uint32_t pairedAt = 0;

void startFriendMatch() {
  battleKind = BK_FRIEND;
  linkStart();
  go(S_FRIEND_FIND);
}

void enterFriendFind() {
  ledColor(1, 0, 1);
  pairedAt = 0;
  blipCount = 0;
  tft.fillScreen(BG);
  text("FRIEND BATTLE", 120, 22, 4, FRIEND_PURPLE);
  text("Looking for another board...", 120, 48, 2, DIM);
  text("Tap Friend on both boards", 120, 240, 2, INK);
  text("Keep them close together", 120, 258, 1, DIM);
  drawBtn(B_FCANCEL2);
}

void updateFriendFind(uint32_t t) {
  drawRadar(t);
  if (t - lastPing > 1050) { sfxScanPing(); lastPing = t; }
  if (linkState() == LINK_PAIRED) {
    if (!pairedAt) {
      pairedAt = millis();
      sfxFound();
      ledColor(0, 1, 0);
      tft.fillRect(0, 230, 240, 36, BG);
      text("Found a friend!", 120, 246, 4, MINT);
    } else if (millis() - pairedAt > 900) {
      chooseFighter();
    }
    return;
  }
  int x, y;
  if (touchTapped(x, y) && hit(B_FCANCEL2, x, y)) { sfxClick(); linkStop(true); go(S_TITLE); }
}

void enterFriendWait() {
  tft.fillScreen(BG);
  text("FRIEND BATTLE", 120, 22, 4, FRIEND_PURPLE);
  Owned o = collectionAt(fighterIndex);
  Monster m = makeMonster(o.seed, ownedNight(o));
  char buf[40];
  snprintf(buf, sizeof(buf), "You sent %s (Lv %u)", m.name, o.level);
  text(buf, 120, 48, 2, INK);
  text("Waiting for your friend to pick...", 120, 240, 2, DIM);
  drawBtn(B_FCANCEL2);
}

void updateFriendWait(uint32_t t) {
  Owned o = collectionAt(fighterIndex);
  showMonster(makeMonster(o.seed, ownedNight(o)), 120, 145, 100, t);
  if (linkHaveTeam()) {
    foeMon = makeMonster(linkPeerSeed(), linkPeerNight());
    foeLv = linkPeerLevel();
    go(S_BATTLE);
    return;
  }
  int x, y;
  if (touchTapped(x, y) && hit(B_FCANCEL2, x, y)) { sfxClick(); linkStop(true); go(S_TITLE); }
}

void enterFriendEnd() {
  battleFreeSprite();
  tft.fillScreen(BG);
  starfield(foeMon.seed ^ 55);
  char buf[48];
  const char* title = "";
  uint16_t tc = INK;
  const char* line = "";
  switch (friendEnd) {
    case FE_WON:         title = "Victory!";           tc = MINT; line = "You beat your friend!"; break;
    case FE_LOST:        title = "Defeated";           tc = RED;  line = "Your friend won this one"; break;
    case FE_YOU_LEFT:    title = "You ran";            tc = DIM;  line = "Your friend wins this one"; break;
    case FE_FRIEND_LEFT: title = "Friend left";        tc = GOLD; line = "Your friend ran off"; break;
    case FE_LINK_LOST:   title = "Connection lost";    tc = RED;  line = "Move the boards closer and try again"; break;
    case FE_CHEAT:       title = "Battle stopped";     tc = RED;  line = "A throw didn't match its lock"; break;
  }
  text(title, 120, 24, 4, tc);
  text(line, 120, 228, 2, INK);
  if (friendXp) {
    Owned o = collectionAt(fighterIndex);
    Monster m = makeMonster(o.seed, ownedNight(o));
    if (friendLevelUp) snprintf(buf, sizeof(buf), "%s +%u XP - Level up! Lv %u", m.name, friendXp, o.level);
    else               snprintf(buf, sizeof(buf), "%s +%u XP", m.name, friendXp);
    text(buf, 120, 248, 1, friendLevelUp ? GOLD : MINT);
  }
  if (friendEnd == FE_WON || friendEnd == FE_FRIEND_LEFT) { sfxCaught(); ledColor(0, 1, 0); }
  else { sfxFled(); ledColor(1, 0, 0); }
  drawBtn(B_FAGAIN);
  drawBtn(B_HOME);
}

void updateFriendEnd(uint32_t t) {
  Owned o = collectionAt(fighterIndex);
  showMonster(makeMonster(o.seed, ownedNight(o)), 120, 128, 110, t);
  int x, y;
  if (!touchTapped(x, y)) return;
  if (hit(B_FAGAIN, x, y)) { sfxClick(); linkStop(true); startFriendMatch(); }
  else if (hit(B_HOME, x, y)) { sfxClick(); linkStop(true); go(S_TITLE); }
}

// Called every frame: if the friend's board leaves or goes quiet in the
// middle of a friend match, stop and say so (no waiting forever).
void checkFriendLink() {
  if (battleKind != BK_FRIEND || linkState() != LINK_LOST) return;
  bool inBattle = screen == S_BATTLE && !battleOver(battle);
  bool active = inBattle || screen == S_FRIEND_FIND || screen == S_FRIEND_WAIT || screen == S_FIGHTER;
  if (!active) return;
  friendXp = 0;
  friendLevelUp = false;
  if (linkCheated()) friendEnd = FE_CHEAT;
  else if (linkPeerLeft()) {
    friendEnd = FE_FRIEND_LEFT;
    if (inBattle) {   // they ran mid-battle: you win
      friendXp = battleXp(battle.f[1]);
      friendLevelUp = giveXp(fighterIndex, friendXp);
    }
  } else friendEnd = FE_LINK_LOST;
  linkStop(false);
  go(S_FRIEND_END);
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
    case S_FRIEND_FIND: enterFriendFind(); break;
    case S_FRIEND_WAIT: enterFriendWait(); break;
    case S_FRIEND_END:  enterFriendEnd(); break;
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

  // Splash logo (landscape) while the radio wakes up: at least 2.5 s, a tap skips the rest.
  uint32_t splashAt = millis();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, 320, 240, splash_logo);
  tft.setSwapBytes(false);
  storageBegin();
  setSoundOn(loadSoundSetting());
  scannerBegin();
  int tx, ty;
  touchTapped(tx, ty);   // forget any press from before the logo was up
  while (millis() - splashAt < 2500 && !touchTapped(tx, ty)) delay(20);
  tft.setRotation(0);   // portrait for the game
  tft.fillScreen(BG);
  Serial.printf("BattleBuddies v0.1  collection=%u  scans=%u  light=%d\n",
                (unsigned)collectionSize(), (unsigned)totalScans(), lightReading());
  go(S_TITLE);
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
    case S_FRIEND_FIND: updateFriendFind(t); break;
    case S_FRIEND_WAIT: updateFriendWait(t); break;
    case S_FRIEND_END:  updateFriendEnd(t); break;
  }
  linkPoll();          // no-op unless a friend link is running
  checkFriendLink();
  delay(16);
}
