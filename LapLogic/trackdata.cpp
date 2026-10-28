#include "trackdata.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#if BOARD_JC4827
  #include <PubSubClient.h>
#endif

TrackData        *track = nullptr;
SemaphoreHandle_t trackLock = nullptr;

static const char *OF_BASE = "https://api.openf1.org/v1/";
static bool fsOk = false;
static char liveStatus[48] = "";

// ================================================================== helpers

static long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

static void civilFromDays(long z, int &y, unsigned &m, unsigned &d) {
  z += 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += (m <= 2);
}

// "2024-07-21T14:03:12.345000+00:00" -> ms since 1970 (UTC). 0 if unparseable.
static int64_t isoMs(const char *s) {
  if (!s || strlen(s) < 19) return 0;
  int y, mo, d, h, mi, se;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
  int64_t ms = ((int64_t)daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se) * 1000;
  const char *p = s + 19;
  if (*p == '.') {
    p++;
    int frac = 0, digits = 0;
    while (isdigit((unsigned char)*p)) { if (digits < 3) { frac = frac * 10 + (*p - '0'); digits++; } p++; }
    while (digits < 3) { frac *= 10; digits++; }
    ms += frac;
  }
  if (*p == '+' || *p == '-') {
    int oh = 0, om = 0;
    sscanf(p + 1, "%d:%d", &oh, &om);
    const int64_t off = (int64_t)(oh * 3600 + om * 60) * 1000;
    ms += (*p == '+') ? -off : off;
  }
  return ms;
}

// ms since 1970 -> "2024-07-21T14:03:12.345" (UTC), the form OpenF1 accepts in date filters
static String isoFromMs(int64_t ms) {
  int64_t secs = ms / 1000;
  long days = (long)(secs / 86400);
  long rem = (long)(secs % 86400);
  int y; unsigned m, d;
  civilFromDays(days, y, m, d);
  char b[32];
  snprintf(b, sizeof(b), "%04d-%02u-%02uT%02ld:%02ld:%02ld.%03d", y, m, d, rem / 3600, (rem % 3600) / 60, rem % 60, (int)(ms % 1000));
  return b;
}

static int64_t nowMs() { return (int64_t)time(nullptr) * 1000; }

// lower-case ASCII copy, accents folded ("São Paulo" -> "sao paulo")
static void fold(const char *in, char *out, size_t n) {
  foldAscii(in, out, n);
  for (char *p = out; *p; p++) *p = tolower(*p);
}

static bool sameName(const char *a, const char *b) {
  char x[40], y[40];
  fold(a, x, sizeof(x)); fold(b, y, sizeof(y));
  if (!*x || !*y) return false;
  const size_t n = min((size_t)5, min(strlen(x), strlen(y)));
  return strncmp(x, y, n) == 0 || strstr(x, y) || strstr(y, x);
}

static bool sameCountry(const char *a, const char *b) {
  static const char *alias[][2] = {{"usa", "united states"}, {"uk", "united kingdom"},
                                   {"uae", "united arab emirates"}, {"korea", "south korea"}};
  char x[32], y[32];
  fold(a, x, sizeof(x)); fold(b, y, sizeof(y));
  if (!strcmp(x, y)) return true;
  for (auto &p : alias)
    if ((!strcmp(x, p[0]) && !strcmp(y, p[1])) || (!strcmp(x, p[1]) && !strcmp(y, p[0]))) return true;
  return false;
}

static void setText(char *dst, size_t n, const char *src) { strncpy(dst, src ? src : "", n - 1); dst[n - 1] = 0; }

// ================================================================== OpenF1 login (live only)

static String token;
static uint32_t tokenExpiresAt = 0;
static volatile bool loginChanged = false;   // settings changed the OpenF1 login
static volatile bool forceRun = false;       // re-decide what the map shows on the next update
static SemaphoreHandle_t tokenLock = nullptr;

static String urlEncode(const String &s) {
  String o;
  const char *hex = "0123456789ABCDEF";
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
  }
  return o;
}

// Returns a valid access token, logging in if needed. Empty if no login is set or it failed.
static String getToken() {
  if (!trackLiveSupported() || settings.ofUser.isEmpty() || settings.ofPass.isEmpty()) return "";
  xSemaphoreTake(tokenLock, portMAX_DELAY);
  if (token.length() && (int32_t)(tokenExpiresAt - millis()) > 120000) {
    String t = token;
    xSemaphoreGive(tokenLock);
    return t;
  }
  token = "";
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (http.begin(client, "https://api.openf1.org/token")) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    const int code = http.POST("username=" + urlEncode(settings.ofUser) + "&password=" + urlEncode(settings.ofPass));
    if (code == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getString())) {
        token = (const char *)(doc["access_token"] | "");
        long exp = doc["expires_in"].is<const char *>() ? atol(doc["expires_in"] | "3600") : (long)(doc["expires_in"] | 3600);
        tokenExpiresAt = millis() + (uint32_t)exp * 1000UL;
      }
      setText(liveStatus, sizeof(liveStatus), token.length() ? "logged in" : "login reply not understood");
    } else {
      setText(liveStatus, sizeof(liveStatus), code == 401 || code == 403 ? "login refused - check email/password" : "login failed");
      Serial.printf("[map] OpenF1 login -> HTTP %d\n", code);
    }
    http.end();
  }
  String t = token;
  xSemaphoreGive(tokenLock);
  return t;
}

// ================================================================== CSV over HTTPS

typedef bool (*RowFn)(void *ctx, const char *const *f);   // return false to stop early

static uint32_t lastRequest = 0;

// Split one CSV line in place (handles "quoted, fields")
static int splitCsv(char *line, char **out, int max) {
  int n = 0;
  char *p = line;
  while (n < max) {
    if (*p == '"') {
      char *start = ++p, *w = p;
      while (*p && !(*p == '"' && p[1] != '"')) { if (*p == '"') p++; *w++ = *p++; }
      if (*p == '"') p++;
      out[n++] = start;
      if (*p == ',') { *w = 0; p++; } else { *w = 0; break; }
    } else {
      out[n++] = p;
      char *c = strchr(p, ',');
      if (!c) break;
      *c = 0; p = c + 1;
    }
  }
  return n;
}

// GET an OpenF1 endpoint as CSV and hand the wanted columns of each row to fn.
// Returns rows read, or -1 on failure.
static int csvGet(const String &path, const char *const *cols, int nCols, RowFn fn, void *ctx) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  const uint32_t since = millis() - lastRequest;
  if (since < 700) delay(700 - since);           // stay well inside the free rate limit
  lastRequest = millis();

  const String tok = getToken();
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(20000);
  http.setUserAgent(String(APP_NAME "/" APP_VERSION " (ESP32)"));
  const String url = String(OF_BASE) + path + (path.indexOf('?') >= 0 ? "&csv=true" : "?csv=true");
  if (!http.begin(client, url)) return -1;
  if (tok.length()) http.addHeader("Authorization", "Bearer " + tok);
  const int code = http.GET();
  if (code != 200) {
    char tls[96] = "";
    if (code < 0) client.lastError(tls, sizeof(tls));
    Serial.printf("[map] %s -> HTTP %d %s %s\n", path.c_str(), code, code < 0 ? http.errorToString(code).c_str() : "", tls);
    http.end();
    return -1;
  }

  WiFiClient *s = http.getStreamPtr();
  int remaining = http.getSize();                 // -1 if unknown
  static char line[640];                          // only ever used from one task at a time
  size_t len = 0;
  bool header = true, stop = false, overflow = false;
  int idx[12];
  for (int i = 0; i < nCols; i++) idx[i] = -1;
  int rows = 0;
  uint32_t lastData = millis();
  uint8_t chunk[512];

  auto handleLine = [&]() {
    line[len] = 0;
    if (len && line[len - 1] == '\r') line[--len] = 0;
    char *f[40];
    const int n = splitCsv(line, f, 40);
    if (header) {
      for (int c = 0; c < nCols; c++)
        for (int i = 0; i < n; i++) if (!strcmp(f[i], cols[c])) idx[c] = i;
      header = false;
    } else if (len && !overflow) {
      const char *v[12];
      for (int c = 0; c < nCols; c++) v[c] = (idx[c] >= 0 && idx[c] < n) ? f[idx[c]] : "";
      rows++;
      if (!fn(ctx, v)) stop = true;
    }
    len = 0;
    overflow = false;
  };

  while (!stop && (remaining != 0)) {
    const int avail = s->available();
    if (avail <= 0) {
      if (!s->connected()) break;
      if (millis() - lastData > 15000) break;
      delay(2);
      continue;
    }
    const int got = s->read(chunk, min(avail, (int)sizeof(chunk)));
    if (got <= 0) continue;
    lastData = millis();
    if (remaining > 0) remaining -= got;
    for (int i = 0; i < got && !stop; i++) {
      const char c = chunk[i];
      if (c == '\n') handleLine();
      else if (len < sizeof(line) - 1) line[len++] = c;
      else overflow = true;
    }
  }
  if (len && !stop) handleLine();
  http.end();
  return rows;
}

// ================================================================== meetings / sessions / drivers

struct Meeting { int32_t key, circuit; int64_t start; char loc[28], country[24], name[24]; };
struct Sess { int32_t key; char name[24]; char type[12]; int64_t start, end; };

static Meeting  *meet = nullptr;          // [2][32]: this year, last year - only while trackUpdate runs
static int       nMeet[2] = {0, 0};
static int       meetYear[2] = {0, 0};
static uint32_t  meetAt[2] = {0, 0};

static bool loadMeetings(int slot, int year) {
  if (!meet) return false;
  if (meetYear[slot] == year && nMeet[slot]) return true;
  struct Ctx { Meeting *m; int n; } ctx = {meet + slot * 32, 0};
  const char *cols[] = {"meeting_key", "circuit_key", "circuit_short_name", "location", "country_name", "date_start"};
  const int r = csvGet("meetings?year=" + String(year), cols, 6, [](void *c, const char *const *f) {
    Ctx &x = *(Ctx *)c;
    if (x.n >= 32) return false;
    Meeting &m = x.m[x.n++];
    m.key = atol(f[0]); m.circuit = atol(f[1]);
    foldAscii(f[2], m.name, sizeof(m.name));
    foldAscii(f[3], m.loc, sizeof(m.loc));
    foldAscii(f[4], m.country, sizeof(m.country));
    m.start = isoMs(f[5]);
    return true;
  }, &ctx);
  if (r < 0) return false;
  nMeet[slot] = ctx.n; meetYear[slot] = year; meetAt[slot] = millis();
  return true;
}

// Find the OpenF1 meeting for a race: by circuit, then by date, then by place name
static const Meeting *findMeeting(int slot, const Race *r, int32_t circuit) {
  Meeting *m = meet + slot * 32;
  const int n = nMeet[slot];
  if (circuit) for (int i = 0; i < n; i++) if (m[i].circuit == circuit) return &m[i];
  if (!r) return nullptr;
  if (r->ns) {
    const int64_t fp1 = (int64_t)r->s[0].start * 1000;
    for (int i = 0; i < n; i++) if (llabs(m[i].start - fp1) < 4LL * 86400 * 1000) return &m[i];
  }
  for (int i = 0; i < n; i++) if (sameName(m[i].loc, r->locality) || sameName(m[i].name, r->locality)) return &m[i];
  const Meeting *only = nullptr; int count = 0;
  for (int i = 0; i < n; i++) if (sameCountry(m[i].country, r->country)) { only = &m[i]; count++; }
  return count == 1 ? only : nullptr;
}

static int loadSessions(int32_t meeting, Sess *out, int max) {
  struct Ctx { Sess *s; int n, max; } ctx = {out, 0, max};
  const char *cols[] = {"session_key", "session_name", "session_type", "date_start", "date_end"};
  const int r = csvGet("sessions?meeting_key=" + String(meeting), cols, 5, [](void *c, const char *const *f) {
    Ctx &x = *(Ctx *)c;
    if (x.n >= x.max) return false;
    Sess &s = x.s[x.n++];
    s.key = atol(f[0]);
    setText(s.name, sizeof(s.name), f[1]);
    setText(s.type, sizeof(s.type), f[2]);
    s.start = isoMs(f[3]); s.end = isoMs(f[4]);
    return true;
  }, &ctx);
  return r < 0 ? -1 : ctx.n;
}

static int loadDrivers(int32_t session, TrackCar *out, int max) {
  struct Ctx { TrackCar *c; int n, max; } ctx = {out, 0, max};
  const char *cols[] = {"driver_number", "name_acronym", "team_colour"};
  const int r = csvGet("drivers?session_key=" + String(session), cols, 3, [](void *c, const char *const *f) {
    Ctx &x = *(Ctx *)c;
    if (x.n >= x.max) return false;
    const int num = atoi(f[0]);
    for (int i = 0; i < x.n; i++) if (x.c[i].num == num) return true;   // one row per driver
    TrackCar &t = x.c[x.n++];
    t.num = num;
    foldAscii(f[1], t.code, sizeof(t.code));
    t.colour = (f[2] && *f[2]) ? strtoul(f[2], nullptr, 16) : 0x888890;
    return true;
  }, &ctx);
  return r < 0 ? -1 : ctx.n;
}

// ================================================================== storage (spare flash)

static String outlinePath(int32_t circuit) { return "/o_" + String(circuit) + ".bin"; }
static String replayPath(int32_t session)  { return "/r_" + String(session) + ".bin"; }
static const uint32_t MAGIC = 0x4C4C3031;   // "LL01"

struct OutlineFile { uint32_t magic; int32_t cx, cy, scale; uint16_t n; char circuit[32]; };
struct ReplayFile  { uint32_t magic; int32_t session; uint8_t nCars, nFrames; };
static const size_t FRAME_BYTES = sizeof(int16_t) * TRACK_MAX_CARS * 2;
static SemaphoreHandle_t fileLock = nullptr;   // replay file is written on core 0 and read by the UI
static File    rf;                             // the UI's open handle on the stored replay
static int32_t rfSession = 0;

static bool loadOutlineFile(int32_t circuit) {
  if (!fsOk) return false;
  File f = LittleFS.open(outlinePath(circuit), "r");
  if (!f) return false;
  OutlineFile h;
  bool ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic == MAGIC && h.n > 1 && h.n <= TRACK_MAX_PTS;
  if (ok) {
    static int16_t tmp[TRACK_MAX_PTS][2];
    ok = f.read((uint8_t *)tmp, h.n * 4) == (size_t)(h.n * 4);
    if (ok) {
      xSemaphoreTake(trackLock, portMAX_DELAY);
      track->cx = h.cx; track->cy = h.cy; track->scale = h.scale;
      track->nPts = h.n;
      memcpy(track->pts, tmp, h.n * 4);
      setText(track->circuit, sizeof(track->circuit), h.circuit);
      track->version++;
      xSemaphoreGive(trackLock);
    }
  }
  f.close();
  return ok;
}

static void saveOutlineFile(int32_t circuit) {
  if (!fsOk) return;
  File f = LittleFS.open(outlinePath(circuit), "w");
  if (!f) return;
  OutlineFile h = {MAGIC, track->cx, track->cy, track->scale, track->nPts, {0}};
  setText(h.circuit, sizeof(h.circuit), track->circuit);
  f.write((const uint8_t *)&h, sizeof(h));
  f.write((const uint8_t *)track->pts, track->nPts * 4);
  f.close();
}

static void removeOldReplays(int32_t keep) {
  if (!fsOk) return;
  if (rf) { rf.close(); rfSession = 0; }   // never delete a file that's open for reading
  File dir = LittleFS.open("/");
  String keepName = replayPath(keep).substring(1);
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    if (n.startsWith("/")) n = n.substring(1);
    f.close();
    if (n.startsWith("r_") && n != keepName) LittleFS.remove("/" + n);
  }
}

// ================================================================== circuit outline

static inline int16_t q16(int32_t v, int32_t c, int32_t s) { return (int16_t)constrain((v - c) / s, -32000, 32000); }

static bool fetchOutline(int32_t session, int64_t sessionStart, int32_t circuit, const char *circuitName) {
  if (loadOutlineFile(circuit)) return true;

  TrackCar cars[TRACK_MAX_CARS];
  const int nc = loadDrivers(session, cars, TRACK_MAX_CARS);
  if (nc <= 0) return false;

  struct Ctx { int32_t (*p)[2]; int n, max; } ctx;
  ctx.max = 700;
  ctx.p = (int32_t (*)[2])malloc(ctx.max * 8);
  if (!ctx.p) return false;
  const int64_t offsets[2] = {20LL * 60000, 45LL * 60000};
  bool ok = false;
  for (int attempt = 0; attempt < 4 && !ok; attempt++) {
    const TrackCar &car = cars[attempt % nc];
    const int64_t from = sessionStart + offsets[attempt / 2 % 2];
    ctx.n = 0;
    const char *cols[] = {"x", "y"};
    csvGet("location?session_key=" + String(session) + "&driver_number=" + String(car.num) +
           "&date>" + isoFromMs(from) + "&date<" + isoFromMs(from + 130000), cols, 2,
           [](void *c, const char *const *f) {
             Ctx &x = *(Ctx *)c;
             if (x.n >= x.max) return false;
             const int32_t px = atol(f[0]), py = atol(f[1]);
             if (px == 0 && py == 0) return true;                // no fix yet
             if (x.n && x.p[x.n - 1][0] == px && x.p[x.n - 1][1] == py) return true;   // parked
             x.p[x.n][0] = px; x.p[x.n][1] = py; x.n++;
             return true;
           }, &ctx);
    ok = ctx.n >= 60;   // a car parked in the garage gives too few distinct points
  }
  if (ok) {
    int32_t minx = INT32_MAX, maxx = INT32_MIN, miny = INT32_MAX, maxy = INT32_MIN;
    for (int i = 0; i < ctx.n; i++) {
      minx = min(minx, ctx.p[i][0]); maxx = max(maxx, ctx.p[i][0]);
      miny = min(miny, ctx.p[i][1]); maxy = max(maxy, ctx.p[i][1]);
    }
    const int32_t cx = (minx + maxx) / 2, cy = (miny + maxy) / 2;
    const int32_t scale = max((int32_t)1, (max(maxx - minx, maxy - miny) / 2) / 30000 + 1);
    const int stride = (ctx.n + TRACK_MAX_PTS - 1) / TRACK_MAX_PTS;
    xSemaphoreTake(trackLock, portMAX_DELAY);
    track->cx = cx; track->cy = cy; track->scale = scale;
    track->nPts = 0;
    for (int i = 0; i < ctx.n && track->nPts < TRACK_MAX_PTS; i += stride) {
      track->pts[track->nPts][0] = q16(ctx.p[i][0], cx, scale);
      track->pts[track->nPts][1] = q16(ctx.p[i][1], cy, scale);
      track->nPts++;
    }
    setText(track->circuit, sizeof(track->circuit), circuitName);
    track->version++;
    xSemaphoreGive(trackLock);
    saveOutlineFile(circuit);
    Serial.printf("[map] circuit %s: %u points\n", circuitName, track->nPts);
  }
  free(ctx.p);
  return ok;
}

// ================================================================== final-lap replay

static int32_t replayLoaded = 0;

// Read just the header and car list of a stored replay
static bool loadReplayFile(int32_t session) {
  if (!fsOk) return false;
  xSemaphoreTake(fileLock, portMAX_DELAY);
  File f = LittleFS.open(replayPath(session), "r");
  bool ok = false;
  if (f) {
    ReplayFile h;
    ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic == MAGIC && h.session == session &&
         h.nCars <= TRACK_MAX_CARS && h.nFrames > 1 && h.nFrames <= TRACK_REPLAY_FRAMES &&
         f.size() >= sizeof(h) + h.nCars * sizeof(TrackCar) + h.nFrames * FRAME_BYTES;
    TrackCar cars[TRACK_MAX_CARS];
    if (ok) ok = f.read((uint8_t *)cars, h.nCars * sizeof(TrackCar)) == h.nCars * sizeof(TrackCar);
    if (ok) {
      xSemaphoreTake(trackLock, portMAX_DELAY);
      memcpy(track->cars, cars, h.nCars * sizeof(TrackCar));
      track->nCars = h.nCars; track->nFrames = h.nFrames; track->replaySession = session;
      track->version++;
      xSemaphoreGive(trackLock);
    }
    f.close();
  }
  xSemaphoreGive(fileLock);
  return ok;
}

bool trackReadFrame(int frame, int16_t out[TRACK_MAX_CARS][2]) {
  if (!fsOk || !track || !track->replaySession || frame < 0 || frame >= track->nFrames) return false;
  xSemaphoreTake(fileLock, portMAX_DELAY);
  if (!rf || rfSession != track->replaySession) {
    if (rf) rf.close();
    rf = LittleFS.open(replayPath(track->replaySession), "r");
    rfSession = track->replaySession;
  }
  bool ok = false;
  if (rf) {
    const size_t at = sizeof(ReplayFile) + track->nCars * sizeof(TrackCar) + (size_t)frame * FRAME_BYTES;
    ok = rf.seek(at) && rf.read((uint8_t *)out, FRAME_BYTES) == FRAME_BYTES;
  }
  xSemaphoreGive(fileLock);
  return ok;
}

// Rows arrive in time order, so only the current second is held in memory while
// the replay is written straight to flash.
struct ReplayCtx {
  File f; TrackCar *cars; int nc, frames, cur, written; int64_t t0; int32_t cx, cy, scale;
  int16_t now[TRACK_MAX_CARS][2], last[TRACK_MAX_CARS][2];
  void flush() {                       // write second `cur`, filling gaps from the second before
    for (int i = 0; i < TRACK_MAX_CARS; i++)
      if (now[i][0] == TRACK_NO_POS) { now[i][0] = last[i][0]; now[i][1] = last[i][1]; }
    f.write((const uint8_t *)now, FRAME_BYTES);
    memcpy(last, now, FRAME_BYTES);
    for (int i = 0; i < TRACK_MAX_CARS; i++) now[i][0] = now[i][1] = TRACK_NO_POS;
    written++; cur++;
  }
};

static bool fetchReplay(const Sess &s) {
  if (replayLoaded == s.key) return true;
  if (loadReplayFile(s.key)) { replayLoaded = s.key; return true; }
  if (!fsOk) return false;   // the replay is streamed to flash; without storage there's nowhere to put it

  // 1. who won
  struct Lead { int num; int64_t at; } lead = {0, 0};
  const char *pc[] = {"driver_number", "date"};
  csvGet("position?session_key=" + String(s.key) + "&position=1", pc, 2, [](void *c, const char *const *f) {
    Lead &l = *(Lead *)c;
    const int64_t t = isoMs(f[1]);
    if (t >= l.at) { l.at = t; l.num = atoi(f[0]); }
    return true;
  }, &lead);
  if (!lead.num) return false;

  // 2. the start and length of their last lap
  struct Lap { int lap; int64_t start; float dur; } lap = {0, 0, 0};
  const char *lc[] = {"lap_number", "date_start", "lap_duration"};
  csvGet("laps?session_key=" + String(s.key) + "&driver_number=" + String(lead.num), lc, 3, [](void *c, const char *const *f) {
    Lap &l = *(Lap *)c;
    const int n = atoi(f[0]);
    const int64_t st = isoMs(f[1]);
    if (n > l.lap && st) { l.lap = n; l.start = st; l.dur = atof(f[2]); }
    return true;
  }, &lap);
  if (!lap.start) return false;
  const int frames = constrain((int)(lap.dur > 0 ? lap.dur : 100) + 2, 30, TRACK_REPLAY_FRAMES);

  // 3. cars
  TrackCar cars[TRACK_MAX_CARS];
  const int nc = loadDrivers(s.key, cars, TRACK_MAX_CARS);
  if (nc <= 0) return false;

  // 4. everyone's position over that lap, one sample per second
  ReplayCtx *ctx = new ReplayCtx();   // ~400 bytes, kept off the task stack
  if (!ctx) return false;
  xSemaphoreTake(fileLock, portMAX_DELAY);
  removeOldReplays(s.key);
  ctx->f = LittleFS.open(replayPath(s.key), "w");
  xSemaphoreGive(fileLock);
  if (!ctx->f) { delete ctx; return false; }
  ctx->cars = cars; ctx->nc = nc; ctx->frames = frames; ctx->cur = 0; ctx->written = 0; ctx->t0 = lap.start;
  for (int i = 0; i < TRACK_MAX_CARS; i++) ctx->now[i][0] = ctx->now[i][1] = ctx->last[i][0] = ctx->last[i][1] = TRACK_NO_POS;
  xSemaphoreTake(trackLock, portMAX_DELAY);
  ctx->cx = track->cx; ctx->cy = track->cy; ctx->scale = track->scale;
  xSemaphoreGive(trackLock);

  ReplayFile h = {MAGIC, s.key, (uint8_t)nc, 0};           // nFrames stays 0 until complete
  ctx->f.write((const uint8_t *)&h, sizeof(h));
  ctx->f.write((const uint8_t *)cars, nc * sizeof(TrackCar));

  const char *cols[] = {"date", "driver_number", "x", "y"};
  const int rows = csvGet("location?session_key=" + String(s.key) + "&date>" + isoFromMs(lap.start) +
                          "&date<" + isoFromMs(lap.start + (int64_t)frames * 1000), cols, 4,
                          [](void *c, const char *const *f) {
    ReplayCtx &x = *(ReplayCtx *)c;
    const int fi = (int)((isoMs(f[0]) - x.t0) / 1000);
    if (fi < x.cur || fi >= x.frames) return true;     // late or out of range
    while (x.cur < fi) x.flush();
    const int num = atoi(f[1]);
    for (int i = 0; i < x.nc; i++) if (x.cars[i].num == num) {
      if (x.now[i][0] == TRACK_NO_POS) {
        x.now[i][0] = q16(atol(f[2]), x.cx, x.scale);
        x.now[i][1] = q16(atol(f[3]), x.cy, x.scale);
      }
      break;
    }
    return true;
  }, ctx);
  const bool ok = rows > 0;
  xSemaphoreTake(fileLock, portMAX_DELAY);
  if (ok) {
    while (ctx->cur < frames) ctx->flush();
    h.nFrames = frames;
    ctx->f.seek(0);
    ctx->f.write((const uint8_t *)&h, sizeof(h));       // mark complete
    ctx->f.close();
  } else {
    ctx->f.close();
    LittleFS.remove(replayPath(s.key));
  }
  xSemaphoreGive(fileLock);
  if (ok) {
    xSemaphoreTake(trackLock, portMAX_DELAY);
    memcpy(track->cars, cars, sizeof(TrackCar) * nc);
    track->nCars = nc; track->nFrames = frames; track->replaySession = s.key;
    track->version++;
    xSemaphoreGive(trackLock);
    replayLoaded = s.key;
    Serial.printf("[map] replay of %s: %d cars, %d s, %d samples\n", s.name, nc, frames, rows);
  }
  delete ctx;
  return ok;
}

// ================================================================== what to show

static void setView(TrackMode mode, const char *caption, const char *note, const char *message) {
  xSemaphoreTake(trackLock, portMAX_DELAY);
  const bool changed = track->mode != mode || strcmp(track->caption, caption ? caption : "") ||
                       strcmp(track->note, note ? note : "") || strcmp(track->message, message ? message : "");
  track->mode = mode;
  setText(track->caption, sizeof(track->caption), caption);
  setText(track->note, sizeof(track->note), note);
  setText(track->message, sizeof(track->message), message);
  if (changed) track->version++;
  xSemaphoreGive(trackLock);
}

void trackUpdate(const F1Data &d) {
  static uint32_t lastRun = 0;
  static int lastTarget = -2;
  if (!track || WiFi.status() != WL_CONNECTED || !d.nRaces) return;

  const time_t now = time(nullptr);
  const int ri = nextRaceIndex(d, now);
  // During a race weekend (from 2 days before practice) show that circuit; otherwise the last race
  const Race *weekend = (ri >= 0 && d.races[ri].ns && now >= d.races[ri].s[0].start - 2 * 86400) ? &d.races[ri] : nullptr;
  const Race *last = ri > 0 ? &d.races[ri - 1] : (ri < 0 ? &d.races[d.nRaces - 1] : nullptr);
  const Race *target = weekend ? weekend : last;
  const int targetId = target ? target->round : -1;

  const bool liveish = track->liveSession != 0;
  // As fast as OpenF1's free data changes: each session's data appears shortly after it ends
  const uint32_t every = liveish ? 60000UL : weekend ? 2UL * 60000 : 15UL * 60000;
  if (!forceRun && targetId == lastTarget && lastRun && millis() - lastRun < every) return;
  forceRun = false;
  lastRun = millis();
  lastTarget = targetId;

  if (!target) { setView(TM_NONE, "", "", "The track map appears once the season starts."); return; }

  struct tm g; gmtime_r(&now, &g);
  const int year = g.tm_year + 1900;

  // The meetings list (~6 KB) only exists while this runs
  struct MeetGuard {
    MeetGuard()  { meet = (Meeting *)calloc(64, sizeof(Meeting)); nMeet[0] = nMeet[1] = 0; meetYear[0] = meetYear[1] = 0; }
    ~MeetGuard() { free(meet); meet = nullptr; }
  } guard;
  if (!meet) return;
  if (!loadMeetings(0, year)) { setView(track->mode, track->caption, track->note, track->nPts ? "" : "Couldn't reach OpenF1 - will retry."); return; }
  const Meeting *m = findMeeting(0, target, 0);

  static Sess sess[12];
  int ns = m ? loadSessions(m->key, sess, 12) : 0;
  const int64_t nowm = nowMs();
  const Sess *liveSess = nullptr, *lastEnded = nullptr, *lastRace = nullptr;
  for (int i = 0; i < ns; i++) {
    const Sess &s = sess[i];
    if (s.start && s.start <= nowm && nowm < s.end + 10LL * 60000) liveSess = &s;
    if (s.end && s.end + 15LL * 60000 < nowm) {      // free data appears a little after a session ends
      if (!lastEnded || s.end > lastEnded->end) lastEnded = &s;
      if (!strcmp(s.type, "Race") && (!lastRace || s.end > lastRace->end)) lastRace = &s;
    }
  }

  // Circuit outline: from this weekend if anything has run, otherwise last year's race here
  bool haveOutline = false;
  char circuitName[32];
  foldAscii(target->locality, circuitName, sizeof(circuitName));
  if (m) {
    if (lastEnded) haveOutline = fetchOutline(lastEnded->key, lastEnded->start, m->circuit, circuitName);
    else haveOutline = loadOutlineFile(m->circuit);
  }
  if (!haveOutline && loadMeetings(1, year - 1)) {
    const Meeting *pm = findMeeting(1, m ? nullptr : target, m ? m->circuit : 0);
    if (!pm && m) pm = findMeeting(1, target, 0);
    if (pm) {
      haveOutline = loadOutlineFile(pm->circuit);
      if (!haveOutline) {
        static Sess prev[12];
        const int np = loadSessions(pm->key, prev, 12);
        for (int i = np - 1; i >= 0 && !haveOutline; i--)
          if (!strcmp(prev[i].type, "Race")) haveOutline = fetchOutline(prev[i].key, prev[i].start, pm->circuit, circuitName);
      }
    }
  }

  char caption[64], note[96] = "";
  const bool canLive = trackLiveSupported();
  const bool hasLogin = settings.ofUser.length() && settings.ofPass.length();

  if (liveSess && canLive && hasLogin) {
    track->liveSession = liveSess->key;
    snprintf(caption, sizeof(caption), "Live - %s", liveSess->name);
    setView(TM_LIVE, caption, "", haveOutline ? "" : "Waiting for the circuit map...");
    return;
  }
  track->liveSession = 0;

  if (liveSess) {
    snprintf(note, sizeof(note), canLive ? "%s is on now. Add an OpenF1 login in settings for live car positions."
                                         : "%s is on now. Live car positions need the 4.3\" board.", liveSess->name);
  }

  if (lastRace && haveOutline && fetchReplay(*lastRace)) {
    snprintf(caption, sizeof(caption), "Final lap - %s%s", target->name, strstr(lastRace->name, "Sprint") ? " (Sprint)" : "");
    setView(TM_REPLAY, caption, note, "");
  } else if (haveOutline) {
    snprintf(caption, sizeof(caption), "%s", target->name);
    if (!*note) snprintf(note, sizeof(note), "Cars appear here after the race as a final-lap replay.");
    setView(TM_OUTLINE, caption, note, "");
  } else {
    setView(TM_NONE, target->name, note, "The circuit map appears once OpenF1 has data for this track.");
  }
}

// ================================================================== live positions (4.3" board)

#if BOARD_JC4827
bool trackLiveSupported() { return true; }

static WiFiClientSecure mqttNet;
static PubSubClient mqtt(mqttNet);
static int32_t liveFor = 0;   // session the car list was loaded for

static int carIndex(int num) {
  for (int i = 0; i < track->nCars; i++) if (track->cars[i].num == num) return i;
  return -1;
}

static void onMqtt(char *topic, byte *payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return;
  if ((int32_t)(doc["session_key"] | 0L) != track->liveSession) return;
  if (!strcmp(topic, "v1/race_control")) {
    String cat = doc["category"] | "", flag = doc["flag"] | "", scope = doc["scope"] | "", msg = doc["message"] | "";
    cat.toUpperCase(); flag.toUpperCase(); scope.toUpperCase(); msg.toUpperCase();
    const int sector = doc["sector"] | 0;
    xSemaphoreTake(trackLock, portMAX_DELAY);
    uint8_t f = track->rcFlag;
    if (cat == "SAFETYCAR") {
      const bool vsc = msg.indexOf("VIRTUAL") >= 0;
      if (msg.indexOf("ENDING") >= 0 || msg.indexOf("IN THIS LAP") >= 0) f = vsc ? RF_VSC_ENDING : RF_SC_ENDING;
      else if (msg.indexOf("DEPLOYED") >= 0) f = vsc ? RF_VSC : RF_SC;
    } else if (cat == "FLAG") {
      if (scope == "TRACK") {
        if (flag == "GREEN") { f = RF_GREEN; track->rcSectors = 0; track->rcDouble = false; }
        else if (flag == "RED") f = RF_RED;
        else if (flag == "CHEQUERED") { f = RF_CHEQUERED; track->rcSectors = 0; }
      } else if (scope == "SECTOR" && sector > 0 && sector < 32) {
        if (flag == "YELLOW" || flag == "DOUBLE YELLOW") {
          track->rcSectors |= 1UL << sector;
          if (flag == "DOUBLE YELLOW") track->rcDouble = true;
        } else if (flag == "CLEAR") {
          track->rcSectors &= ~(1UL << sector);
          if (!track->rcSectors) track->rcDouble = false;
        }
      }
    }
    track->rcFlag = f;
    track->rcAt = millis() | 1;
    xSemaphoreGive(trackLock);
    Serial.printf("[rc] %s\n", (const char *)(doc["message"] | ""));
    return;
  }
  if (!strcmp(topic, "v1/weather")) {
    if (!doc["track_temperature"].isNull()) {
      xSemaphoreTake(trackLock, portMAX_DELAY);
      track->trackTemp = doc["track_temperature"] | 0.0f;
      track->trackTempAt = millis() | 1;
      xSemaphoreGive(trackLock);
    }
    return;
  }
  const int num = doc["driver_number"] | 0;
  xSemaphoreTake(trackLock, portMAX_DELAY);
  const int c = carIndex(num);
  if (c >= 0) {
    if (!strcmp(topic, "v1/location")) {
      const int32_t x = doc["x"] | 0L, y = doc["y"] | 0L;
      if (x || y) {
        track->live[c][0] = q16(x, track->cx, track->scale);
        track->live[c][1] = q16(y, track->cy, track->scale);
        track->liveVersion++;
      }
    } else if (!strcmp(topic, "v1/position")) {
      track->livePos[c] = doc["position"] | 0;
      track->liveVersion++;
    }
  }
  xSemaphoreGive(trackLock);
}

static void liveTask(void *) {
  mqttNet.setInsecure();
  mqtt.setServer("mqtt.openf1.org", 8883);
  mqtt.setBufferSize(2048);
  mqtt.setKeepAlive(30);
  mqtt.setCallback(onMqtt);
  uint32_t lastTry = 0;
  for (;;) {
    if (loginChanged) {
      loginChanged = false;
      if (mqtt.connected()) mqtt.disconnect();
      xSemaphoreTake(tokenLock, portMAX_DELAY);
      token = ""; tokenExpiresAt = 0;
      xSemaphoreGive(tokenLock);
      liveStatus[0] = 0;
      lastTry = 0;
    }
    const bool hasLogin = settings.ofUser.length() && settings.ofPass.length();
    if (!hasLogin) setText(liveStatus, sizeof(liveStatus), "off - no OpenF1 login");
    if (!hasLogin || !track->liveSession || WiFi.status() != WL_CONNECTED) {
      if (mqtt.connected()) mqtt.disconnect();
      if (hasLogin && !track->liveSession) setText(liveStatus, sizeof(liveStatus), "ready - waiting for a session");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    if (liveFor != track->liveSession) {
      TrackCar cars[TRACK_MAX_CARS];
      const int n = loadDrivers(track->liveSession, cars, TRACK_MAX_CARS);
      if (n > 0) {
        xSemaphoreTake(trackLock, portMAX_DELAY);
        memcpy(track->cars, cars, sizeof(TrackCar) * n);
        track->nCars = n;
        for (int i = 0; i < TRACK_MAX_CARS; i++) { track->live[i][0] = track->live[i][1] = TRACK_NO_POS; track->livePos[i] = 0; }
        track->rcFlag = RF_NONE; track->rcSectors = 0; track->rcDouble = false; track->rcAt = 0;   // new session
        track->version++;
        xSemaphoreGive(trackLock);
        liveFor = track->liveSession;
      }
    }
    if (!mqtt.connected() && millis() - lastTry > 10000) {
      lastTry = millis();
      const String tok = getToken();
      if (tok.length()) {
        const String id = "laplogic-" + WiFi.macAddress();
        if (mqtt.connect(id.c_str(), settings.ofUser.c_str(), tok.c_str())) {
          mqtt.subscribe("v1/location");
          mqtt.subscribe("v1/position");
          mqtt.subscribe("v1/weather");
          mqtt.subscribe("v1/race_control");
          setText(liveStatus, sizeof(liveStatus), "live - receiving positions");
          Serial.println("[map] live stream connected");
        } else {
          setText(liveStatus, sizeof(liveStatus), "couldn't connect to the live stream");
          Serial.printf("[map] MQTT connect failed, state %d\n", mqtt.state());
        }
      }
    }
    // reconnect with a fresh token before the old one expires
    if (mqtt.connected() && (int32_t)(tokenExpiresAt - millis()) < 60000) mqtt.disconnect();
    mqtt.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
#else
bool trackLiveSupported() { return false; }
#endif

bool trackRaceControl(char *label, size_t n, uint32_t &colour) {
  if (!track || !trackLiveSupported()) return false;
  xSemaphoreTake(trackLock, portMAX_DELAY);
  const uint8_t f = track->rcFlag;
  const uint32_t at = track->rcAt, sectors = track->rcSectors;
  const bool dbl = track->rcDouble, live = track->liveSession != 0;
  xSemaphoreGive(trackLock);
  if (!live || !at) return false;
  const uint32_t age = millis() - at;
  switch (f) {
    case RF_RED:        snprintf(label, n, "RED FLAG");      colour = 0xE10600; return true;
    case RF_SC:         snprintf(label, n, "SAFETY CAR");    colour = 0xFFB800; return true;
    case RF_SC_ENDING:  snprintf(label, n, "SC IN THIS LAP"); colour = 0xFFB800; return true;
    case RF_VSC:        snprintf(label, n, "VIRTUAL SC");    colour = 0xFFB800; return true;
    case RF_VSC_ENDING: snprintf(label, n, "VSC ENDING");    colour = 0xFFB800; return true;
    case RF_CHEQUERED:  if (age > 5UL * 60000) return false; snprintf(label, n, "CHEQUERED FLAG"); colour = 0xF2F2F5; return true;
    default: break;
  }
  if (sectors) {
    int count = 0, first = 0;
    for (int s = 1; s < 32; s++) if (sectors & (1UL << s)) { if (!first) first = s; count++; }
    if (count == 1) snprintf(label, n, "%s S%d", dbl ? "DOUBLE YELLOW" : "YELLOW", first);
    else snprintf(label, n, "%s x%d", dbl ? "DOUBLE YELLOW" : "YELLOW", count);
    colour = 0xFFE000;
    return true;
  }
  if (f == RF_GREEN && age < 30000) { snprintf(label, n, "GREEN FLAG"); colour = 0x2BD47D; return true; }
  return false;
}

void trackLoginChanged() { loginChanged = true; forceRun = true; }

const char *trackLiveStatus() {
  if (!trackLiveSupported()) return "needs the 4.3\" board";
  return liveStatus[0] ? liveStatus : "off - no OpenF1 login";
}

// ================================================================== start

void trackBegin() {
  trackLock = xSemaphoreCreateMutex();
  tokenLock = xSemaphoreCreateMutex();
  track = (TrackData *)ps_calloc(1, sizeof(TrackData));
  if (!track) track = (TrackData *)calloc(1, sizeof(TrackData));
  fileLock = xSemaphoreCreateMutex();
  if (!track) { Serial.println("[map] out of memory"); return; }
  for (int i = 0; i < TRACK_MAX_CARS; i++) track->live[i][0] = track->live[i][1] = TRACK_NO_POS;
  track->scale = 1;
  setText(track->message, sizeof(track->message), "Loading the track map...");
  fsOk = LittleFS.begin(true);   // formats the spare flash on first use
  Serial.printf("[map] storage %s\n", fsOk ? "ready" : "unavailable");
#if BOARD_JC4827
  xTaskCreatePinnedToCore(liveTask, "live", 12288, nullptr, 1, nullptr, 0);
#endif
}
