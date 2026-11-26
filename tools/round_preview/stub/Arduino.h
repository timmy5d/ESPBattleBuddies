// Host-side stand-in for Arduino.h, just enough to build the game's screen
// code on a PC and render screenshots (see tools/round_preview/README.md).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pgmspace.h"

template <class T, class U> inline auto min(T a, U b) { return a < b ? a : b; }
template <class T, class U> inline auto max(T a, U b) { return a > b ? a : b; }
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
inline long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }

extern uint32_t fakeMillis;
inline uint32_t millis() { return fakeMillis; }
inline void delay(uint32_t ms) { fakeMillis += ms; }
inline uint32_t esp_random() { static uint32_t s = 0x1234567; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
// glibc provides strlcpy
#if !defined(__GLIBC__)
inline size_t strlcpy(char* d, const char* s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l; }
#endif

struct FakeSerial {
  void begin(int) {}
  template <class... A> void printf(const char* f, A... a) { ::printf(f, a...); }
  void println(const char* s) { ::puts(s); }
};
extern FakeSerial Serial;
