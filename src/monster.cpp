#include "monster.h"

namespace {

// Small deterministic PRNG (xorshift32) so trait rolls never depend on
// esp_random() or anything that differs between boards.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) { next(); next(); }
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  uint32_t below(uint32_t n) { return next() % n; }
};

uint32_t mix(uint32_t x) {           // final avalanche so close seeds differ a lot
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

// Three body colours per element, picked to make the type readable at a glance.
const uint8_t PALETTE[ELEMENT_COUNT][3][3] = {
  {{232, 84, 52}, {245,140, 40}, {200, 50, 70}},   // Ember
  {{ 52,130,230}, { 40,190,210}, { 80, 90,220}},   // Tide
  {{ 70,180, 80}, {140,200, 60}, { 40,150,120}},   // Moss
  {{250,210, 40}, {255,240,110}, {240,170, 30}},   // Volt
  {{130, 80,200}, { 90, 70,150}, {170, 90,190}},   // Shade
  {{150,120, 90}, {130,130,140}, {180,150,110}},   // Stone
};
const uint8_t ACCENTS[8][3] = {
  {255,255,255}, {255,220,120}, {255,120,160}, {120,240,255},
  {180,255,140}, { 60, 50, 80}, {255,170, 80}, {200,170,255},
};

const char* const PRE[] = { "Bor","Zip","Kra","Mun","Flo","Gri","Tav","Quo",
                            "Lum","Rix","Pek","Oza","Vel","Dru","Sny","Hob",
                            "Yip","Cag","Nub","Wex","Jor","Fiz","Pom","Tru" };
const char* const MID[] = { "","","a","o","i","u","ee","ra","lo","bi" };
const char* const END[] = { "bit","mox","zle","gor","nip","dra","fin","lux",
                            "tok","pom","rax","sy","wump","kin","tor","vee",
                            "dle","bop","quin","zor" };
template <typename T, size_t N> constexpr size_t countOf(T (&)[N]) { return N; }

}  // namespace

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t shade565(uint16_t c, int percent) {
  int r = ((c >> 11) & 0x1F) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c & 0x1F) << 3;
  auto f = [percent](int v) {
    v = percent <= 100 ? v * percent / 100 : v + (255 - v) * (percent - 100) / 100;
    return (uint8_t)constrain(v, 0, 255);
  };
  return rgb565(f(r), f(g), f(b));
}

uint32_t seedFromBytes(const uint8_t* data, size_t len, uint8_t salt) {
  uint32_t h = 2166136261u ^ salt;      // FNV-1a
  for (size_t i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
  return mix(h ^ 0xBB0D1E5u);
}

Monster makeMonster(uint32_t seed, bool night) {
  Monster m{};
  Rng r(mix(seed));
  m.seed  = seed;
  m.night = night;

  // Rarity: about 59% common, 27% uncommon, 12% rare, 2% legendary.
  uint32_t roll = r.below(1000);
  m.rarity = roll < 590 ? 0 : roll < 860 ? 1 : roll < 980 ? 2 : 3;

  m.element  = r.below(ELEMENT_COUNT);
  m.body     = r.below(BODY_COUNT);
  m.head     = r.below(HEAD_COUNT);
  m.limbs    = r.below(LIMB_COUNT);
  m.tail     = r.below(TAIL_COUNT);
  m.pattern  = r.below(PAT_COUNT);
  m.mouth    = r.below(MOUTH_COUNT);
  m.eyeStyle = r.below(EYE_COUNT);
  m.eyes     = 1 + r.below(10) / 4;          // 1,2 mostly; 3 sometimes
  if (m.eyes > 3) m.eyes = 3;
  // Flame tails only on hot-blooded types; Ember buddies always get one.
  if (m.tail == TAIL_FLAME && m.element != EMBER && m.element != VOLT) m.tail = TAIL_PLAIN;
  if (m.element == EMBER && m.tail == TAIL_PLAIN) m.tail = TAIL_FLAME;

  const uint8_t* p = PALETTE[m.element][r.below(3)];
  m.color = rgb565(p[0], p[1], p[2]);
  const uint8_t* a = ACCENTS[r.below(8)];
  m.accent = rgb565(a[0], a[1], a[2]);

  // Base stats: a body-shape bias, some per-seed spread, and a rarity bonus.
  static const int8_t BIAS[BODY_COUNT][4] = {   // hp, atk, def, spd
    { 4, 0, 0, 0}, { 2, 2, 0,-2}, {-2, 2,-2, 4}, { 2,-2, 6,-4}, { 6,-2, 2,-4}, {-4, 2,-2, 6},
  };
  uint8_t* stats[4] = { &m.hp, &m.atk, &m.def, &m.spd };
  for (int i = 0; i < 4; i++) {
    int base = (i == 0 ? 30 : 12) + (int)r.below(8) + BIAS[m.body][i] + m.rarity * 4;
    *stats[i] = (uint8_t)constrain(base, 5, 60);
  }

  snprintf(m.name, sizeof(m.name), "%s%s%s",
           PRE[r.below(countOf(PRE))], MID[r.below(countOf(MID))], END[r.below(countOf(END))]);

  if (night) {           // night forms: same creature, darker and Shade-type
    m.element = SHADE;
    m.color   = shade565(m.color, 55);
    m.accent  = rgb565(190, 140, 255);
    m.spd     = (uint8_t)min(60, m.spd + 4);
  }
  return m;
}

uint8_t wildLevel(const Monster& m) {
  return 1 + (mix(m.seed ^ 0x1E7E1u) % 4) + m.rarity;
}

uint16_t statAtLevel(uint8_t base, uint8_t level) {
  return base + (uint32_t)base * (level - 1) * 8 / 100;
}

const char* elementName(uint8_t e) {
  static const char* const N[] = { "Ember", "Tide", "Moss", "Volt", "Shade", "Stone" };
  return e < ELEMENT_COUNT ? N[e] : "?";
}

uint16_t elementColor(uint8_t e) {
  static const uint16_t C[] = { rgb565(232,84,52), rgb565(52,130,230), rgb565(70,180,80),
                                rgb565(240,200,30), rgb565(140,90,210), rgb565(150,120,90) };
  return e < ELEMENT_COUNT ? C[e] : 0xFFFF;
}

const char* rarityName(uint8_t r) {
  static const char* const N[] = { "Common", "Uncommon", "Rare", "Legendary" };
  return r < 4 ? N[r] : "?";
}
