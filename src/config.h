// BattleBuddies - hardware and tuning settings for the ESP32-2432S028R (CYD).
#pragma once

// ---- Touch controller (XPT2046 on its own SPI bus) ----
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25

// Raw touch range. If taps land in the wrong place, use the Touch Test
// screen (the "?" on the title screen) and adjust these.
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800
// Set to 1 if taps are mirrored left/right or up/down.
#define TOUCH_FLIP_X 0
#define TOUCH_FLIP_Y 0

// ---- Other on-board parts ----
#define SPEAKER_PIN   26   // "SPEAK" connector, via the on-board amp
#define LIGHT_PIN     34   // LDR next to the screen
#define LED_R_PIN      4   // RGB LED, active low
#define LED_G_PIN     16
#define LED_B_PIN     17

// Light sensor: the reading goes UP as it gets darker on most boards.
// Above this value the board counts as "dark" and night forms can appear.
#define LIGHT_DARK_THRESHOLD 700

// ---- Game tuning ----
#define MAX_COLLECTION     96
#define WILD_SCAN_MIN_MS 2500   // the radar runs at least this long
#define BLE_SCAN_SECONDS    2
#define SOUND_VOLUME       60   // 0-511; the speaker is loud

// ---- Friend battles (ESP-NOW) ----
#define FRIEND_CHANNEL      1      // both boards must use the same WiFi channel
#define LINK_RESEND_MS    250      // re-send the latest packet this often
#define LINK_TIMEOUT_MS 12000      // friend silent this long = connection lost
