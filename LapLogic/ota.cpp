#include "ota.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>

static volatile OtaState state = OTA_IDLE;
static volatile bool wantCheck = false, wantInstall = false;
static volatile int progress = 0;
static char newVersion[16] = "";
static char assetUrl[256] = "";
static String notes;
static char error[80] = "";
static time_t checkedAt = 0;
static SemaphoreHandle_t lock = nullptr;

// "v1.10.2" / "b1.10.2" -> one comparable number; at the same number an official release ("v")
// ranks above its beta ("b"), so a beta board moves on to the official version when it's out
static uint32_t versionRank(const char *v) {
  const bool official = *v == 'v' || *v == 'V';
  if (isalpha((unsigned char)*v)) v++;
  unsigned a = 0, b = 0, c = 0;
  sscanf(v, "%u.%u.%u", &a, &b, &c);
  return (a * 1000000UL + b * 1000UL + c) * 2 + (official ? 1 : 0);
}
static bool isBeta(const char *v) { return *v == 'b' || *v == 'B'; }

static void setError(const char *e) { strlcpy(error, e, sizeof(error)); state = OTA_FAILED; Serial.printf("[ota] %s\n", e); }

static void check() {
  state = OTA_CHECKING;
  WiFiClientSecure client;
  client.setInsecure();   // a bad image is still rejected: the update is verified before it's used
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);
  http.setUserAgent(String(APP_NAME "/" APP_VERSION " (ESP32)"));
  // Official firmware asks for the latest official release. Beta firmware looks at the most recent
  // few releases, betas included (GitHub's "latest" skips pre-releases).
  const String url = isBeta(APP_VERSION) ? "https://api.github.com/repos/" UPDATE_REPO "/releases?per_page=5"
                                         : "https://api.github.com/repos/" UPDATE_REPO "/releases/latest";
  if (!http.begin(client, url)) { setError("couldn't reach GitHub"); return; }
  http.addHeader("Accept", "application/vnd.github+json");
  const int code = http.GET();
  if (code != 200) {
    http.end();
    if (code == 404) { state = OTA_IDLE; strlcpy(error, "no releases published yet", sizeof(error)); Serial.println("[ota] no releases yet"); return; }
    char e[48]; snprintf(e, sizeof(e), "GitHub answered %d", code); setError(e); return;
  }
  JsonDocument f1;
  f1["tag_name"] = true;
  f1["body"] = true;
  f1["draft"] = true;
  f1["assets"][0]["name"] = true;
  f1["assets"][0]["browser_download_url"] = true;
  JsonDocument filter;
  if (isBeta(APP_VERSION)) filter[0] = f1; else filter = f1;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) { setError("couldn't read GitHub's reply"); return; }
  checkedAt = time(nullptr);

  // the newest release (by version number) that isn't a draft
  JsonObjectConst best;
  if (doc.is<JsonArrayConst>()) {
    for (JsonObjectConst r : doc.as<JsonArrayConst>())
      if (!(r["draft"] | false) && (best.isNull() || versionRank(r["tag_name"] | "") > versionRank(best["tag_name"] | ""))) best = r;
  } else best = doc.as<JsonObjectConst>();
  if (best.isNull()) { state = OTA_IDLE; strlcpy(error, "no releases published yet", sizeof(error)); return; }

  const char *tag = best["tag_name"] | "";
  const bool newer = versionRank(tag) > versionRank(APP_VERSION);
  Serial.printf("[ota] latest release %s, this is %s%s\n", tag, APP_VERSION, newer ? " - update available" : "");
  error[0] = 0;
  if (!newer) { state = OTA_IDLE; return; }

  // the app-only image for this board, e.g. LapLogic-CYD-ESP32-2432S028R-ota.bin
  for (JsonObjectConst a : best["assets"].as<JsonArrayConst>()) {
    const char *name = a["name"] | "";
    if (!strcmp(name, UPDATE_ASSET)) {
      xSemaphoreTake(lock, portMAX_DELAY);
      strlcpy(newVersion, tag, sizeof(newVersion));
      strlcpy(assetUrl, a["browser_download_url"] | "", sizeof(assetUrl));
      notes = (const char *)(best["body"] | "");
      if (notes.length() > 400) notes = notes.substring(0, 397) + "...";
      xSemaphoreGive(lock);
      state = OTA_AVAILABLE;
      return;
    }
  }
  setError("the new release has no firmware for this board");
}

static void install() {
  state = OTA_DOWNLOADING;
  progress = 0;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // GitHub hands the file over from another address
  http.setUserAgent(String(APP_NAME "/" APP_VERSION " (ESP32)"));
  if (!http.begin(client, assetUrl)) { setError("couldn't start the download"); return; }
  const int code = http.GET();
  if (code != 200) { char e[48]; snprintf(e, sizeof(e), "download failed (%d)", code); http.end(); setError(e); return; }
  const int len = http.getSize();
  if (len <= 0) { http.end(); setError("download has no size"); return; }
  if (!Update.begin(len, U_FLASH)) { http.end(); setError("not enough room for the update - reinstall over USB once"); return; }

  WiFiClient *s = http.getStreamPtr();
  uint8_t buf[2048];
  int got = 0;
  uint32_t lastData = millis();
  while (got < len && http.connected()) {
    const int avail = s->available();
    if (avail > 0) {
      const int n = s->readBytes(buf, min((int)sizeof(buf), avail));
      if (n > 0 && Update.write(buf, n) != (size_t)n) break;
      got += n;
      progress = (int)((int64_t)got * 100 / len);
      lastData = millis();
    } else {
      if (millis() - lastData > 20000) break;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  http.end();
  if (got != len) { Update.abort(); setError("download was cut short - still on the old version"); return; }
  if (!Update.end(true)) { setError("the downloaded firmware didn't check out - still on the old version"); return; }
  Serial.printf("[ota] installed %s, restarting\n", newVersion);
  state = OTA_DONE;
  progress = 100;
  vTaskDelay(pdMS_TO_TICKS(2500));   // let the screen say so
  ESP.restart();
}

void otaLoop() {
  if (!lock) lock = xSemaphoreCreateMutex();
  if (WiFi.status() != WL_CONNECTED) return;
  static uint32_t nextCheck = 60000;   // first look a minute after starting
  if (wantInstall) {
    wantInstall = false;
    if (state == OTA_AVAILABLE || (state == OTA_FAILED && assetUrl[0])) install();
    return;
  }
  const bool due = settings.updCheck && (int32_t)(millis() - nextCheck) >= 0;
  if (wantCheck || (due && state != OTA_AVAILABLE)) {
    wantCheck = false;
    nextCheck = millis() + UPDATE_CHECK_MS;
    check();
  }
}

void otaCheckNow() { wantCheck = true; }
void otaInstall() { wantInstall = true; }
OtaState otaState() { return state; }
const char *otaVersion() { return newVersion; }
int otaProgress() { return progress; }

String otaNotes() {
  if (!lock) return "";
  xSemaphoreTake(lock, portMAX_DELAY);
  String n = notes;
  xSemaphoreGive(lock);
  return n;
}

String otaStatus() {
  String s = APP_VERSION;
  if (isBeta(APP_VERSION)) s += " (beta)";
  switch (state) {
    case OTA_CHECKING:    return s + ", checking for updates...";
    case OTA_AVAILABLE:   return s + ", " + newVersion + " available";
    case OTA_DOWNLOADING: return s + ", installing " + newVersion + " (" + String(progress) + "%)";
    case OTA_DONE:        return s + ", " + newVersion + " installed - restarting";
    case OTA_FAILED:      return s + ", update problem: " + error;
    default: break;
  }
  if (error[0]) return s + " (" + error + ")";
  if (checkedAt) return s + ", up to date";
  return s + (settings.updCheck ? "" : ", update checks off");
}
