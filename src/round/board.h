// BattleBuddies - pins and settings for the Waveshare ESP32-S3-Touch-AMOLED-1.43
// (466x466 round AMOLED, also sold in the CNC metal case as "-1.43-B").
// Pins are from Waveshare's wiki and demo code for this board.
#pragma once

// ---- AMOLED panel (SH8601 or CO5300 driver, QSPI) ----
#define LCD_CS     9
#define LCD_SCLK  10
#define LCD_D0    11
#define LCD_D1    12
#define LCD_D2    13
#define LCD_D3    14
#define LCD_RST   21
#define LCD_EN    42     // AMOLED power enable
#define LCD_W    466
#define LCD_H    466
#define LCD_SPI_HZ  40000000
#define LCD_BRIGHTNESS 0xD0   // 0x00-0xFF

// ---- I2C bus: touch, real-time clock and motion sensor share it ----
#define I2C_SDA   47
#define I2C_SCL   48
#define TOUCH_ADDR 0x38   // FT3168
#define RTC_ADDR   0x51   // PCF85063

// If taps land in the wrong place, use the Touch Test screen ("?" on the
// title screen) and flip these.
#define TOUCH_SWAP_XY 0
#define TOUCH_FLIP_X  0
#define TOUCH_FLIP_Y  0

// ---- Night forms ----
// This board has no light sensor, so "dark" means the on-board clock says
// it's night. Set the clock from the title screen ("Clock").
#define NIGHT_FROM_HOUR 20   // 8 pm
#define NIGHT_TO_HOUR    6   // 6 am
