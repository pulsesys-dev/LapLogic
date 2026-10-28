#include "f1data.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// ------------------------------------------------------------------ helpers

static const char LATIN1[]  = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
static const char LATIN_A[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

// The built-in fonts are ASCII only, so "São Paulo" becomes "Sao Paulo".
void foldAscii(const char *in, char *out, size_t n) {
  size_t o = 0;
  const uint8_t *p = (const uint8_t *)(in ? in : "");
  while (*p && o + 1 < n) {
    uint32_t cp; int len;
    if (*p < 0x80)                { cp = *p; len = 1; }
    else if ((*p & 0xE0) == 0xC0) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); len = 2; }
    else if ((*p & 0xF0) == 0xE0) { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); len = 3; }
    else                          { cp = '?'; len = 4; }
    for (int i = 1; i < len; i++) if (!p[i]) { len = i; break; }   // truncated input guard
    char c;
    if (cp < 0x80)                     c = (char)cp;
    else if (cp >= 0xC0 && cp < 0x100)  c = LATIN1[cp - 0xC0];
    else if (cp >= 0x100 && cp < 0x180) c = LATIN_A[cp - 0x100];
    else if (cp == 0x2019 || cp == 0x2018) c = '\'';
    else if (cp == 0x2013 || cp == 0x2014) c = '-';
    else c = 0;
    if (c) out[o++] = c;
    p += len;
  }
  out[o] = 0;
}

static void copyStr(char *dst, size_t n, const char *src) { foldAscii(src, dst, n); }

// days since 1970-01-01 for a civil date (Howard Hinnant's algorithm)
static long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

// "2026-10-25" + "20:00:00Z" -> UTC epoch. Missing time = midday UTC.
static time_t parseUtc(const char *date, const char *tm) {
  if (!date) return 0;
  int y = 0, mo = 0, d = 0, h = 12, mi = 0, s = 0;
  if (sscanf(date, "%d-%d-%d", &y, &mo, &d) != 3) return 0;
  if (tm && *tm) sscanf(tm, "%d:%d:%d", &h, &mi, &s);
  return (time_t)(daysFromCivil(y, mo, d) * 86400L + h * 3600L + mi * 60L + s);
}

const char *sessionName(SessionKind k) {
  switch (k) {
    case S_FP1: return "Practice 1";
    case S_FP2: return "Practice 2";
    case S_FP3: return "Practice 3";
    case S_SQ: return "Sprint Qualifying";
    case S_SPRINT: return "Sprint";
    case S_QUALI: return "Qualifying";
    default: return "Race";
  }
}

const char *sessionShort(SessionKind k) {
  switch (k) {
    case S_FP1: return "FP1";
    case S_FP2: return "FP2";
    case S_FP3: return "FP3";
    case S_SQ: return "SQ";
    case S_SPRINT: return "SPR";
    case S_QUALI: return "Q";
    default: return "RACE";
  }
}

static uint16_t sessionMins(SessionKind k) {
  switch (k) {
    case S_SQ: return 45;
    case S_RACE: return 120;
    default: return 60;
  }
}

time_t raceEnd(const Race &r) {
  if (!r.ns) return 0;
  const Session &last = r.s[r.ns - 1];
  return last.start + last.mins * 60;
}

int nextRaceIndex(const F1Data &d, time_t now) {
  for (int i = 0; i < d.nRaces; i++)
    if (raceEnd(d.races[i]) > now) return i;
  return -1;
}

// ------------------------------------------------------------------ HTTP

static bool getJson(const String &path, JsonDocument &doc, JsonDocument &filter) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();   // public, read-only data; skips certificate pinning
  HTTPClient http;
  http.useHTTP10(true);   // no chunked encoding, so we can stream-parse
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent(String(APP_NAME "/" APP_VERSION " (ESP32)"));

  const String url = String(API_BASE) + path;
  if (!http.begin(client, url)) return false;
  const int code = http.GET();
  if (code != 200) {
    char tls[96] = "";
    if (code < 0) client.lastError(tls, sizeof(tls));   // e.g. out of memory during the secure handshake
    Serial.printf("[f1] %s -> HTTP %d %s%s (free %u, largest %u)\n", url.c_str(), code, http.errorToString(code).c_str(),
                  *tls ? (String(" / ") + tls).c_str() : "", (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    http.end();
    return false;
  }
  DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("[f1] %s -> JSON %s\n", url.c_str(), err.c_str());
    return false;
  }
  return true;
}

// ------------------------------------------------------------------ calendar

static void addSession(Race &r, SessionKind k, JsonVariantConst v) {
  if (v.isNull() || r.ns >= MAX_SESS + 1) return;
  time_t t = parseUtc(v["date"] | (const char *)nullptr, v["time"] | (const char *)nullptr);
  if (!t) return;
  Session &s = r.s[r.ns++];
  s.kind = k;
  s.start = t;
  s.mins = sessionMins(k);
}

bool fetchCalendar(F1Data &d, const char *season) {
  JsonDocument filter;
  filter["MRData"]["RaceTable"]["season"] = true;
  JsonObject f = filter["MRData"]["RaceTable"]["Races"][0].to<JsonObject>();
  f["round"] = true;
  f["raceName"] = true;
  f["date"] = true;
  f["time"] = true;
  f["Circuit"]["circuitName"] = true;
  f["Circuit"]["Location"]["locality"] = true;
  f["Circuit"]["Location"]["country"] = true;
  f["Circuit"]["Location"]["lat"] = true;
  f["Circuit"]["Location"]["long"] = true;
  const char *keys[] = {"FirstPractice", "SecondPractice", "ThirdPractice", "Qualifying",
                        "Sprint", "SprintQualifying", "SprintShootout"};
  for (const char *k : keys) { f[k]["date"] = true; f[k]["time"] = true; }

  JsonDocument doc;
  if (!getJson(String(season) + ".json?limit=100", doc, filter)) return false;

  JsonObjectConst table = doc["MRData"]["RaceTable"];
  JsonArrayConst races = table["Races"];
  if (races.isNull() || races.size() == 0) return false;   // leave existing data untouched

  d.season = atoi(table["season"] | "0");
  d.nRaces = 0;
  for (JsonObjectConst r : races) {
    if (d.nRaces >= MAX_RACES) break;
    Race &x = d.races[d.nRaces];
    memset(&x, 0, sizeof(x));
    x.round = atoi(r["round"] | "0");
    copyStr(x.name, sizeof(x.name), r["raceName"] | "");
    copyStr(x.circuit, sizeof(x.circuit), r["Circuit"]["circuitName"] | "");
    String place = String((const char *)(r["Circuit"]["Location"]["locality"] | "")) + ", " +
                   String((const char *)(r["Circuit"]["Location"]["country"] | ""));
    copyStr(x.place, sizeof(x.place), place.c_str());
    copyStr(x.locality, sizeof(x.locality), r["Circuit"]["Location"]["locality"] | "");
    copyStr(x.country, sizeof(x.country), r["Circuit"]["Location"]["country"] | "");
    x.lat = atof(r["Circuit"]["Location"]["lat"] | "0");
    x.lon = atof(r["Circuit"]["Location"]["long"] | "0");

    addSession(x, S_FP1, r["FirstPractice"]);
    addSession(x, S_FP2, r["SecondPractice"]);
    addSession(x, S_FP3, r["ThirdPractice"]);
    addSession(x, S_SQ, r["SprintQualifying"]);
    addSession(x, S_SQ, r["SprintShootout"]);
    addSession(x, S_SPRINT, r["Sprint"]);
    addSession(x, S_QUALI, r["Qualifying"]);
    x.sprint = !r["Sprint"].isNull();

    // the race itself
    time_t rt = parseUtc(r["date"] | (const char *)nullptr, r["time"] | (const char *)nullptr);
    if (rt && x.ns < MAX_SESS + 1) x.s[x.ns++] = {S_RACE, rt, sessionMins(S_RACE)};

    // sort sessions by start time (tiny insertion sort)
    for (int i = 1; i < x.ns; i++) {
      Session k = x.s[i]; int j = i - 1;
      while (j >= 0 && x.s[j].start > k.start) { x.s[j + 1] = x.s[j]; j--; }
      x.s[j + 1] = k;
    }
    d.nRaces++;
  }
  d.calendarAt = time(nullptr);
  Serial.printf("[f1] calendar %u: %u races\n", d.season, d.nRaces);
  return d.nRaces > 0;
}

// ------------------------------------------------------------------ standings

bool fetchDriverStandings(F1Data &d) {
  JsonDocument filter;
  JsonObject sl = filter["MRData"]["StandingsTable"]["StandingsLists"][0].to<JsonObject>();
  sl["round"] = true;
  JsonObject ds = sl["DriverStandings"][0].to<JsonObject>();
  ds["position"] = true;
  ds["positionText"] = true;
  ds["points"] = true;
  ds["wins"] = true;
  ds["Driver"]["code"] = true;
  ds["Driver"]["familyName"] = true;
  ds["Constructors"][0]["constructorId"] = true;

  JsonDocument doc;
  if (!getJson("current/driverStandings.json?limit=100", doc, filter)) return false;
  JsonObjectConst list = doc["MRData"]["StandingsTable"]["StandingsLists"][0];
  if (list.isNull()) return false;

  d.standingsRound = atoi(list["round"] | "0");
  d.nDrivers = 0;
  uint8_t fallbackPos = 0;
  for (JsonObjectConst r : list["DriverStandings"].as<JsonArrayConst>()) {
    if (d.nDrivers >= MAX_ROWS) break;
    DriverRow &x = d.drivers[d.nDrivers++];
    fallbackPos++;
    x.pos = atoi(r["position"] | "0");
    if (!x.pos) x.pos = fallbackPos;      // unclassified drivers have no "position"
    x.pts = atof(r["points"] | "0");
    x.wins = atoi(r["wins"] | "0");
    const char *code = r["Driver"]["code"] | "";
    const char *last = r["Driver"]["familyName"] | "";
    copyStr(x.last, sizeof(x.last), last);
    if (*code) copyStr(x.code, sizeof(x.code), code);
    else {  // build a code from the surname if the API has none
      char tmp[24]; copyStr(tmp, sizeof(tmp), last);
      for (int i = 0; i < 3; i++) x.code[i] = toupper(tmp[i] ? tmp[i] : '-');
      x.code[3] = 0;
    }
    copyStr(x.teamId, sizeof(x.teamId), r["Constructors"][0]["constructorId"] | "");
  }
  d.standingsAt = time(nullptr);
  return d.nDrivers > 0;
}

bool fetchTeamStandings(F1Data &d) {
  JsonDocument filter;
  JsonObject cs = filter["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"][0].to<JsonObject>();
  cs["position"] = true;
  cs["points"] = true;
  cs["wins"] = true;
  cs["Constructor"]["constructorId"] = true;
  cs["Constructor"]["name"] = true;

  JsonDocument doc;
  if (!getJson("current/constructorStandings.json?limit=100", doc, filter)) return false;
  JsonArrayConst arr = doc["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"];
  if (arr.isNull()) return false;

  d.nTeams = 0;
  for (JsonObjectConst r : arr) {
    if (d.nTeams >= MAX_TEAMS) break;
    TeamRow &x = d.teams[d.nTeams++];
    x.pos = atoi(r["position"] | "0");
    if (!x.pos) x.pos = d.nTeams;
    x.pts = atof(r["points"] | "0");
    x.wins = atoi(r["wins"] | "0");
    copyStr(x.id, sizeof(x.id), r["Constructor"]["constructorId"] | "");
    copyStr(x.name, sizeof(x.name), r["Constructor"]["name"] | "");
  }
  return d.nTeams > 0;
}

// ------------------------------------------------------------------ results

bool fetchLastResults(F1Data &d) {
  JsonDocument filter;
  JsonObject race = filter["MRData"]["RaceTable"]["Races"][0].to<JsonObject>();
  race["round"] = true;
  race["raceName"] = true;
  JsonObject res = race["Results"][0].to<JsonObject>();
  res["position"] = true;
  res["points"] = true;
  res["status"] = true;
  res["Time"]["time"] = true;
  res["Driver"]["code"] = true;
  res["Driver"]["familyName"] = true;
  res["Constructor"]["constructorId"] = true;

  JsonDocument doc;
  if (!getJson("current/last/results.json?limit=100", doc, filter)) return false;
  JsonObjectConst r0 = doc["MRData"]["RaceTable"]["Races"][0];
  if (r0.isNull()) {               // no race run yet this season
    d.nResults = 0;
    d.lastRound = 0;
    d.lastRaceName[0] = 0;
    d.resultsAt = time(nullptr);
    return true;
  }

  d.lastRound = atoi(r0["round"] | "0");
  copyStr(d.lastRaceName, sizeof(d.lastRaceName), r0["raceName"] | "");
  d.nResults = 0;
  for (JsonObjectConst r : r0["Results"].as<JsonArrayConst>()) {
    if (d.nResults >= MAX_ROWS) break;
    ResultRow &x = d.results[d.nResults++];
    x.pos = atoi(r["position"] | "0");
    x.pts = atof(r["points"] | "0");
    const char *code = r["Driver"]["code"] | "";
    if (*code) copyStr(x.code, sizeof(x.code), code);
    else {
      char tmp[24]; copyStr(tmp, sizeof(tmp), r["Driver"]["familyName"] | "---");
      for (int i = 0; i < 3; i++) x.code[i] = toupper(tmp[i] ? tmp[i] : '-');
      x.code[3] = 0;
    }
    copyStr(x.teamId, sizeof(x.teamId), r["Constructor"]["constructorId"] | "");
    const char *t = r["Time"]["time"] | (const char *)nullptr;
    copyStr(x.info, sizeof(x.info), t ? t : (r["status"] | ""));
  }
  d.resultsAt = time(nullptr);
  return true;
}
