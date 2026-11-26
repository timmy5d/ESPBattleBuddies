// Host-side stand-in for TFT_eSPI: draws into a 16-bit framebuffer so the
// round UI can be rendered to PNG on a PC. Only what the game uses.
#pragma once
#include <Arduino.h>
#ifndef LOAD_GFXFF
#define LOAD_GFXFF 1
#endif
#include <Fonts/GFXFF/gfxfont.h>

enum { TL_DATUM, TC_DATUM, TR_DATUM, ML_DATUM, MC_DATUM, MR_DATUM, BL_DATUM, BC_DATUM, BR_DATUM };
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

class TFT_eSPI {
 public:
  TFT_eSPI() {}
  virtual ~TFT_eSPI() {}
  virtual void drawPixel(int32_t x, int32_t y, uint32_t c);
  virtual void drawChar(int32_t x, int32_t y, uint16_t ch, uint32_t c, uint32_t bg, uint8_t size);
  virtual void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c);
  virtual void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c);
  virtual void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c);
  virtual void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
  virtual int16_t drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font);
  virtual int16_t width() { return w_; }
  virtual int16_t height() { return h_; }

  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c);
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c);
  void fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t c);
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c);
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c);
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c);

  void setViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool vpDatum = true);
  void resetViewport();
  void setTextFont(uint8_t f) { textfont = f; gfxFont = nullptr; }
  void setFreeFont(const GFXfont* f);
  void setTextSize(uint8_t s) { textsize = s ? s : 1; }
  void setTextColor(uint16_t c) { textcolor = textbgcolor = c; }
  void setTextColor(uint16_t c, uint16_t b) { textcolor = c; textbgcolor = b; }
  void setTextDatum(uint8_t d) { textdatum = d; }
  int16_t textWidth(const char* s);
  int16_t textWidth(const char* s, uint8_t font) { uint8_t f = textfont; const GFXfont* g = gfxFont; textfont = font; if (font != 1) gfxFont = nullptr; int16_t w = textWidth(s); textfont = f; gfxFont = g; return w; }
  int16_t fontHeight(uint8_t font);
  int16_t fontHeight() { return fontHeight(textfont); }
  int16_t drawString(const char* s, int32_t x, int32_t y);
  int16_t drawString(const char* s, int32_t x, int32_t y, uint8_t font) { setTextFont(font); return drawString(s, x, y); }

 protected:
  bool inClip(int32_t x, int32_t y) const { return x >= vx && y >= vy && x < vx + vw && y < vy + vh; }
  uint16_t* buf_ = nullptr;
  int16_t w_ = 0, h_ = 0;
  int32_t vx = 0, vy = 0, vw = 0, vh = 0;
  uint8_t textfont = 1, textsize = 1, textdatum = TL_DATUM;
  uint16_t textcolor = 0xFFFF, textbgcolor = 0;
  const GFXfont* gfxFont = nullptr;
  int glyph_ab = 0, glyph_bb = 0;
};

class TFT_eSprite : public TFT_eSPI {
 public:
  explicit TFT_eSprite(TFT_eSPI*) {}
  void setColorDepth(int) {}
  void* createSprite(int16_t w, int16_t h) { w_ = w; h_ = h; buf_ = new uint16_t[w * h](); resetViewport(); return buf_; }
  void fillSprite(uint32_t c) { for (int i = 0; i < w_ * h_; i++) buf_[i] = c; }
  void* getPointer() { return buf_; }
  uint16_t pixel(int x, int y) const { return buf_[y * w_ + x]; }
};
