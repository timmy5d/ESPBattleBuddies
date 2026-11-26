#include "battle.h"

namespace {

// BEATS[a] has bit b set when move a beats move b.
const uint8_t BEATS[MOVE_COUNT] = {
  (1 << SCISSORS) | (1 << LIZARD),   // Rock crushes Scissors and Lizard
  (1 << ROCK)     | (1 << SPOCK),    // Paper covers Rock, disproves Spock
  (1 << PAPER)    | (1 << LIZARD),   // Scissors cuts Paper, decapitates Lizard
  (1 << SPOCK)    | (1 << PAPER),    // Lizard poisons Spock, eats Paper
  (1 << SCISSORS) | (1 << ROCK),     // Spock smashes Scissors, vaporizes Rock
};

const char* const VERB[MOVE_COUNT][MOVE_COUNT] = {
  //  vs Rock      Paper        Scissors       Lizard        Spock
  { "",          "",          "crushes",     "crushes",    ""          },  // Rock
  { "covers",    "",          "",            "",           "disproves" },  // Paper
  { "",          "cuts",      "",            "decapitates",""          },  // Scissors
  { "",          "eats",      "",            "",           "poisons"   },  // Lizard
  { "vaporizes", "",          "smashes",     "",           ""          },  // Spock
};

uint32_t xorshift(uint32_t& s) {
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return s;
}

void initFighter(Fighter& f, const Monster& m, uint8_t level) {
  f.mon   = m;
  f.level = level < 1 ? 1 : level;
  f.maxHp = statAtLevel(m.hp, f.level);
  f.hp    = f.maxHp;
  f.atk   = statAtLevel(m.atk, f.level);
  f.def   = statAtLevel(m.def, f.level);
  f.spd   = statAtLevel(m.spd, f.level);
}

// A normal hit from `a` on `d`, before variance. Scaled by 100.
uint32_t baseHit100(const Fighter& a, const Fighter& d) {
  return 2000u * a.atk / (uint32_t)max(1, a.atk + d.def);
}

// Applies damage; a scrape (tie) never takes the last HP point.
uint16_t hurt(Fighter& f, uint16_t dmg, bool canKo) {
  uint16_t floor = canKo ? 0 : 1;
  if (f.hp <= floor) return 0;
  uint16_t room = f.hp - floor;
  if (dmg > room) dmg = room;
  f.hp -= dmg;
  return dmg;
}

}  // namespace

int8_t throwWinner(uint8_t a, uint8_t b) {
  if (a >= MOVE_COUNT || b >= MOVE_COUNT || a == b) return -1;
  return (BEATS[a] >> b) & 1 ? 0 : 1;
}

const char* moveName(uint8_t m) {
  static const char* const N[] = { "Rock", "Paper", "Scissors", "Lizard", "Spock" };
  return m < MOVE_COUNT ? N[m] : "?";
}

const char* moveVerb(uint8_t w, uint8_t l) {
  return (w < MOVE_COUNT && l < MOVE_COUNT) ? VERB[w][l] : "";
}

uint8_t favouredMove(uint8_t e) {
  switch (e) {
    case STONE: return ROCK;
    case MOSS:  return PAPER;
    case VOLT:  return SCISSORS;
    case EMBER: return LIZARD;
    case SHADE: return SPOCK;
    default:    return NO_MOVE;   // Tide: flows through ties instead
  }
}

void battleBegin(Battle& b, const Monster& m0, uint8_t lv0,
                 const Monster& m1, uint8_t lv1, uint32_t seed) {
  initFighter(b.f[0], m0, lv0);
  initFighter(b.f[1], m1, lv1);
  b.rng  = seed ? seed : 0x5EED1234u;
  xorshift(b.rng);
  b.turn = 0;
  b.lastMove[0] = b.lastMove[1] = NO_MOVE;
}

TurnResult battleTurn(Battle& b, uint8_t move0, uint8_t move1) {
  TurnResult r{};
  r.move[0] = move0 % MOVE_COUNT;
  r.move[1] = move1 % MOVE_COUNT;
  r.winner  = throwWinner(r.move[0], r.move[1]);
  uint32_t roll = xorshift(b.rng);   // exactly one draw per turn (keeps links in sync)

  if (r.winner >= 0) {
    Fighter& a = b.f[r.winner];
    Fighter& d = b.f[1 - r.winner];
    uint32_t dmg = baseHit100(a, d) * (85 + roll % 31) / 10000;
    r.favoured = r.move[r.winner] == favouredMove(a.mon.element);
    if (r.favoured) dmg = dmg * 3 / 2;
    if (dmg < 2) dmg = 2;
    r.damage[1 - r.winner] = hurt(d, (uint16_t)dmg, true);
  } else {
    // Tie: who gets a scrape in? Faster side, both if equal, Tide always.
    const Fighter& f0 = b.f[0];
    const Fighter& f1 = b.f[1];
    bool hits[2] = { f0.spd >= f1.spd || f0.mon.element == TIDE,
                     f1.spd >= f0.spd || f1.mon.element == TIDE };
    uint16_t dmg[2] = {0, 0};   // damage dealt BY side i
    for (int i = 0; i < 2; i++) {
      if (!hits[i]) continue;
      uint32_t h = baseHit100(b.f[i], b.f[1 - i]) / 100;
      h = b.f[i].mon.element == TIDE ? h / 2 : h / 4;
      dmg[i] = (uint16_t)max<uint32_t>(1, h);
    }
    r.damage[1] = hurt(b.f[1], dmg[0], false);
    r.damage[0] = hurt(b.f[0], dmg[1], false);
  }
  b.lastMove[0] = r.move[0];
  b.lastMove[1] = r.move[1];
  b.turn++;
  return r;
}

bool battleOver(const Battle& b) { return b.f[0].hp == 0 || b.f[1].hp == 0; }

int8_t battleWinner(const Battle& b) {
  if (b.f[1].hp == 0) return 0;
  if (b.f[0].hp == 0) return 1;
  return -1;
}

uint8_t aiChooseMove(const Battle& b, uint8_t side, uint32_t rnd) {
  const Fighter& me = b.f[side & 1];
  uint8_t foeLast = b.lastMove[1 - (side & 1)];
  uint8_t fav = favouredMove(me.mon.element);
  uint32_t pick = rnd % 100;
  rnd /= 100;
  if (fav != NO_MOVE && pick < 35) return fav;
  if (foeLast != NO_MOVE && pick >= 35 && pick < 60) {
    // Expect the foe to repeat itself: throw one of the two moves that beat it.
    uint8_t counters[2], n = 0;
    for (uint8_t m = 0; m < MOVE_COUNT; m++)
      if (throwWinner(m, foeLast) == 0) counters[n++] = m;
    return counters[rnd % n];
  }
  return rnd % MOVE_COUNT;
}

uint8_t opponentLevel(uint8_t playerLevel, uint32_t rnd) {
  int lv = (int)playerLevel - 1 + (int)(rnd % 3);
  return (uint8_t)constrain(lv, 1, 99);
}

uint16_t battleXp(const Fighter& loser) {
  return 12 + loser.level * 6 + loser.mon.rarity * 5;
}
