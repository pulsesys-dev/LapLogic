// Persistent user settings (stored in flash, survive reflashing the same layout)
#pragma once
#include <Arduino.h>

struct TzEntry {
  const char *iana;    // e.g. "Australia/Brisbane"
  const char *posix;   // e.g. "AEST-10"
};

extern const TzEntry TZ_TABLE[];
extern const size_t  TZ_COUNT;

struct Settings {
  String tzName;       // "auto" or an IANA name from TZ_TABLE
  String tzPosix;      // active POSIX TZ string
  String fav;          // favourite driver code, e.g. "PIA" (may be empty)
  uint8_t rotateS;     // seconds per page when auto-rotating (5..120)
  bool   rotateOn;     // auto-rotate pages
  bool   flip;         // rotate screen 180 degrees
  bool   portrait;     // portrait instead of landscape (takes effect after a restart)
  bool   nightDim;     // dim at night
  bool   h24;          // 24-hour clock
  uint8_t panel;       // CYD only: 0 = auto-detect, 1 = ILI9341, 2 = ST7789 (2 USB ports)
  uint8_t colours;     // CYD only: 0 = automatic, 1 = normal, 2 = inverted
  String ofUser;       // OpenF1 sponsor login for live track positions (4.3" board)
  String ofPass;
  uint8_t units;       // 0 = metric (C, km/h), 1 = imperial (F, mph)
  bool    light;       // light mode (dark text on a light background)
  uint8_t bright;      // screen brightness, % (5..100)
  bool    autoDim;     // CYD: follow the room's light using the light sensor
  bool    led;         // CYD: race-status light on the back
  bool    updCheck;    // look for firmware updates on GitHub once a day
};

extern Settings settings;

void        settingsLoad();
void        settingsSave();
const char *tzLookup(const String &iana);    // POSIX string or nullptr
String      tzFromOffset(long offsetSeconds);// fixed-offset fallback
void        applyTimezone();                 // pushes settings.tzPosix to the C library

// ---- every setting by name: shared by the on-screen buttons, the web page and USB setup
// Calls fn for each readable setting (the OpenF1 password is never read back).
void settingsForEach(void (*fn)(void *ctx, const char *key, const String &value), void *ctx);
// Changes one setting in s. Returns an empty string, or a short reason it was refused.
String settingsSet(Settings &s, const char *key, const String &value);
// Saves and puts changes into effect (time zone, screen, weather units...). Implemented in
// LapLogic.ino because it reaches every module. Returns true if a restart was scheduled.
bool settingsApply(const Settings &before);
// Ask the main loop to do something: "refresh" (F1 data), "restart", "wifi" (forget it and
// go back to setup), "calibrate" (touch, CYD). Also implemented in LapLogic.ino.
void appRequest(const char *what);
// Light sensor status for the settings pages ("" on boards without one). In LapLogic.ino.
String appLightStatus();
