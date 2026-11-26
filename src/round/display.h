// BattleBuddies - round AMOLED display for the Waveshare ESP32-S3 1.43in board.
//
// The game draws into a full-screen 466x466 canvas in PSRAM (a TFT_eSPI
// sprite, so all the usual drawing calls, fonts and drawMonster() work).
// The canvas remembers which area changed, and displayFlush() sends just
// that area to the panel over QSPI. Drawing off-screen first also means
// animations never flicker.
#pragma once
#include <TFT_eSPI.h>

class Canvas : public TFT_eSprite {
 public:
  explicit Canvas(TFT_eSPI* parent) : TFT_eSprite(parent) {}

  using TFT_eSprite::drawChar;
  void drawPixel(int32_t x, int32_t y, uint32_t c) override;
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) override;
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) override;
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) override;
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) override;
  void drawChar(int32_t x, int32_t y, uint16_t ch, uint32_t c, uint32_t bg, uint8_t size) override;
  int16_t drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font) override;

  void clear(uint16_t color);                 // fill everything (and mark it all changed)
  void mark(int32_t x, int32_t y, int32_t w, int32_t h);
  void markAll() { mark(0, 0, width(), height()); }
  bool takeDirty(int16_t& x0, int16_t& y0, int16_t& x1, int16_t& y1);   // inclusive; clears it
  bool allDirty() const { return dx0 <= 0 && dy0 <= 0 && dx1 >= width_ - 1 && dy1 >= height_ - 1; }
  void setSize(int16_t w, int16_t h) { width_ = w; height_ = h; }

 private:
  int16_t dx0 = 32767, dy0 = 32767, dx1 = -1, dy1 = -1;
  int16_t width_ = 0, height_ = 0;
};

bool    displayBegin();        // false if the canvas didn't fit in memory
Canvas& canvas();
void    displayFlush();        // send whatever changed since the last flush
void    displayRing(uint16_t color);   // glow ring round the edge (stands in for the CYD's RGB LED)
uint8_t displayPanelId();      // 0x86 SH8601, otherwise CO5300
