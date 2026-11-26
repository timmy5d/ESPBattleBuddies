// Host-side TFT_eSPI drawing, close enough to the real library for layout previews.
#include <TFT_eSPI.h>
#include <Fonts/glcdfont.c>
#include <Fonts/Font16.h>
#include <Fonts/Font7srle.h>

uint32_t fakeMillis = 0;
FakeSerial Serial;

void TFT_eSPI::setViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool) { vx = x; vy = y; vw = w; vh = h; }
void TFT_eSPI::resetViewport() { vx = 0; vy = 0; vw = w_; vh = h_; }

void TFT_eSPI::drawPixel(int32_t x, int32_t y, uint32_t c) {
  if (buf_ && inClip(x, y) && x < w_ && y < h_ && x >= 0 && y >= 0) buf_[y * w_ + x] = c;
}
void TFT_eSPI::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) { for (int i = 0; i < w; i++) drawPixel(x + i, y, c); }
void TFT_eSPI::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) { for (int i = 0; i < h; i++) drawPixel(x, y + i, c); }
void TFT_eSPI::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) { for (int j = 0; j < h; j++) drawFastHLine(x, y + j, w, c); }
void TFT_eSPI::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
  drawFastHLine(x, y, w, c); drawFastHLine(x, y + h - 1, w, c); drawFastVLine(x, y, h, c); drawFastVLine(x + w - 1, y, h, c);
}
void TFT_eSPI::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    drawPixel(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
void TFT_eSPI::drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t c) {
  int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  drawPixel(x0, y0 + r, c); drawPixel(x0, y0 - r, c); drawPixel(x0 + r, y0, c); drawPixel(x0 - r, y0, c);
  while (x < y) {
    if (f >= 0) { y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
    drawPixel(x0 + x, y0 + y, c); drawPixel(x0 - x, y0 + y, c); drawPixel(x0 + x, y0 - y, c); drawPixel(x0 - x, y0 - y, c);
    drawPixel(x0 + y, y0 + x, c); drawPixel(x0 - y, y0 + x, c); drawPixel(x0 + y, y0 - x, c); drawPixel(x0 - y, y0 - x, c);
  }
}
void TFT_eSPI::fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t c) {
  for (int y = -r; y <= r; y++) { int w = (int)sqrtf((float)(r * r - y * y) + 0.5f * r); drawFastHLine(x0 - w, y0 + y, 2 * w + 1, c); }
}
void TFT_eSPI::fillEllipse(int32_t x0, int32_t y0, int32_t rx, int32_t ry, uint32_t c) {
  if (rx < 1 || ry < 1) return;
  for (int y = -ry; y <= ry; y++) { int w = (int)(rx * sqrtf(1.0f - (float)y * y / ((float)ry * ry))); drawFastHLine(x0 - w, y0 + y, 2 * w + 1, c); }
}
void TFT_eSPI::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
  r = min(r, min(w / 2, h / 2));
  for (int j = 0; j < h; j++) {
    int inset = 0;
    int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
    if (dy) inset = r - (int)sqrtf((float)(r * r - dy * dy) + 0.5f * r);
    drawFastHLine(x + inset, y + j, w - 2 * inset, c);
  }
}
void TFT_eSPI::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
  r = min(r, min(w / 2, h / 2));
  int prev = -1;
  for (int j = 0; j < h; j++) {
    int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
    int inset = dy ? r - (int)sqrtf((float)(r * r - dy * dy) + 0.5f * r) : 0;
    if (j == 0 || j == h - 1) drawFastHLine(x + inset, y + j, w - 2 * inset, c);
    else {
      int a = prev >= 0 ? min(prev, inset) : inset, b = prev >= 0 ? max(prev, inset) : inset;
      for (int k = a; k <= b; k++) { drawPixel(x + k, y + j, c); drawPixel(x + w - 1 - k, y + j, c); }
    }
    prev = inset;
  }
}
void TFT_eSPI::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
  int minY = min(y0, min(y1, y2)), maxY = max(y0, max(y1, y2));
  for (int y = minY; y <= maxY; y++) {
    float xs[3]; int n = 0;
    int px[3] = {x0, x1, x2}, py[3] = {y0, y1, y2};
    for (int i = 0; i < 3; i++) {
      int a = i, b = (i + 1) % 3;
      if (py[a] == py[b]) { if (py[a] == y && n < 2) { xs[n++] = px[a]; if (n < 3) xs[n++] = px[b]; } continue; }
      if ((y >= min(py[a], py[b])) && (y <= max(py[a], py[b])) && n < 3)
        xs[n++] = px[a] + (float)(y - py[a]) * (px[b] - px[a]) / (py[b] - py[a]);
    }
    if (n < 2) continue;
    float lo = xs[0], hi = xs[0];
    for (int i = 1; i < n; i++) { lo = fminf(lo, xs[i]); hi = fmaxf(hi, xs[i]); }
    drawFastHLine((int)lroundf(lo), y, (int)lroundf(hi) - (int)lroundf(lo) + 1, c);
  }
}

void TFT_eSPI::setFreeFont(const GFXfont* f) {
  textfont = 1;
  gfxFont = f;
  glyph_ab = glyph_bb = 0;
  for (int c = 0; c < f->last - f->first; c++) {
    const GFXglyph& g = f->glyph[c];
    int ab = -g.yOffset, bb = g.height - ab;
    glyph_ab = max(glyph_ab, ab);
    glyph_bb = max(glyph_bb, bb);
  }
}

int16_t TFT_eSPI::fontHeight(uint8_t font) {
  if (font == 1 && gfxFont) return gfxFont->yAdvance * textsize;
  if (font == 1) return 8 * textsize;
  if (font == 2) return chr_hgt_f16 * textsize;
  if (font == 7) return chr_hgt_f7s * textsize;
  return 16;
}

static int charWidth(const TFT_eSPI*, uint8_t font, const GFXfont* g, uint8_t size, char ch) {
  if (font == 1 && g) { if (ch < g->first || ch > g->last) return 0; return g->glyph[ch - g->first].xAdvance * size; }
  if (font == 1) return 6 * size;
  if (ch < 32 || ch > 127) return 0;
  if (font == 2) return widtbl_f16[ch - 32] * size;
  if (font == 7) return widtbl_f7s[ch - 32] * size;
  return 0;
}

int16_t TFT_eSPI::textWidth(const char* s) {
  int w = 0;
  for (; *s; s++) w += charWidth(this, textfont, gfxFont, textsize, *s);
  return w;
}

void TFT_eSPI::drawChar(int32_t x, int32_t y, uint16_t ch, uint32_t c, uint32_t bg, uint8_t size) {
  if (gfxFont) {   // free font: y is the baseline
    if (ch < gfxFont->first || ch > gfxFont->last) return;
    const GFXglyph& g = gfxFont->glyph[ch - gfxFont->first];
    const uint8_t* bm = gfxFont->bitmap + g.bitmapOffset;
    int bit = 0; uint8_t bits = 0;
    for (int yy = 0; yy < g.height; yy++)
      for (int xx = 0; xx < g.width; xx++) {
        if (!(bit++ & 7)) bits = *bm++;
        if (bits & 0x80) fillRect(x + (g.xOffset + xx) * size, y + (g.yOffset + yy) * size, size, size, c);
        bits <<= 1;
      }
    return;
  }
  for (int i = 0; i < 6; i++) {   // classic 5x7
    uint8_t line = i == 5 ? 0 : font[ch * 5 + i];
    for (int j = 0; j < 8; j++, line >>= 1) {
      if (line & 1) fillRect(x + i * size, y + j * size, size, size, c);
      else if (bg != c) fillRect(x + i * size, y + j * size, size, size, bg);
    }
  }
}

int16_t TFT_eSPI::drawChar(uint16_t uni, int32_t x, int32_t y, uint8_t fnt) {
  if (fnt == 1) { drawChar(x, y, uni, textcolor, textbgcolor, textsize); return charWidth(this, 1, gfxFont, textsize, uni); }
  if (uni < 32 || uni > 127) return 0;
  int s = textsize;
  if (fnt == 2) {
    int width = widtbl_f16[uni - 32], w = (width + 6) / 8;
    const uint8_t* p = chrtbl_f16[uni - 32];
    for (int i = 0; i < chr_hgt_f16; i++)
      for (int k = 0; k < w; k++) {
        uint8_t line = p[w * i + k];
        for (int b = 0; b < 8; b++) if (line & (0x80 >> b)) fillRect(x + (k * 8 + b) * s, y + i * s, s, s, textcolor);
      }
    return width * s;
  }
  if (fnt == 7) {
    int width = widtbl_f7s[uni - 32], total = width * chr_hgt_f7s, pc = 0;
    const uint8_t* p = chrtbl_f7s[uni - 32];
    while (pc < total) {
      uint8_t line = *p++;
      if (line & 0x80) {
        int n = (line & 0x7F) + 1;
        while (n--) { fillRect(x + (pc % width) * s, y + (pc / width) * s, s, s, textcolor); pc++; }
      } else pc += line + 1;
    }
    return width * s;
  }
  return 0;
}

int16_t TFT_eSPI::drawString(const char* s, int32_t x, int32_t y) {
  int cwidth = textWidth(s), cheight = fontHeight(textfont);
  bool free = textfont == 1 && gfxFont;
  if (free) {
    cheight = glyph_ab * textsize;
    y += cheight;
    if (textdatum == BL_DATUM || textdatum == BC_DATUM || textdatum == BR_DATUM) cheight += glyph_bb * textsize;
  }
  switch (textdatum) {
    case TC_DATUM: x -= cwidth / 2; break;
    case TR_DATUM: x -= cwidth; break;
    case ML_DATUM: y -= cheight / 2; break;
    case MC_DATUM: x -= cwidth / 2; y -= cheight / 2; break;
    case MR_DATUM: x -= cwidth; y -= cheight / 2; break;
    case BL_DATUM: y -= cheight; break;
    case BC_DATUM: x -= cwidth / 2; y -= cheight; break;
    case BR_DATUM: x -= cwidth; y -= cheight; break;
    default: break;
  }
  for (; *s; s++) x += drawChar((uint8_t)*s, x, y, textfont);
  return cwidth;
}
