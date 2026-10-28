#include "web.h"
#include "config.h"
#include "settings.h"
#include "net.h"
#include "board.h"
#include "ui.h"
#include "trackdata.h"
#include "webpage.h"
#include "logos.h"
#include "led.h"
#include "ota.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

static WebServer *server = nullptr;

// ---------------------------------------------------------------- helpers

static void sendJson(int code, JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server->sendHeader("Cache-Control", "no-store");
  server->send(code, "application/json", out);
}

static void addValues(JsonObject v) {
  settingsForEach([](void *ctx, const char *key, const String &value) {
    (*(JsonObject *)ctx)[key] = value;
  }, &v);
}

// ---------------------------------------------------------------- routes

static void handlePage() {
  server->sendHeader("Content-Encoding", "gzip");
  server->sendHeader("Cache-Control", "no-cache");
  server->send_P(200, "text/html", (const char *)SETTINGS_PAGE_GZ, SETTINGS_PAGE_GZ_LEN);
}

static void handleGet() {
  JsonDocument doc;
  addValues(doc["values"].to<JsonObject>());

  JsonArray tz = doc["timezones"].to<JsonArray>();
  for (size_t i = 0; i < TZ_COUNT; i++) tz.add(TZ_TABLE[i].iana);

  JsonArray drivers = doc["drivers"].to<JsonArray>();
  if (const F1Data *d = uiData()) {
    for (int i = 0; i < d->nDrivers; i++) {
      JsonArray row = drivers.add<JsonArray>();
      row.add(d->drivers[i].code);
      row.add(d->drivers[i].last);
    }
  }

  JsonObject st = doc["status"].to<JsonObject>();
  st["board"] = BOARD_NAME;
  st["panel"] = boardPanelName();
  st["ip"] = netUrl();
  st["ssid"] = netSsid();
  st["rssi"] = netRssi();
  st["tz"] = netTzLabel();
  st["updated"] = uiDataSummary();
  st["weather"] = uiWeatherSummary();
  if (trackLiveSupported()) st["live"] = trackLiveStatus();
  st["logos"] = logosStatus();
  st["firmware"] = otaStatus();
  if (otaState() == OTA_AVAILABLE) {
    JsonObject u = doc["update"].to<JsonObject>();
    u["version"] = otaVersion();
    u["notes"] = otaNotes();
  }
  if (ledSupported()) st["led"] = ledStatus();
  const String light = appLightStatus();
  if (light.length()) st["light"] = light;
  char heap[64];
  snprintf(heap, sizeof(heap), "%u KB (largest block %u KB)", (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
  st["heap"] = heap;
  sendJson(200, doc);
}

static void handlePost() {
  JsonDocument in;
  if (deserializeJson(in, server->arg("plain")) || !in.is<JsonObject>()) {
    JsonDocument err;
    err["ok"] = false;
    err["error"] = "expected a JSON object of settings";
    sendJson(400, err);
    return;
  }
  const Settings before = settings;
  JsonDocument out;
  JsonObject errors = out["errors"].to<JsonObject>();
  for (JsonPair kv : in.as<JsonObject>()) {
    String value = kv.value().is<const char *>() ? String(kv.value().as<const char *>())
                 : kv.value().is<bool>() ? String(kv.value().as<bool>() ? "1" : "0")
                 : String(kv.value().as<long>());
    const String why = settingsSet(settings, kv.key().c_str(), value);
    if (why.length()) errors[kv.key().c_str()] = why;
  }
  const bool restart = settingsApply(before);
  out["ok"] = errors.size() == 0;
  out["restart"] = restart;
  addValues(out["values"].to<JsonObject>());
  sendJson(200, out);
}

static void handleAction() {
  JsonDocument in;
  deserializeJson(in, server->arg("plain"));
  const String what = in["do"] | "";
  JsonDocument out;
  if (what == "refresh" || what == "restart" || what == "wifi" || (what == "calibrate" && boardCanCalibrate()) ||
      (what == "ledtest" && ledSupported()) || what == "updcheck" || (what == "update" && otaState() == OTA_AVAILABLE)) {
    out["ok"] = true;
    sendJson(200, out);
    appRequest(what.c_str());   // acted on by the main loop after this reply has gone out
  } else {
    out["ok"] = false;
    out["error"] = "unknown action";
    sendJson(400, out);
  }
}

// ---------------------------------------------------------------- lifecycle

void webStart() {
  if (server) return;
  server = new WebServer(80);
  server->on("/", HTTP_GET, handlePage);
  server->on("/settings", HTTP_GET, handlePage);
  server->on("/api/settings", HTTP_GET, handleGet);
  server->on("/api/settings", HTTP_POST, handlePost);
  server->on("/api/action", HTTP_POST, handleAction);
  server->onNotFound([]() { server->sendHeader("Location", "/"); server->send(302, "text/plain", ""); });
  server->begin();
  Serial.println("[web] settings page at http://" HOSTNAME ".local");
}

void webStop() {
  if (!server) return;
  server->stop();
  delete server;
  server = nullptr;
}

void webLoop() {
  if (server) server->handleClient();
}
