#include "net.h"
#include "config.h"
#include "settings.h"
#include "improv.h"
#include "web.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static WiFiManager wm;
static NetState state = NET_CONNECTING;
static bool wasConnected = false;
static bool mdnsUp = false;
static uint32_t lastRetry = 0;
static uint32_t setupSince = 0;

static ImprovSerial *improvWho = nullptr;
static uint32_t improvStart = 0;

static bool tzAutoPending = false;
static uint8_t tzAttempts = 0;
static uint32_t tzLastTry = 0;
static String tzDetected;

// ----------------------------------------------------------- time zone

static void detectTimezone() {
  tzLastTry = millis();
  tzAttempts++;
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin("http://ip-api.com/json/?fields=status,timezone,offset")) return;
  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString()) && doc["status"] == "success") {
      String iana = doc["timezone"] | "";
      const char *posix = tzLookup(iana);
      const Settings before = settings;
      settings.tzPosix = posix ? String(posix) : tzFromOffset(doc["offset"] | 0L);
      tzDetected = iana;
      tzAutoPending = false;
      settingsApply(before);
      Serial.printf("[net] time zone %s -> %s\n", iana.c_str(), settings.tzPosix.c_str());
    }
  }
  http.end();
}

// ----------------------------------------------------------- lifecycle

static void onConnected() {
  state = NET_ONLINE;
  WiFi.setAutoReconnect(true);
  Serial.printf("[net] online %s\n", WiFi.localIP().toString().c_str());
  configTzTime(settings.tzPosix.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  applyTimezone();
  if (!mdnsUp && MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
  }
  if (settings.tzName == "auto" && tzDetected.isEmpty()) { tzAutoPending = true; tzAttempts = 0; tzLastTry = 0; }
}

void netBegin(bool forceSetup) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);

  wm.setDebugOutput(false);
  wm.setConfigPortalBlocking(false);
  wm.setTitle(APP_NAME);
  wm.setHostname(HOSTNAME);
  wm.setDarkMode(true);
  wm.setShowInfoUpdate(false);
  wm.setShowInfoErase(false);
  // WiFiManager only runs the first-time hotspot now; settings live on our own page (web.cpp)
  std::vector<const char *> menu = {"wifi", "info", "sep", "restart"};
  wm.setMenu(menu);
  wm.setConnectTimeout(20);
  wm.setAPCallback([](WiFiManager *) { state = NET_SETUP; setupSince = millis(); });

  if (forceSetup) {
    wm.startConfigPortal(SETUP_AP_NAME);
    state = NET_SETUP;
    setupSince = millis();
  } else if (wm.autoConnect(SETUP_AP_NAME)) {
    onConnected();
    wasConnected = true;
  }
}

void netLoop() {
  wm.process();

  const bool c = WiFi.status() == WL_CONNECTED;

  // finishing an Improv (USB) provisioning attempt
  if (improvWho) {
    if (c) {
      improvWho->provisioned(netUrl());
      improvWho = nullptr;
    } else if (millis() - improvStart > 20000) {
      improvWho->failed();
      improvWho = nullptr;
      wm.startConfigPortal(SETUP_AP_NAME);
      state = NET_SETUP;
      setupSince = millis();
    }
  }

  if (c && !wasConnected) onConnected();
  if (!c && wasConnected) { state = NET_OFFLINE; lastRetry = millis(); }
  wasConnected = c;

  // our settings page once we're online; the setup hotspot needs port 80 while it runs
  if (c && !wm.getConfigPortalActive()) webStart();
  else webStop();
  webLoop();

  // known network but it isn't answering: give the setup hotspot 5 minutes, then keep retrying
  if (!c && wm.getConfigPortalActive() && wm.getWiFiIsSaved() && millis() - setupSince > 300000UL) {
    wm.stopConfigPortal();
    WiFi.mode(WIFI_STA);
    state = NET_OFFLINE;
    lastRetry = 0;
  }
  if (!c && !wm.getConfigPortalActive() && !improvWho && millis() - lastRetry > 30000UL) {
    lastRetry = millis();
    if (wm.getWiFiIsSaved()) WiFi.begin();
    if (state != NET_SETUP) state = NET_OFFLINE;
  }

  if (c && tzAutoPending && tzAttempts < 5 && (tzLastTry == 0 || millis() - tzLastTry > 60000UL))
    detectTimezone();
}

void netImprovConnect(const String &ssid, const String &pass, ImprovSerial *who) {
  webStop();
  if (wm.getConfigPortalActive()) wm.stopConfigPortal();
  WiFi.disconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  wasConnected = false;
  improvWho = who;
  improvStart = millis();
  state = NET_CONNECTING;
}

void netResetWifi() {
  wm.resetSettings();
  delay(300);
  ESP.restart();
}

NetState netState() { return state; }
String   netUrl()   { return "http://" + WiFi.localIP().toString(); }
String   netSsid()  { return WiFi.SSID(); }
int      netRssi()  { return WiFi.RSSI(); }
bool     netTimeValid() { return time(nullptr) > 1700000000; }

String netTzLabel() {
  if (settings.tzName != "auto") return settings.tzName;
  if (tzDetected.length()) return tzDetected + " (auto)";
  return "Detecting...";
}

void netDetectTimezone() {
  tzDetected = "";
  tzAutoPending = true;
  tzAttempts = 0;
  tzLastTry = 0;
}
