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
enum ScanMode  : uint8_t { MODE_WIFI, MODE_BLE, MODE_BOTH };   // BOTH = WiFi, then Bluetooth (Battle Bot 9000)

// One signal heard in the last scan, strongest first. The player can pick
// one on the radar instead of letting the scanner choose.
struct ScanSignal {
  uint32_t seed;
  uint8_t  source;     // Source enum from monster.h
  int8_t   rssi;
  char     name[21];   // WiFi network name or Bluetooth device name ("" if none)
};

void      scannerBegin();
void      scannerStart(ScanMode mode);   // non-blocking; call scannerPoll() every frame
ScanPhase scannerPoll();
ScanResult scannerResult();      // the scanner's own pick (used when nothing is heard)
int        scannerSignalCount(); // signals from the last finished scan
ScanSignal scannerSignal(int i); // 0 = strongest
