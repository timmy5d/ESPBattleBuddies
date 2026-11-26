#include "display.h"
#include <Arduino.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include "board.h"

// ---------------------------------------------------------------- canvas
void Canvas::mark(int32_t x, int32_t y, int32_t w, int32_t h) {
  if (w <= 0 || h <= 0) return;
  int32_t x1 = x + w - 1, y1 = y + h - 1;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x1 >= width_) x1 = width_ - 1;
  if (y1 >= height_) y1 = height_ - 1;
  if (x > x1 || y > y1) return;
  if (x < dx0) dx0 = x;
  if (y < dy0) dy0 = y;
  if (x1 > dx1) dx1 = x1;
  if (y1 > dy1) dy1 = y1;
}

bool Canvas::takeDirty(int16_t& x0, int16_t& y0, int16_t& x1, int16_t& y1) {
  if (dx1 < dx0 || dy1 < dy0) return false;
  x0 = dx0; y0 = dy0; x1 = dx1; y1 = dy1;
  dx0 = dy0 = 32767;
  dx1 = dy1 = -1;
  return true;
}

void Canvas::clear(uint16_t color) { fillSprite(color); markAll(); }

void Canvas::drawPixel(int32_t x, int32_t y, uint32_t c) { mark(x, y, 1, 1); TFT_eSprite::drawPixel(x, y, c); }
void Canvas::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
  mark(min(x0, x1), min(y0, y1), abs(x1 - x0) + 1, abs(y1 - y0) + 1);
  TFT_eSprite::drawLine(x0, y0, x1, y1, c);
}
void Canvas::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) { mark(x, y, 1, h); TFT_eSprite::drawFastVLine(x, y, h, c); }
void Canvas::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) { mark(x, y, w, 1); TFT_eSprite::drawFastHLine(x, y, w, c); }
void Canvas::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) { mark(x, y, w, h); TFT_eSprite::fillRect(x, y, w, h, c); }
void Canvas::drawChar(int32_t x, int32_t y, uint16_t ch, uint32_t c, uint32_t bg, uint8_t size) {
  mark(x, y, 6 * size, 8 * size);
  TFT_eSprite::drawChar(x, y, ch, c, bg, size);
}
int16_t Canvas::drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font) {
  // Free fonts draw from the baseline and can reach well above y; be generous.
  int16_t h = fontHeight(font);
  int16_t w = TFT_eSprite::drawChar(uniCode, x, y, font);
  mark(x - 4, y - h, w + 8, 2 * h + 4);
  return w;
}

// ---------------------------------------------------------------- panel
namespace {
TFT_eSPI parentTft;          // never initialised: only lends its fonts and drawing code
Canvas   cv(&parentTft);
spi_device_handle_t dev = nullptr;
uint8_t  panelId = 0;
int      colOffset = 0;      // CO5300 panels on this board start 6 columns in
uint16_t ringColor = 0;
bool     ringChanged = true;

const int CHUNK_ROWS = 20;
const size_t CHUNK_BYTES = LCD_W * CHUNK_ROWS * 2;
uint8_t* dmaBuf[2] = {nullptr, nullptr};

// Reads the panel's ID byte by bit-banging the bus (as Waveshare's demo
// does) before the SPI driver takes the pins over.
uint8_t readPanelId() {
  const int pins[] = {LCD_CS, LCD_SCLK, LCD_D0, LCD_RST};
  for (int p : pins) { pinMode(p, OUTPUT); digitalWrite(p, HIGH); }
  delay(20);
  digitalWrite(LCD_RST, LOW);  delay(20);
  digitalWrite(LCD_RST, HIGH); delay(120);
  digitalWrite(LCD_CS, LOW);
  auto sendByte = [](uint8_t b) {
    for (int i = 0; i < 8; i++) {
      digitalWrite(LCD_D0, (b & 0x80) ? HIGH : LOW);
      b <<= 1;
      digitalWrite(LCD_SCLK, LOW);
      digitalWrite(LCD_SCLK, HIGH);
    }
  };
  sendByte(0x03); sendByte(0x00); sendByte(0xDA); sendByte(0x00);   // read RDID1
  pinMode(LCD_D0, INPUT_PULLUP);
  uint8_t id = 0;
  for (int i = 0; i < 8; i++) {
    digitalWrite(LCD_SCLK, LOW);
    delayMicroseconds(1);
    id = (id << 1) | (digitalRead(LCD_D0) ? 1 : 0);
    digitalWrite(LCD_SCLK, HIGH);
    delayMicroseconds(1);
  }
  digitalWrite(LCD_CS, HIGH);
  pinMode(LCD_D0, OUTPUT);
  return id;
}

void lcdCmd(uint8_t reg, const uint8_t* data = nullptr, size_t n = 0) {
  spi_transaction_t t = {};
  t.cmd = 0x02;                    // "write register", one data line
  t.addr = (uint32_t)reg << 8;
  if (n) {
    t.flags = SPI_TRANS_USE_TXDATA;
    t.length = n * 8;
    memcpy(t.tx_data, data, n);   // all our parameters are 4 bytes or fewer
  }
  spi_device_polling_transmit(dev, &t);
}
void lcdCmd1(uint8_t reg, uint8_t v) { lcdCmd(reg, &v, 1); }

void setWindow(int x0, int y0, int x1, int y1) {
  x0 += colOffset; x1 += colOffset;
  uint8_t c[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
  uint8_t r[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
  lcdCmd(0x2A, c, 4);
  lcdCmd(0x2B, r, 4);
}

// The ring is drawn into the canvas, so it is flushed like anything else.
void drawRing() {
  const float R = LCD_W / 2.0f, rIn = R - 6;
  for (int y = 0; y < LCD_H; y++) {
    float dy = y + 0.5f - R;
    if (fabsf(dy) >= R) continue;
    int outer = (int)sqrtf(R * R - dy * dy);
    int inner = fabsf(dy) < rIn ? (int)sqrtf(rIn * rIn - dy * dy) : 0;
    int cx = LCD_W / 2;
    cv.drawFastHLine(cx - outer, y, outer - inner, ringColor);
    cv.drawFastHLine(cx + inner, y, outer - inner, ringColor);
  }
}
}  // namespace

Canvas& canvas() { return cv; }
uint8_t displayPanelId() { return panelId; }
void displayRing(uint16_t color) {
  if (color != ringColor) { ringColor = color; ringChanged = true; }
}

bool displayBegin() {
  pinMode(LCD_EN, OUTPUT);
  digitalWrite(LCD_EN, HIGH);
  panelId = readPanelId();
  colOffset = panelId == 0x86 ? 0 : 6;
  Serial.printf("AMOLED panel id 0x%02X (%s)\n", panelId, panelId == 0x86 ? "SH8601" : "CO5300");

  spi_bus_config_t bus = {};
  bus.sclk_io_num = LCD_SCLK;
  bus.mosi_io_num = LCD_D0;
  bus.miso_io_num = LCD_D1;
  bus.quadwp_io_num = LCD_D2;
  bus.quadhd_io_num = LCD_D3;
  bus.max_transfer_sz = CHUNK_BYTES + 16;
  bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
  spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);

  spi_device_interface_config_t dc = {};
  dc.command_bits = 8;
  dc.address_bits = 24;
  dc.mode = 0;
  dc.clock_speed_hz = LCD_SPI_HZ;
  dc.spics_io_num = LCD_CS;
  dc.queue_size = 2;
  dc.flags = SPI_DEVICE_HALFDUPLEX;
  spi_bus_add_device(SPI2_HOST, &dc, &dev);

  for (int i = 0; i < 2; i++) dmaBuf[i] = (uint8_t*)heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA);

  // Panel start-up, after Waveshare's demo for each driver.
  lcdCmd(0x11);                    // sleep out
  delay(120);
  if (panelId == 0x86) {           // SH8601
    const uint8_t te[2] = {0x01, 0xD1};
    lcdCmd(0x44, te, 2);
    lcdCmd1(0x35, 0x00);
  } else {                         // CO5300
    lcdCmd1(0xC4, 0x80);
    lcdCmd1(0x63, 0xFF);
  }
  lcdCmd1(0x3A, 0x55);             // 16-bit colour
  lcdCmd1(0x36, 0x00);
  lcdCmd1(0x53, 0x20);
  lcdCmd1(0x51, 0x00);
  lcdCmd(0x29);                    // display on
  delay(10);

  cv.setColorDepth(16);
  bool ok = cv.createSprite(LCD_W, LCD_H) != nullptr;   // ~425 KB, lands in PSRAM
  cv.setSize(LCD_W, LCD_H);
  if (ok) { cv.clear(TFT_BLACK); displayFlush(); }
  lcdCmd1(0x51, LCD_BRIGHTNESS);
  return ok;
}

void displayFlush() {
  if (ringChanged || cv.allDirty()) { drawRing(); ringChanged = false; }
  int16_t x0, y0, x1, y1;
  if (!cv.takeDirty(x0, y0, x1, y1) || !dev) return;
  // The panel wants windows that start on an even pixel and end on an odd one.
  x0 &= ~1; y0 &= ~1;
  x1 |= 1;  y1 |= 1;
  if (x1 >= LCD_W) x1 = LCD_W - 1;
  if (y1 >= LCD_H) y1 = LCD_H - 1;
  const int w = x1 - x0 + 1;
  const int rowsPer = max(2, (int)(CHUNK_BYTES / (w * 2)) & ~1);
  const uint8_t* img = (const uint8_t*)cv.getPointer();   // already big-endian RGB565

  spi_transaction_t tr[2] = {};
  int inFlight = -1;
  int b = 0;
  for (int y = y0; y <= y1; y += rowsPer) {
    int rows = min(rowsPer, y1 - y + 1);
    uint8_t* dst = dmaBuf[b];
    for (int r = 0; r < rows; r++)
      memcpy(dst + r * w * 2, img + ((size_t)(y + r) * LCD_W + x0) * 2, w * 2);
    if (inFlight >= 0) {
      spi_transaction_t* done;
      spi_device_get_trans_result(dev, &done, portMAX_DELAY);
      inFlight = -1;
    }
    setWindow(x0, y, x1, y + rows - 1);
    spi_transaction_t& t = tr[b];
    t = {};
    t.flags = SPI_TRANS_MODE_QIO;  // pixels go out on all four lines
    t.cmd = 0x32;
    t.addr = 0x2C << 8;            // memory write
    t.length = rows * w * 16;
    t.tx_buffer = dst;
    spi_device_queue_trans(dev, &t, portMAX_DELAY);
    inFlight = b;
    b ^= 1;
  }
  if (inFlight >= 0) {
    spi_transaction_t* done;
    spi_device_get_trans_result(dev, &done, portMAX_DELAY);
  }
}
