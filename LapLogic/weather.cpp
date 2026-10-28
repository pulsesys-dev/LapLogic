// Weather at the circuit from Open-Meteo (https://open-meteo.com) - free, no account or key.
// Current conditions, plus the chance of rain for each session once the weekend is
// within the 16-day forecast.
#include "f1data.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char *weatherText(uint8_t c) {
  switch (c) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Storm, hail";
    default: return "";
  }
}

static void isoHour(time_t t, char *out, size_t n) {
  struct tm g; gmtime_r(&t, &g);
  strftime(out, n, "%Y-%m-%dT%H:00", &g);
}

bool fetchWeather(F1Data &d, int ri, bool imperial) {
  if (ri < 0 || ri >= d.nRaces || WiFi.status() != WL_CONNECTED) return false;
  const Race &r = d.races[ri];
  if (!r.lat && !r.lon) return false;

  char url[480];
  int n = snprintf(url, sizeof(url),
      "%s?latitude=%.3f&longitude=%.3f&timeformat=unixtime"
      "&current=temperature_2m,relative_humidity_2m,precipitation,weather_code,wind_speed_10m%s",
      WEATHER_API, r.lat, r.lon, imperial ? "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch" : "");

  // Chance of rain across the weekend's sessions that haven't finished, if they're forecast yet
  const time_t now = time(nullptr);
  time_t from = 0, to = 0;
  for (int i = 0; i < r.ns; i++) {
    const time_t a = r.s[i].start, b = a + r.s[i].mins * 60;
    if (b <= now) continue;
    if (!from || a < from) from = a;
    if (b > to) to = b;
  }
  const time_t horizon = now + 15L * 86400;
  if (from && from < horizon) {
    if (from < now) from = now;
    if (to > horizon) to = horizon;
    char a[20], b[20];
    isoHour(from, a, sizeof(a));
    isoHour(to, b, sizeof(b));
    n += snprintf(url + n, sizeof(url) - n, "&hourly=precipitation_probability&start_hour=%s&end_hour=%s", a, b);
  }

  JsonDocument filter;
  filter["current"] = true;
  filter["hourly"]["time"] = true;
  filter["hourly"]["precipitation_probability"] = true;
  filter["reason"] = true;

  // Plain http first (no secure handshake, so it works even when memory is tight), then https
  JsonDocument doc;
  bool ok = false;
  for (int attempt = 0; attempt < 2 && !ok; attempt++) {
    String u = url;
    if (attempt) u.replace("http://", "https://");
    WiFiClient plain;
    WiFiClientSecure secure;
    secure.setInsecure();
    HTTPClient http;
    http.useHTTP10(true);
    http.setTimeout(12000);
    http.setUserAgent(String(APP_NAME "/" APP_VERSION " (ESP32)"));
    if (!(attempt ? http.begin(secure, u) : http.begin(plain, u))) continue;
    const int code = http.GET();
    if (code == 200) {
      doc.clear();
      ok = !deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    } else {
      Serial.printf("[wx] %s -> HTTP %d %s\n", u.c_str(), code, code > 0 ? http.getString().substring(0, 120).c_str() : "");
    }
    http.end();
  }
  if (!ok || doc["current"].isNull()) return false;

  Weather w;
  memset(&w, 0, sizeof(w));
  w.round    = r.round;
  w.at       = now;
  w.imperial = imperial;
  JsonObjectConst c = doc["current"];
  w.temp     = c["temperature_2m"] | 0.0f;
  w.humidity = c["relative_humidity_2m"] | 0;
  w.wind     = c["wind_speed_10m"] | 0.0f;
  w.rainMm   = c["precipitation"] | 0.0f;
  w.code     = c["weather_code"] | 0;

  // highest hourly chance of rain while each session runs
  JsonArrayConst times = doc["hourly"]["time"], prob = doc["hourly"]["precipitation_probability"];
  for (int i = 0; i < r.ns; i++) {
    const time_t a = r.s[i].start, b = a + r.s[i].mins * 60;
    int best = -1;
    for (size_t k = 0; k < times.size() && k < prob.size(); k++) {
      const time_t h = (time_t)(times[k] | 0L);
      if (h + 3600 <= a || h >= b || prob[k].isNull()) continue;
      best = max(best, (int)(prob[k] | 0));
    }
    w.rain[i] = best;
  }
  for (int i = r.ns; i <= MAX_SESS; i++) w.rain[i] = -1;

  d.weather = w;
  Serial.printf("[wx] %s: %.1f%s %s, wind %.0f, humidity %u%%\n", r.locality, w.temp, imperial ? "F" : "C",
                weatherText(w.code), w.wind, w.humidity);
  return true;
}
