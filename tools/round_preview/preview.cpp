// Renders every screen of the round UI to out/*.ppm (see make_previews.sh).
#include "../../src/round/ui_round.cpp"

extern ScanPhase scanPhase;
extern int tapX, tapY;

static void save(const char* name) {
  displayFlush();
  char path[128];
  snprintf(path, sizeof(path), "out/%s.ppm", name);
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6 466 466 255\n");
  for (int y = 0; y < 466; y++)
    for (int x = 0; x < 466; x++) {
      uint16_t c = tft.pixel(x, y);
      uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

static void frame(uint32_t ms) { fakeMillis += ms; loop(); }

int main() {
  fakeMillis = 1000;
  displayBegin();
  storageBegin();
  drawSplash();                save("01_splash");

  go(S_TITLE); frame(300);     save("02_title");
  scanMode = MODE_WIFI; scanForBot = false;
  go(S_SCAN); frame(1200);     save("03_scan");
  pickSel = 1;
  scanPhase = SCAN_DONE;
  go(S_PICK); frame(400);      save("04_pick");

  ScanSignal s = scannerSignal(1);
  wild = makeMonster(s.seed, false); wildLv = wildLevel(wild); wildSource = s.source; wildRssi = s.rssi;
  go(S_ENCOUNTER); frame(300); save("05_encounter");

  battleKind = BK_WILD; foeMon = wild; foeLv = wildLv; fighterIndex = 2;
  go(S_BATTLE); frame(300);    save("06_battle_pick");
  bMove[0] = ROCK; bMove[1] = SCISSORS;
  lastTurn = battleTurn(battle, ROCK, SCISSORS);
  showTurnResult(); drawMoveButtons(false);
  bPhase = BP_RESULT; bPhaseAt = fakeMillis; shownHp[1] = battle.f[1].hp;
  fakeMillis += 600; updateBattle(fakeMillis - screenStart);
  save("07_battle_hit");

  outcome = OUT_CAUGHT; resultLevel = wildLv; fighterXpGained = 14; fighterLevelUp = true;
  go(S_RESULT); frame(300);    save("08_result");
  collectionPage = 0;
  go(S_COLLECTION); frame(50); save("09_collection");
  detailIndex = 2;
  go(S_DETAIL); frame(300);    save("10_detail");

  scanForBot = true;
  go(S_BOT_INTRO); frame(300); save("11_bot_intro");
  battleKind = BK_BOT; memset(koOut, 0, sizeof(koOut)); koOut[1] = true;
  foeMon = botTeam[0]; foeLv = botLv[0];
  collectionPage = 0;
  go(S_FIGHTER); frame(50);    save("12_fighter");
  botNext = 1; roundsPlayed = 1; roundWinner = 0; roundXp = 18; roundLevelUp = false; fighterIndex = 0;
  go(S_BOT_ROUND); frame(300); save("13_bot_round");
  botNext = 3; roundXp = 22; roundLevelUp = true; fought[0] = true;
  go(S_BOT_ROUND); frame(300); save("14_bot_won");

  go(S_CLOCK); frame(50);      save("15_clock");
  go(S_TOUCHTEST);
  tapX = 120; tapY = 300; frame(30);
  save("16_touch_test");
  return 0;
}
