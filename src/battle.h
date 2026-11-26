// BattleBuddies - battle rules (Rock Paper Scissors Lizard Spock).
//
// Pure game logic: no screen, no radio, no esp_random(). Given the same
// inputs it always gives the same result, so two boards can each run their
// own copy of a battle and stay in sync just by exchanging seed, level and
// the move chosen each turn.
//
// Rules
// -----
// Each turn both fighters pick one of five moves at the same time.
//   Scissors cuts Paper, Paper covers Rock, Rock crushes Lizard,
//   Lizard poisons Spock, Spock smashes Scissors, Scissors decapitates Lizard,
//   Lizard eats Paper, Paper disproves Spock, Spock vaporizes Rock,
//   Rock crushes Scissors.
// The winner of the throw hits the loser. Damage is about
//   20 * ATK / (ATK + foe DEF), times a random 85..115%,
// with ATK/DEF taken at the fighters' levels (statAtLevel), so a buddy with
// equal stats lands ~10 damage and needs 3-5 winning throws to finish a foe.
//
// Ties: nobody wins the throw, but the faster fighter (higher SPD) gets in a
// "scrape" worth a quarter of a normal hit. Equal SPD: both get scraped.
// A tie can never knock a fighter out (it leaves at least 1 HP), so a
// battle always ends on a real win and there is never a double knock-out.
//
// Elements: five of the six elements each have a favoured move. Winning a
// throw with your favoured move hits 50% harder:
//   Stone = Rock, Moss = Paper, Volt = Scissors, Ember = Lizard, Shade = Spock.
// Tide has no favoured move; instead "Tide flows" through ties: on any tie a
// Tide buddy always lands a half-strength hit, whatever its speed.
//
// Two-board use: each board puts itself in side 0 and the other board in
// side 1, and both call battleBegin with the same shared seed. Every turn
// draws exactly one number from the shared RNG, in the same way on both
// sides, so the two boards see the same battle mirrored.
#pragma once
#include <Arduino.h>
#include "monster.h"

enum Move : uint8_t { ROCK, PAPER, SCISSORS, LIZARD, SPOCK, MOVE_COUNT };
const uint8_t NO_MOVE = MOVE_COUNT;

struct Fighter {
  Monster  mon;
  uint8_t  level;
  uint16_t maxHp, hp;
  uint16_t atk, def, spd;   // at level
};

struct Battle {
  Fighter  f[2];        // 0 = this board's buddy, 1 = the opponent
  uint32_t rng;         // shared deterministic RNG state
  uint16_t turn;        // turns played so far
  uint8_t  lastMove[2]; // NO_MOVE before the first turn
};

struct TurnResult {
  uint8_t  move[2];
  int8_t   winner;      // 0 or 1 won the throw, -1 tie
  uint16_t damage[2];   // damage TAKEN by each side this turn
  bool     favoured;    // the winner threw its element's favoured move
};

void       battleBegin(Battle& b, const Monster& m0, uint8_t lv0,
                       const Monster& m1, uint8_t lv1, uint32_t seed);
TurnResult battleTurn(Battle& b, uint8_t move0, uint8_t move1);
bool       battleOver(const Battle& b);
int8_t     battleWinner(const Battle& b);   // 0 or 1, -1 while still going

int8_t      throwWinner(uint8_t a, uint8_t b);             // 0 a, 1 b, -1 tie
const char* moveName(uint8_t m);
const char* moveVerb(uint8_t winner, uint8_t loser);      // "crushes", ...
uint8_t     favouredMove(uint8_t element);                // NO_MOVE for Tide

// Computer opponent. `rnd` is any random number (e.g. esp_random()).
// Picks its favoured move ~35% of the time, sometimes counters the move the
// foe threw last turn, otherwise throws at random.
uint8_t aiChooseMove(const Battle& b, uint8_t side, uint32_t rnd);
// Level for a wild opponent: the player's level -1..+1 (at least 1).
uint8_t opponentLevel(uint8_t playerLevel, uint32_t rnd);
// XP the winner's buddy earns for beating `loser`.
uint16_t battleXp(const Fighter& loser);
