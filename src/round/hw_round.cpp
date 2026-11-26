// BattleBuddies - hw.h for the Waveshare ESP32-S3 1.43in round AMOLED board.
//
// Differences from the CYD, and what stands in for them:
//   touch      FT3168 capacitive over I2C, in 466x466 screen coordinates
//   RGB LED    none: a glow ring round the screen edge shows the same colours
//   light      none: the real-time clock decides when it's night (clock.h)
//   speaker    none: the sound calls do nothing, the setting is still saved
#include "hw.h"
#include <Wire.h>
#include "board.h"
#include "clock.h"
#include "display.h"
#include "monster.h"

namespace {
bool wasDown = false;
uint32_t lastTap = 0;
bool soundEnabled = true;

bool i2cWrite(uint8_t addr, uint8_t reg, const uint8_t* data, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  for (size_t i = 0; i < n; i++) Wire.write(data[i]);
  return Wire.endTransmission() == 0;
}
bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* data, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) data[i] = Wire.read();
  return true;
}
uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }
uint8_t unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
}  // namespace

void hwBegin() {
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  const uint8_t normal = 0x00;
  i2cWrite(TOUCH_ADDR, 0x00, &normal, 1);   // touch: normal operating mode
  uint8_t ctl = 0;
  if (i2cRead(RTC_ADDR, 0x00, &ctl, 1) && (ctl & 0x22)) {   // make sure the clock runs in 24 h mode
    ctl &= ~0x22;
    i2cWrite(RTC_ADDR, 0x00, &ctl, 1);
  }
}

// ---------------------------------------------------------------- touch
bool touchDown(int& x, int& y) {
  uint8_t n = 0, p[4];
  if (!i2cRead(TOUCH_ADDR, 0x02, &n, 1) || (n & 0x0F) == 0) return false;
  if (!i2cRead(TOUCH_ADDR, 0x03, p, 4)) return false;
  int tx = ((p[0] & 0x0F) << 8) | p[1];
  int ty = ((p[2] & 0x0F) << 8) | p[3];
  if (TOUCH_SWAP_XY) { int s = tx; tx = ty; ty = s; }
  if (TOUCH_FLIP_X) tx = LCD_W - 1 - tx;
  if (TOUCH_FLIP_Y) ty = LCD_H - 1 - ty;
  x = constrain(tx, 0, LCD_W - 1);
  y = constrain(ty, 0, LCD_H - 1);
  return true;
}

bool touchTapped(int& x, int& y) {
  bool down = touchDown(x, y);
  bool tapped = down && !wasDown && millis() - lastTap > 120;
  wasDown = down;
  if (tapped) lastTap = millis();
  return tapped;
}

// ---------------------------------------------------------------- sound (no speaker on this board)
void playNotes(const Note*, size_t) {}
void setSoundOn(bool on) { soundEnabled = on; }
bool soundOn() { return soundEnabled; }
void sfxClick() {}
void sfxScanPing() {}
void sfxFound() {}
void sfxThrow() {}
void sfxCaught() {}
void sfxMiss() {}
void sfxFled() {}

// ---------------------------------------------------------------- "LED" = glow ring
void ledColor(bool r, bool g, bool b) {
  uint16_t c = 0;
  if (r && g && b)  c = rgb565(235, 235, 245);
  else if (r && g)  c = rgb565(245, 200, 40);
  else if (r && b)  c = rgb565(200, 90, 235);
  else if (g && b)  c = rgb565(60, 210, 220);
  else if (r)       c = rgb565(235, 60, 50);
  else if (g)       c = rgb565(60, 215, 100);
  else if (b)       c = rgb565(60, 120, 245);
  displayRing(c);
}
void ledOff() { displayRing(0); }

// ---------------------------------------------------------------- clock / night
bool clockNow(uint8_t& h, uint8_t& m, uint8_t& s) {
  uint8_t t[3];
  if (!i2cRead(RTC_ADDR, 0x04, t, 3)) return false;
  if (t[0] & 0x80) return false;   // oscillator stopped: the time was lost
  s = unbcd(t[0] & 0x7F);
  m = unbcd(t[1] & 0x7F);
  h = unbcd(t[2] & 0x3F);
  return h < 24 && m < 60;
}

bool clockValid() { uint8_t h, m, s; return clockNow(h, m, s); }

void clockSet(uint8_t h, uint8_t m) {
  // Seconds (also clears the "time lost" flag), minutes, hours, then a date
  // so the chip has something sensible; the game only uses the time.
  const uint8_t t[7] = {0, bcd(m), bcd(h), bcd(1), 0, bcd(1), bcd(26)};
  i2cWrite(RTC_ADDR, 0x04, t, 7);
}

bool clockIsNightHour(uint8_t h) {
  return NIGHT_FROM_HOUR > NIGHT_TO_HOUR ? (h >= NIGHT_FROM_HOUR || h < NIGHT_TO_HOUR)
                                         : (h >= NIGHT_FROM_HOUR && h < NIGHT_TO_HOUR);
}

bool clockIsNight() {
  uint8_t h, m, s;
  return clockNow(h, m, s) && clockIsNightHour(h);
}

bool isDark() { return clockIsNight(); }

// The scanner mixes this into "static" monsters as noise.
int lightReading() {
  uint8_t h = 0, m = 0, s = 0;
  clockNow(h, m, s);
  return (h * 60 + m) * 60 + s + (int)(esp_random() & 0x3FF);
}
