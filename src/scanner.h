// BattleBuddies - "scanning" the world for monsters.
// The board has no barcode reader, so it listens to the radio signals around
// it instead. Each WiFi router has a fixed hardware address, so the same
// router always hatches the same monster. Bluetooth gadgets (phones, watches,
// headphones) often change their address, so they give wandering monsters.
#pragma once
#include <Arduino.h>

struct ScanResult {
  bool     found;
  uint32_t seed;
  uint8_t  source;     // Source enum from monster.h
  int8_t   rssi;
  bool     night;
  uint16_t wifiCount, bleCount;
};

enum ScanPhase : uint8_t { SCAN_IDLE, SCAN_WIFI, SCAN_BLE, SCAN_DONE };
enum ScanMode  : uint8_t { MODE_WIFI, MODE_BLE };

void      scannerBegin();
void      scannerStart(ScanMode mode);   // non-blocking; call scannerPoll() every frame
ScanPhase scannerPoll();
ScanResult scannerResult();
