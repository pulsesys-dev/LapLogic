// =====================================================================
//  LapLogic - motorsport desk display
//  config.h : board pins and behaviour you may want to tweak
//
//  The board is chosen automatically from the chip you compile for:
//    ESP32-S3  -> Guition JC4827W543C (4.3" 480x272, capacitive touch)
//    ESP32     -> ESP32-2432S028R "Cheap Yellow Display" (2.8" 320x240, resistive touch)
// =====================================================================
#pragma once
#include <Arduino.h>

#define APP_NAME      "LapLogic"
// "b1.7.0" = beta, "v1.8.0" = official release. Beta boards also take beta updates; official
// boards only take official ones. Tag each GitHub release with exactly this text.
#define APP_VERSION   "b1.7.1"
#define HOSTNAME      "laplogic"          // http://laplogic.local once connected
#define SETUP_AP_NAME "LapLogic-Setup"    // Wi-Fi hotspot shown during first setup
#define NVS_NAMESPACE "pitwall"          // internal storage name, kept from the project's old name so
                                         // boards updated from PitWall keep their settings and calibration

#if defined(CONFIG_IDF_TARGET_ESP32S3)
// ---------------------------------------------------------------------
//  Guition JC4827W543C : NV3041A on QSPI, GT911 touch
// ---------------------------------------------------------------------
  #define BOARD_JC4827 1
  #define BOARD_NAME   "Guition JC4827W543C"
  #define LCD_W   480
  #define LCD_H   272
  #define LCD_CS   45
  #define LCD_SCK  47
  #define LCD_D0   21
  #define LCD_D1   48
  #define LCD_D2   40
  #define LCD_D3   39
  #define LCD_BL    1
  #define LCD_SPEED_HZ 20000000UL        // raise to 32 MHz if you like; drop back if the image glitches
  #define TP_SDA    8
  #define TP_SCL    4
  #define TP_INT    3
  #define TP_RST   38
  // If taps register in the wrong place, flip these. (The "flip screen"
  // setting rotates display and touch together on top of this.)
  #define TOUCH_MIRROR_X 0
  #define TOUCH_MIRROR_Y 0
  // Portrait touch mapping is worked out from the panel's rotation registers. If taps land
  // diagonally opposite where you touch in portrait, set this to 1.
  #define TOUCH_PORTRAIT_FIX 0
  #define LV_BUF_LINES 40
  #define UI_LAZY_PAGES 0                // 8 MB PSRAM: keep every page built

#elif defined(CONFIG_IDF_TARGET_ESP32)
// ---------------------------------------------------------------------
//  ESP32-2432S028R "Cheap Yellow Display"
//  Display ILI9341 (1 USB port) or ST7789 (2 USB ports) on HSPI,
//  XPT2046 resistive touch on its own SPI pins
// ---------------------------------------------------------------------
  #define BOARD_CYD    1
  #define BOARD_NAME   "ESP32-2432S028R (CYD)"
  #define LCD_W   320
  #define LCD_H   240
  #define LCD_SCK   14
  #define LCD_MOSI  13
  #define LCD_MISO  12
  #define LCD_CS    15
  #define LCD_DC     2
  #define LCD_BL    21
  #define LCD_SPEED_HZ 40000000UL        // drop to 27 MHz if the image glitches
  #define TP_CLK    25
  #define TP_MOSI   32
  #define TP_MISO   39
  #define TP_CS     33
  #define TP_IRQ    36
  #define TP_Z_THRESHOLD 350             // touch pressure needed to count as a press
  #define LED_R      4                   // on-board RGB LED (active low): the race-status light
  #define LED_MAX  200                   // its brightest (0..255); it's very bright behind clear plastic
  #define LED_G     16
  #define LED_B     17
  #define LV_BUF_LINES 24
  #define UI_LAZY_PAGES 1                // no PSRAM: only build the page on screen
  // Light sensor (LDR) beside the screen, for auto-dim. If the screen gets brighter as the
  // room gets darker, set LDR_DARK_IS_HIGH to 0. The [mem] log line shows the raw reading.
  #define LDR_PIN   34
  #define LDR_DARK_IS_HIGH 1
  #define LDR_MIN_SPAN   120             // raw change between the brightest and darkest the room has been
  // MicroSD slot, read once at start-up for team logos (see docs/using.md, "Team logos")
  #define SD_SCK    18
  #define SD_MISO   19
  #define SD_MOSI   23
  #define SD_CS      5

#else
  #error "LapLogic supports the Guition JC4827W543C (ESP32-S3) and the ESP32-2432S028R CYD (ESP32). Pick one of those chips."
#endif

#ifndef BOARD_CYD
  #define BOARD_CYD 0
#endif
#ifndef BOARD_JC4827
  #define BOARD_JC4827 0
#endif

#ifndef LDR_DARK_IS_HIGH                 // boards without a light sensor
  #define LDR_DARK_IS_HIGH 1
  #define LDR_MIN_SPAN   120
#endif

#define COMPACT (LCD_W < 400)            // smaller layouts for the 320x240 screen

// ---- Behaviour -----------------------------------------------------
#define AUTO_ROTATE_DEFAULT_S  20        // seconds per page (0 = off), changeable in settings
#define TOUCH_PAUSE_MS      60000UL      // auto-rotate pauses this long after a touch
#define NIGHT_START_HOUR       22        // backlight dims between these hours
#define NIGHT_END_HOUR          7
#define BRIGHT_NIGHT           35        // 0..255, cap while dimmed at night
#define BRIGHT_MIN              8        // 0..255, never dimmer than this (auto-dim / slider)
#define AUTO_DIM_LOWEST        15        // %, the darkest room dims to this share of your brightness
#define WAKE_ON_TOUCH_MS    30000UL      // full brightness after a touch at night

// ---- Data ----------------------------------------------------------
#define API_BASE "https://api.jolpi.ca/ergast/f1/"
// Set as fast as each service usefully updates (see docs/using.md, "How fast it updates")
#define CAL_REFRESH_MS        (60UL * 60UL * 1000UL)    // race calendar (session times can move)
#define STANDINGS_REFRESH_MS  (10UL * 60UL * 1000UL)    // standings + last result
#define RESULTS_CHASE_MS      (60UL * 1000UL)           // after a race or sprint, until its results are out
#define RETRY_MS              (60UL * 1000UL)           // after a failed fetch
// ---- Updates over Wi-Fi: the GitHub repository whose releases carry the firmware (see docs/updates.md)
#define UPDATE_REPO      "pulsesys-dev/LapLogic"
#define UPDATE_CHECK_MS  (24UL * 3600UL * 1000UL)    // once a day
#if BOARD_CYD
  #define UPDATE_ASSET   "LapLogic-CYD-ESP32-2432S028R-ota.bin"
#else
  #define UPDATE_ASSET   "LapLogic-Guition-JC4827W543-ota.bin"
#endif
#define WEATHER_API "http://api.open-meteo.com/v1/forecast"
#define WEATHER_REFRESH_MS    (15UL * 60UL * 1000UL)    // weather (Open-Meteo recalculates every 15 minutes)
#define WEATHER_WEEKEND_MS    (15UL * 60UL * 1000UL)
