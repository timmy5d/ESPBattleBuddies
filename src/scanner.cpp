#include "scanner.h"
#include <WiFi.h>
#include <NimBLEDevice.h>
#include "config.h"
#include "hw.h"
#include "monster.h"

namespace {
struct Candidate { uint8_t id[6]; int8_t rssi; uint8_t source; };
const int MAX_CANDIDATES = 48;
Candidate cands[MAX_CANDIDATES];
int candCount = 0;
uint16_t wifiCount = 0, bleCount = 0;

ScanPhase phase = SCAN_IDLE;
volatile bool bleDone = false;
ScanResult result{};

void onBleDone(NimBLEScanResults) { bleDone = true; }

void addCandidate(const uint8_t* id, int rssi, uint8_t source) {
  if (candCount >= MAX_CANDIDATES) return;
  Candidate& c = cands[candCount++];
  memcpy(c.id, id, 6);
  c.rssi = (int8_t)constrain(rssi, -127, 0);
  c.source = source;
}

// Pick one signal, favouring strong (nearby) ones.
void choose() {
  result = {};
  result.wifiCount = wifiCount;
  result.bleCount = bleCount;
  result.night = isDark();
  if (candCount == 0) {
    // Nothing on the air: a rare "static" monster made from sensor noise.
    uint8_t noise[6];
    uint32_t r = esp_random();
    memcpy(noise, &r, 4);
    noise[4] = lightReading() & 0xFF;
    noise[5] = millis() & 0xFF;
    result.found = true;
    result.seed = seedFromBytes(noise, 6, SRC_STATIC);
    result.source = SRC_STATIC;
    result.rssi = 0;
    return;
  }
  uint32_t total = 0;
  for (int i = 0; i < candCount; i++) total += max(1, cands[i].rssi + 100);
  uint32_t pick = esp_random() % total;
  int idx = 0;
  for (; idx < candCount - 1; idx++) {
    uint32_t w = max(1, cands[idx].rssi + 100);
    if (pick < w) break;
    pick -= w;
  }
  const Candidate& c = cands[idx];
  result.found = true;
  result.seed = seedFromBytes(c.id, 6, c.source);
  result.source = c.source;
  result.rssi = c.rssi;
}
}  // namespace

void scannerBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  NimBLEDevice::init("");
  NimBLEScan* s = NimBLEDevice::getScan();
  s->setActiveScan(false);   // just listen, never connect to anything
  s->setInterval(100);
  s->setWindow(90);
}

void scannerStart() {
  candCount = 0; wifiCount = 0; bleCount = 0;
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);   // async, include hidden networks
  phase = SCAN_WIFI;
}

ScanPhase scannerPoll() {
  if (phase == SCAN_WIFI) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return phase;
    if (n > 0) {
      wifiCount = n;
      for (int i = 0; i < n; i++) {
        uint8_t* bssid = WiFi.BSSID(i);
        if (bssid) addCandidate(bssid, WiFi.RSSI(i), SRC_WIFI);
      }
    }
    WiFi.scanDelete();
    bleDone = false;
    NimBLEScan* s = NimBLEDevice::getScan();
    s->clearResults();
    if (!s->start(BLE_SCAN_SECONDS, onBleDone, false)) bleDone = true;
    phase = SCAN_BLE;
  } else if (phase == SCAN_BLE) {
    if (!bleDone) return phase;
    NimBLEScanResults res = NimBLEDevice::getScan()->getResults();
    bleCount = res.getCount();
    for (uint32_t i = 0; i < res.getCount(); i++) {
      NimBLEAdvertisedDevice d = res.getDevice(i);
      addCandidate(d.getAddress().getNative(), d.getRSSI(), SRC_BLE);
    }
    NimBLEDevice::getScan()->clearResults();
    choose();
    phase = SCAN_DONE;
  }
  return phase;
}

ScanResult scannerResult() { return result; }
