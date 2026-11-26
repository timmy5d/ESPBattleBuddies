#include "hw.h"
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "config.h"

namespace {
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
bool wasDown = false;
uint32_t lastTap = 0;

QueueHandle_t noteQueue;
volatile bool soundEnabled = true;
const int SOUND_CH = 7;

void soundTask(void*) {
  Note n;
  for (;;) {
    if (xQueueReceive(noteQueue, &n, portMAX_DELAY) == pdTRUE) {
      if (n.freq && soundEnabled) {
        ledcWriteTone(SOUND_CH, n.freq);   // sets 50% duty at 10-bit resolution
        ledcWrite(SOUND_CH, SOUND_VOLUME); // then turn it down
      } else {
        ledcWrite(SOUND_CH, 0);
      }
      vTaskDelay(pdMS_TO_TICKS(n.ms));
      ledcWrite(SOUND_CH, 0);
    }
  }
}
}  // namespace

void hwBegin() {
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);   // read in landscape, convert to portrait below

  ledcSetup(SOUND_CH, 2000, 10);
  ledcAttachPin(SPEAKER_PIN, SOUND_CH);
  ledcWrite(SOUND_CH, 0);
  noteQueue = xQueueCreate(48, sizeof(Note));
  xTaskCreatePinnedToCore(soundTask, "sound", 2048, nullptr, 1, nullptr, 0);

  pinMode(LED_R_PIN, OUTPUT); pinMode(LED_G_PIN, OUTPUT); pinMode(LED_B_PIN, OUTPUT);
  ledOff();

  analogSetPinAttenuation(LIGHT_PIN, ADC_0db);   // the LDR swing is small
  pinMode(LIGHT_PIN, INPUT);
}

bool touchDown(int& x, int& y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < 200) return false;
  // Landscape position first (the usual CYD calibration), then rotate to portrait.
  int lx = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 319);
  int ly = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 239);
  x = 239 - ly;
  y = lx;
  if (TOUCH_FLIP_X) x = 239 - x;
  if (TOUCH_FLIP_Y) y = 319 - y;
  x = constrain(x, 0, 239);
  y = constrain(y, 0, 319);
  return true;
}

bool touchTapped(int& x, int& y) {
  bool down = touchDown(x, y);
  bool tapped = down && !wasDown && millis() - lastTap > 120;
  wasDown = down;
  if (tapped) lastTap = millis();
  return tapped;
}

void playNotes(const Note* notes, size_t count) {
  if (!soundEnabled) return;
  for (size_t i = 0; i < count; i++) xQueueSend(noteQueue, &notes[i], 0);
}
void setSoundOn(bool on) { soundEnabled = on; if (!on) xQueueReset(noteQueue); }
bool soundOn() { return soundEnabled; }

// Original little jingles.
void sfxClick()    { static const Note n[] = {{1800, 18}}; playNotes(n, 1); }
void sfxScanPing() { static const Note n[] = {{1320, 30}, {0, 20}, {1760, 40}}; playNotes(n, 3); }
void sfxFound()    { static const Note n[] = {{523, 70}, {659, 70}, {784, 70}, {1047, 160}}; playNotes(n, 4); }
void sfxThrow()    { static const Note n[] = {{900, 25}, {700, 25}, {500, 40}}; playNotes(n, 3); }
void sfxCaught()   { static const Note n[] = {{784, 90}, {0, 30}, {784, 90}, {988, 90}, {1175, 90}, {1568, 260}}; playNotes(n, 6); }
void sfxMiss()     { static const Note n[] = {{330, 90}, {262, 160}}; playNotes(n, 2); }
void sfxFled()     { static const Note n[] = {{494, 90}, {415, 90}, {349, 90}, {262, 240}}; playNotes(n, 4); }

void ledColor(bool r, bool g, bool b) {
  digitalWrite(LED_R_PIN, r ? LOW : HIGH);
  digitalWrite(LED_G_PIN, g ? LOW : HIGH);
  digitalWrite(LED_B_PIN, b ? LOW : HIGH);
}
void ledOff() { ledColor(false, false, false); }

int lightReading() { return analogRead(LIGHT_PIN); }
bool isDark() { return lightReading() > LIGHT_DARK_THRESHOLD; }
