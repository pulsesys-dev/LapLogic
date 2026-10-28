// =====================================================================
//  LapLogic - motorsport desk display
//  Boards: Guition JC4827W543C (ESP32-S3, 4.3" 480x272, capacitive touch)
//          ESP32-2432S028R "Cheap Yellow Display" (ESP32, 2.8" 320x240, resistive)
//          The board is picked automatically from the chip - see config.h
//  Data  : Jolpica F1 API (Ergast-compatible), OpenF1 (track map) and Open-Meteo
//          (weather) - no accounts needed
//
//  Build with Arduino IDE or PlatformIO - see docs/building.md for the exact
//  board settings and library versions.
// =====================================================================

#include "config.h"
#include "settings.h"
#include "board.h"
#include "net.h"
#include "improv.h"
#include "f1data.h"
#include "ui.h"
#include "trackdata.h"
#include "logos.h"
#include "led.h"
#include "ota.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

// Improv (USB Wi-Fi setup) listens on every serial port wired to USB
#if ARDUINO_USB_CDC_ON_BOOT
  static ImprovSerial improvA(Serial);     // native USB
  static ImprovSerial improvB(Serial0);    // UART, if the board has a USB-serial chip
  #define HAVE_IMPROV_B 1
#else
  static ImprovSerial improvA(Serial);     // UART
  #if ARDUINO_USB_MODE
    static ImprovSerial improvB(USBSerial);
    #define HAVE_IMPROV_B 1
  #endif
#endif

// Data is fetched on core 0 into `work`, published to `shared`, then copied
// to `view` for the UI on core 1 (each copy ~12 KB).
static F1Data *shared = nullptr, *work = nullptr, *view = nullptr;
static SemaphoreHandle_t dataLock;
static volatile bool dataDirty = false;
static volatile bool refreshNow = false;
static volatile bool weatherNow = false;     // units changed: fetch the weather again

static void publish() {
  xSemaphoreTake(dataLock, portMAX_DELAY);
  memcpy(shared, work, sizeof(F1Data));
  dataDirty = true;
  xSemaphoreGive(dataLock);
}

// True from about when a race or sprint finishes until Jolpica has its results: check every
// minute then, so new results and standings show up within a minute of being published.
static bool chasingResults(const F1Data &d, time_t now) {
  for (int i = 0; i < d.nRaces; i++) {
    const Race &r = d.races[i];
    for (int k = 0; k < r.ns; k++) {
      const Session &s = r.s[k];
      if (s.kind != S_RACE && s.kind != S_SPRINT) continue;
      const long since = (long)(now - s.start);
      const long from = s.kind == S_RACE ? 85 * 60 : 30 * 60;           // a race runs ~1.5 h, a sprint ~0.5 h
      const long until = s.kind == S_RACE ? 8 * 3600 : 3 * 3600;         // give up after this
      if (since < from || since > until) continue;
      if (s.kind == S_RACE && d.lastRound >= r.round) continue;          // already got this race's result
      return true;
    }
  }
  return false;
}

static void fetchTask(void *) {
  uint32_t nextCal = 0, nextStand = 0, nextWx = 0;
  int wxRound = -1;
  for (;;) {
    otaLoop();   // firmware updates: a daily check, and the install when you say yes
    if (WiFi.status() == WL_CONNECTED && netTimeValid()) {
      const uint32_t now = millis();
      if (refreshNow) { refreshNow = false; nextCal = nextStand = nextWx = now; }
      if (weatherNow) { weatherNow = false; nextWx = now; }
      // a race just finished: start checking for its results now, not at the next 10-minute refresh
      if ((int32_t)(nextStand - now) > (int32_t)RESULTS_CHASE_MS && chasingResults(*work, time(nullptr))) nextStand = now;

      if ((int32_t)(now - nextCal) >= 0) {
        bool ok = fetchCalendar(*work, "current");
        if (ok && nextRaceIndex(*work, time(nullptr)) < 0) {
          // season over: show next year's calendar as soon as it's published
          char y[8];
          snprintf(y, sizeof(y), "%u", work->season + 1);
          fetchCalendar(*work, y);
        }
        if (ok) publish();
        nextCal = millis() + (ok ? CAL_REFRESH_MS : RETRY_MS);
        Serial.printf("[f1] calendar refresh %s, next in %lu min\n", ok ? "ok" : "FAILED",
                      (unsigned long)((nextCal - millis()) / 60000));
      }

      if ((int32_t)(now - nextStand) >= 0) {
        bool a = fetchDriverStandings(*work);
        bool b = fetchTeamStandings(*work);
        bool c = fetchLastResults(*work);
        if (a || b || c) publish();
        const bool chase = chasingResults(*work, time(nullptr));
        nextStand = millis() + (chase ? RESULTS_CHASE_MS : (a && b && c) ? STANDINGS_REFRESH_MS : RETRY_MS);
        Serial.printf("[f1] standings %s, constructors %s, results %s, next in %lu s%s\n",
                      a ? "ok" : "FAILED", b ? "ok" : "FAILED", c ? "ok" : "FAILED",
                      (unsigned long)((nextStand - millis()) / 1000), chase ? " (waiting for results)" : "");
      }

      // weather at the next circuit
      if (work->nRaces) {
        const time_t t = time(nullptr);
        const int ri = nextRaceIndex(*work, t);
        const int round = ri >= 0 ? work->races[ri].round : -1;
        if (ri >= 0 && (round != wxRound || (int32_t)(millis() - nextWx) >= 0)) {
          const bool ok = fetchWeather(*work, ri, settings.units == 1);
          if (ok) { publish(); wxRound = round; }
          const bool weekend = work->races[ri].ns && t >= work->races[ri].s[0].start - 2 * 86400;
          nextWx = millis() + (ok ? (weekend ? WEATHER_WEEKEND_MS : WEATHER_REFRESH_MS) : RETRY_MS);
          if (!ok) wxRound = round;   // don't retry every second; wait for nextWx
        }
      }

#if !BOARD_CYD
      trackUpdate(*work);   // track map: decides for itself when it needs the network
#endif
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ---------------------------------------------------------------- settings changes
// Called by the on-screen buttons, the web page (laplogic.local) and USB setup.

static volatile bool settingsDirty = false;   // redraw with the new settings on the next loop
static volatile bool themeDirty = false;      // light/dark mode changed: rebuild every screen
static uint32_t restartAt = 0;                // millis() to restart at (0 = not planned)
static const char *restartWhy = "Restarting...";
static volatile bool reqCalibrate = false, reqWifiReset = false;
static uint32_t wifiResetAt = 0;

static void planRestart(const char *why, uint32_t inMs) {
  restartWhy = why;
  restartAt = (millis() + inMs) | 1;
}

bool settingsApply(const Settings &b) {
  const Settings &s = settings;
  settingsSave();
  if (s.tzName != b.tzName || s.tzPosix != b.tzPosix) {
    applyTimezone();
    if (s.tzName == "auto" && b.tzName != "auto") netDetectTimezone();
  }
  if (s.units != b.units) weatherNow = true;
  if (s.ofUser != b.ofUser || s.ofPass != b.ofPass) trackLoginChanged();
  // brightness, auto-dim, night dimming and scroll timing take effect without redrawing
  if (s.fav != b.fav || s.h24 != b.h24 || s.tzPosix != b.tzPosix || s.units != b.units || s.flip != b.flip ||
      s.colours != b.colours || s.ofUser != b.ofUser)
    settingsDirty = true;
  if (s.light != b.light) themeDirty = true;
  if (s.portrait != b.portrait || s.panel != b.panel) {
    planRestart(s.portrait != b.portrait ? (s.portrait ? "Switching to portrait..." : "Switching to landscape...")
                                         : "Restarting with the new display type...", 1500);
    return true;
  }
  return false;
}

void appRequest(const char *what) {
  if (!strcmp(what, "refresh")) refreshNow = true;
  else if (!strcmp(what, "restart")) planRestart("Restarting...", 1000);
  else if (!strcmp(what, "wifi")) { reqWifiReset = true; wifiResetAt = millis() + 1000; }
  else if (!strcmp(what, "calibrate")) reqCalibrate = true;
  else if (!strcmp(what, "ledtest")) ledTest();
  else if (!strcmp(what, "updcheck")) otaCheckNow();
  else if (!strcmp(what, "update")) otaInstall();
}

// ---------------------------------------------------------------- brightness
// Your brightness setting, scaled down in a dark room (CYD light sensor, if auto-dim is on)
// and capped at night (if night dimming is on). Changes fade rather than jump.

static float ambientDark = -1;        // smoothed sensor reading, "higher = darker"
static float ambientLo = 1e9, ambientHi = -1e9;   // brightest and darkest seen since start-up
static uint8_t ambientPct = 100;      // what auto-dim is applying, %

static void readAmbient() {
  static uint32_t last = 0;
  if (millis() - last < 100) return;
  last = millis();
  const int raw = boardLightRaw();
  if (raw < 0) return;
  const float d = LDR_DARK_IS_HIGH ? raw : 4095 - raw;
  ambientDark = ambientDark < 0 ? d : ambientDark + (d - ambientDark) * 0.05f;   // ~2 s smoothing
  ambientLo = min(ambientLo, ambientDark);
  ambientHi = max(ambientHi, ambientDark);
  const float span = max(ambientHi - ambientLo, (float)LDR_MIN_SPAN);
  const float t = constrain((ambientDark - ambientLo) / span, 0.0f, 1.0f);      // 0 = brightest, 1 = darkest
  ambientPct = (uint8_t)(100 - t * (100 - AUTO_DIM_LOWEST));
}

String appLightStatus() {
  if (boardLightRaw() < 0) return "";
  char b[64];
  snprintf(b, sizeof(b), "reading %d, %s %u%%", boardLightRaw(), settings.autoDim ? "auto-dim at" : "auto-dim off, would be",
           (unsigned)ambientPct);
  return b;
}

static int backlightNow = 255;   // what the backlight is at (for the race light)

static void updateBacklight() {
  static uint32_t lastStep = 0;
  static int &current = backlightNow;
  static bool first = true;
  readAmbient();
  int want = 255 * settings.bright / 100;
  if (settings.autoDim && boardLightRaw() >= 0) want = want * ambientPct / 100;
  if (settings.nightDim && netTimeValid()) {
    time_t now = time(nullptr);
    struct tm tm; localtime_r(&now, &tm);
    const bool night = tm.tm_hour >= NIGHT_START_HOUR || tm.tm_hour < NIGHT_END_HOUR;
    const uint32_t lt = boardLastTouchMs();
    const bool recentTouch = lt && millis() - lt < WAKE_ON_TOUCH_MS;
    if (night && !recentTouch) want = min(want, BRIGHT_NIGHT);
  }
  want = max(want, BRIGHT_MIN);
  if (first) { first = false; current = want; boardSetBacklight(want); return; }
  if (want == current || millis() - lastStep < 15) return;
  lastStep = millis();
  current += constrain(want - current, -6, 6);   // full range in about half a second
  boardSetBacklight(current);
}

static void pumpUi(uint32_t ms) {
  const uint32_t until = millis() + ms;
  while ((int32_t)(millis() - until) < 0) { boardLoop(); delay(5); }
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);          // don't stall when no USB host is listening
  Serial0.begin(115200);
#elif ARDUINO_USB_MODE
  USBSerial.begin(115200);
  USBSerial.setTxTimeoutMs(0);
#endif
  Serial.println("\n" APP_NAME " " APP_VERSION);

  settingsLoad();
  applyTimezone();
  logosBegin();   // before the display: on the CYD the SD card shares the touch screen's bus
  boardBegin();
#if BOARD_CYD
  ledBegin();
  // No Track page on the CYD: clear out circuit maps and replays an earlier version saved
  if (File root = LittleFS.open("/")) {
    String gone[40]; int n = 0;
    for (File f = root.openNextFile(); f && n < 40; f = root.openNextFile()) {
      String name = f.name();
      if (name.startsWith("/")) name = name.substring(1);
      if (name.startsWith("o_") || name.startsWith("r_")) gone[n++] = "/" + name;
      f.close();
    }
    for (int i = 0; i < n; i++) LittleFS.remove(gone[i]);
    if (n) Serial.printf("[map] removed %d saved track files (no Track page on this board)\n", n);
  }
#else
  trackBegin();   // early, while memory is still in one piece
#endif
#if defined(CONFIG_IDF_TARGET_ESP32)
  {   // LVGL is compiled with the lv_conf.h in the Arduino libraries folder, not this one
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    if (m.total_size < LV_MEM_SIZE * 9 / 10)
      Serial.println("!! WARNING: the lv_conf.h in your Arduino libraries folder is out of date.\n"
                     "!! Copy LapLogic/lv_conf.h over it and rebuild, or the CYD will run low on memory.");
  }
#endif
  uiBegin();

  // Resistive touch (CYD) needs calibrating once
  if (boardCanCalibrate() && !boardIsCalibrated()) boardCalibrate();

  // Touch during start-up: tap = Wi-Fi setup, hold (CYD) = recalibrate touch
  uiShowSplash(boardCanCalibrate() ? "Tap: Wi-Fi setup    Hold: recalibrate touch"
                                   : "Touch the screen now to change Wi-Fi");
  pumpUi(2500);
  bool forceSetup = boardLastTouchMs() != 0;
  if (forceSetup && boardCanCalibrate()) {
    const uint32_t t0 = millis();
    while (boardTouching() && millis() - t0 < 4000) pumpUi(50);
    if (boardTouching()) {
      uiShowSplash("Let go to start touch calibration");
      while (boardTouching()) pumpUi(50);
      boardCalibrate();
      forceSetup = false;
    }
  }

  shared = (F1Data *)ps_calloc(1, sizeof(F1Data));
  work   = (F1Data *)ps_calloc(1, sizeof(F1Data));
  view   = (F1Data *)ps_calloc(1, sizeof(F1Data));
  if (!shared || !work || !view) {
    // No PSRAM (e.g. the CYD): keep two copies in internal RAM and let the UI
    // read the shared copy directly, holding the lock while it rebuilds.
    free(shared); free(work); free(view);
    shared = (F1Data *)calloc(1, sizeof(F1Data));
    work   = (F1Data *)calloc(1, sizeof(F1Data));
    view   = shared;
  }
  dataLock = xSemaphoreCreateMutex();

  uiShowSplash(forceSetup ? "Starting Wi-Fi setup..." : "Connecting to Wi-Fi...");
  pumpUi(50);
  netBegin(forceSetup);

  xTaskCreatePinnedToCore(fetchTask, "f1fetch", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  netLoop();
  improvA.loop();
#ifdef HAVE_IMPROV_B
  improvB.loop();
#endif

  // which screen to show
  static NetState shown = (NetState)255;
  static bool everOnline = false;
  const NetState ns = netState();
  if (ns != shown) {
    shown = ns;
    if (ns == NET_ONLINE) { everOnline = true; uiShowMain(); }
    else if (ns == NET_SETUP) uiShowSetup(WiFi.SSID().length() ? "Couldn't reach your saved Wi-Fi" : "");
    else if (!everOnline) uiShowSplash(ns == NET_CONNECTING ? "Connecting to Wi-Fi..." : "Wi-Fi offline - retrying");
    else uiShowMain();   // dropped out after being online: keep showing data
  }

  if (dataDirty) {
    xSemaphoreTake(dataLock, portMAX_DELAY);
    dataDirty = false;
    if (view != shared) {
      memcpy(view, shared, sizeof(F1Data));
      xSemaphoreGive(dataLock);
      uiSetData(view);
    } else {
      uiSetData(view);
      xSemaphoreGive(dataLock);
    }
  }

  // memory report for troubleshooting
  static uint32_t lastHeapLog = 0;
  if (millis() - lastHeapLog > 60000UL) {
    lastHeapLog = millis();
    if (boardLightRaw() >= 0) Serial.printf("[ldr] %s\n", appLightStatus().c_str());
    Serial.printf("[mem] free %u  min %u  largest block %u", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#if LV_MEM_CUSTOM == 0
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    static uint8_t peakPct = 0;
    peakPct = max(peakPct, m.used_pct);
    Serial.printf("  | ui %u%% used (peak %u%% of %u KB)", m.used_pct, peakPct, (unsigned)(m.total_size / 1024));
#endif
    Serial.println();
  }

  if (themeDirty) {
    themeDirty = false;
    settingsDirty = false;
    boardSetFlip(settings.flip);
    boardSetColours(settings.colours);
    uiApplyTheme();
  }
  if (settingsDirty) {
    settingsDirty = false;
    boardSetFlip(settings.flip);
    boardSetColours(settings.colours);
    uiRebuild();
  }
  if (reqCalibrate && boardCanCalibrate()) { reqCalibrate = false; boardCalibrate(); uiShowMain(); }
  if (reqWifiReset && (int32_t)(millis() - wifiResetAt) >= 0) { reqWifiReset = false; netResetWifi(); }
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) {
    uiShowSplash(restartWhy);
    pumpUi(400);
    ESP.restart();
  }

  // firmware update: ask once per new version, then show progress while it installs
  {
    static String asked;
    static uint32_t lastShown = 0, failedAt = 0;
    static OtaState was = OTA_IDLE;
    const OtaState st = otaState();
    if (st == OTA_AVAILABLE && asked != otaVersion() && lv_scr_act() != nullptr && netState() == NET_ONLINE) {
      asked = otaVersion();
      uiPromptUpdate(otaVersion(), otaNotes().c_str());
    }
    if ((st == OTA_DOWNLOADING || st == OTA_DONE) && millis() - lastShown > 400) {
      lastShown = millis();
      char b[96];
      if (st == OTA_DONE) snprintf(b, sizeof(b), "Updated to %s. Restarting...", otaVersion());
      else snprintf(b, sizeof(b), "Updating to %s... %d%%\nKeep LapLogic plugged in", otaVersion(), otaProgress());
      uiShowSplash(b);
    }
    if (st == OTA_FAILED && was == OTA_DOWNLOADING) {
      failedAt = millis() | 1;
      uiShowSplash(("Update didn't finish: " + otaStatus().substring(otaStatus().indexOf(':') + 2)).c_str());
    }
    if (failedAt && millis() - failedAt > 6000) { failedAt = 0; uiShowMain(); }
    was = st;
  }

  uiTick();
  updateBacklight();
  if (ledSupported() && xSemaphoreTake(dataLock, 0) == pdTRUE) {   // race light (CYD)
    ledLoop(view && view->nRaces ? view : nullptr, backlightNow);
    xSemaphoreGive(dataLock);
  }
  boardLoop();
  delay(5);
}
