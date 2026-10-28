#include "settings.h"
#include "config.h"
#include <Preferences.h>
#include <time.h>

Settings settings;

// Common timezones with their daylight-saving rules.
// Add more if needed: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
const TzEntry TZ_TABLE[] = {
  {"Australia/Brisbane",   "AEST-10"},
  {"Australia/Sydney",     "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"Australia/Melbourne",  "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"Australia/Hobart",     "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"Australia/Adelaide",   "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
  {"Australia/Darwin",     "ACST-9:30"},
  {"Australia/Perth",      "AWST-8"},
  {"Pacific/Auckland",     "NZST-12NZDT,M9.5.0,M4.1.0/3"},
  {"Asia/Tokyo",           "JST-9"},
  {"Asia/Seoul",           "KST-9"},
  {"Asia/Shanghai",        "CST-8"},
  {"Asia/Hong_Kong",       "HKT-8"},
  {"Asia/Singapore",       "<+08>-8"},
  {"Asia/Kuala_Lumpur",    "<+08>-8"},
  {"Asia/Manila",          "PST-8"},
  {"Asia/Jakarta",         "WIB-7"},
  {"Asia/Bangkok",         "<+07>-7"},
  {"Asia/Kolkata",         "IST-5:30"},
  {"Asia/Karachi",         "PKT-5"},
  {"Asia/Dubai",           "<+04>-4"},
  {"Asia/Baku",            "<+04>-4"},
  {"Asia/Qatar",           "<+03>-3"},
  {"Asia/Riyadh",          "<+03>-3"},
  {"Europe/Istanbul",      "<+03>-3"},
  {"Europe/Moscow",        "MSK-3"},
  {"Europe/Athens",        "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"Europe/Helsinki",      "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"Europe/Berlin",        "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Paris",         "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Rome",          "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Madrid",        "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Amsterdam",     "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Brussels",      "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Vienna",        "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Zurich",        "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Monaco",        "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Stockholm",     "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Warsaw",        "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Budapest",      "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/London",        "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Europe/Dublin",        "GMT0IST,M3.5.0/1,M10.5.0"},
  {"Europe/Lisbon",        "WET0WEST,M3.5.0/1,M10.5.0"},
  {"Africa/Johannesburg",  "SAST-2"},
  {"Africa/Cairo",         "EET-2EEST,M4.5.5/0,M10.5.4/24"},
  {"Africa/Lagos",         "WAT-1"},
  {"America/Sao_Paulo",    "<-03>3"},
  {"America/Argentina/Buenos_Aires", "<-03>3"},
  {"America/Mexico_City",  "CST6"},
  {"America/New_York",     "EST5EDT,M3.2.0,M11.1.0"},
  {"America/Toronto",      "EST5EDT,M3.2.0,M11.1.0"},
  {"America/Chicago",      "CST6CDT,M3.2.0,M11.1.0"},
  {"America/Denver",       "MST7MDT,M3.2.0,M11.1.0"},
  {"America/Phoenix",      "MST7"},
  {"America/Los_Angeles",  "PST8PDT,M3.2.0,M11.1.0"},
  {"America/Vancouver",    "PST8PDT,M3.2.0,M11.1.0"},
  {"America/Anchorage",    "AKST9AKDT,M3.2.0,M11.1.0"},
  {"Pacific/Honolulu",     "HST10"},
  {"UTC",                  "UTC0"},
};
const size_t TZ_COUNT = sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0]);

const char *tzLookup(const String &iana) {
  for (size_t i = 0; i < TZ_COUNT; i++)
    if (iana.equalsIgnoreCase(TZ_TABLE[i].iana)) return TZ_TABLE[i].posix;
  return nullptr;
}

String tzFromOffset(long off) {
  // POSIX offsets are inverted: UTC+10 is written "UTC-10"
  long a = labs(off);
  int h = a / 3600, m = (a % 3600) / 60;
  String s = "UTC";
  s += (off >= 0) ? "-" : "+";
  s += String(h);
  if (m) { s += ":"; if (m < 10) s += "0"; s += String(m); }
  return s;
}

void settingsLoad() {
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  settings.tzName   = p.getString("tzName", "auto");
  settings.tzPosix  = p.getString("tz", "UTC0");
  settings.fav      = p.getString("fav", "");
  settings.rotateS  = p.getUChar("rot", AUTO_ROTATE_DEFAULT_S);
  // before v1.3 "0 seconds" meant off; now on/off is separate and keeps the timing
  settings.rotateOn = p.isKey("rotOn") ? p.getBool("rotOn", true) : settings.rotateS != 0;
  if (settings.rotateS < 5 || settings.rotateS > 120) settings.rotateS = AUTO_ROTATE_DEFAULT_S;
  settings.units    = p.getUChar("units", 0);
  settings.light    = p.getBool("light", false);
  settings.bright   = constrain((int)p.getUChar("bright", 100), 5, 100);
  settings.autoDim  = p.getBool("adim", true);
  settings.led      = p.getBool("led", true);
  settings.updCheck = p.getBool("upd", true);
  settings.flip     = p.getBool("flip", false);
  settings.portrait = p.getBool("port", false);
  settings.nightDim = p.getBool("night", true);
  settings.h24      = p.getBool("h24", false);
  settings.panel    = p.getUChar("panel", 0);
  settings.ofUser   = p.getString("ofu", "");
  settings.ofPass   = p.getString("ofp", "");
  // "inv" replaced the older on/off "invert" key; carry an old "on" over as Inverted
  settings.colours  = p.isKey("inv") ? p.getUChar("inv", 0) : (p.getBool("invert", false) ? 2 : 0);
  p.end();
  settings.fav.toUpperCase();
}

void settingsSave() {
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putString("tzName", settings.tzName);
  p.putString("tz", settings.tzPosix);
  p.putString("fav", settings.fav);
  p.putUChar("rot", settings.rotateS);
  p.putBool("flip", settings.flip);
  p.putBool("port", settings.portrait);
  p.putBool("night", settings.nightDim);
  p.putBool("h24", settings.h24);
  p.putUChar("panel", settings.panel);
  p.putString("ofu", settings.ofUser);
  p.putString("ofp", settings.ofPass);
  p.putUChar("inv", settings.colours);
  p.putBool("rotOn", settings.rotateOn);
  p.putUChar("units", settings.units);
  p.putBool("light", settings.light);
  p.putUChar("bright", settings.bright);
  p.putBool("adim", settings.autoDim);
  p.putBool("led", settings.led);
  p.putBool("upd", settings.updCheck);
  p.end();
}

void applyTimezone() {
  setenv("TZ", settings.tzPosix.c_str(), 1);
  tzset();
}

// ---------------------------------------------------------------- settings by name

static String onOff(bool b) { return b ? "1" : "0"; }
static bool truthy(const String &v) { return v == "1" || v.equalsIgnoreCase("true") || v.equalsIgnoreCase("on") || v.equalsIgnoreCase("yes"); }
static bool boolish(const String &v) { return truthy(v) || v == "0" || v.equalsIgnoreCase("false") || v.equalsIgnoreCase("off") || v.equalsIgnoreCase("no"); }

void settingsForEach(void (*fn)(void *ctx, const char *key, const String &value), void *ctx) {
  fn(ctx, "board", BOARD_CYD ? "cyd" : "guition");
  fn(ctx, "version", APP_VERSION);
  fn(ctx, "tz", settings.tzName);
  fn(ctx, "fav", settings.fav);
  fn(ctx, "rotOn", onOff(settings.rotateOn));
  fn(ctx, "rot", String(settings.rotateS));
  fn(ctx, "orient", settings.portrait ? "portrait" : "landscape");
  fn(ctx, "flip", onOff(settings.flip));
  fn(ctx, "h24", onOff(settings.h24));
  fn(ctx, "night", onOff(settings.nightDim));
  fn(ctx, "units", settings.units ? "imperial" : "metric");
  fn(ctx, "theme", settings.light ? "light" : "dark");
  fn(ctx, "bright", String(settings.bright));
  fn(ctx, "updCheck", onOff(settings.updCheck));
#if BOARD_CYD
  static const char *colours[] = {"auto", "normal", "inverted"};
  static const char *panels[] = {"auto", "ili9341", "st7789"};
  fn(ctx, "colours", colours[min((int)settings.colours, 2)]);
  fn(ctx, "panel", panels[min((int)settings.panel, 2)]);
  fn(ctx, "autoDim", onOff(settings.autoDim));
  fn(ctx, "led", onOff(settings.led));
#endif
#if BOARD_JC4827
  fn(ctx, "ofUser", settings.ofUser);
  fn(ctx, "ofPassSet", onOff(settings.ofPass.length() > 0));   // the password itself is never sent back
#endif
}

String settingsSet(Settings &s, const char *key, const String &value) {
  String v = value;
  v.trim();
  const String k = key;
  if (k == "tz") {
    if (v == "auto") { s.tzName = "auto"; return ""; }
    const char *posix = tzLookup(v);
    if (!posix) return "unknown time zone";
    s.tzName = v; s.tzPosix = posix;
    return "";
  }
  if (k == "fav") {
    String f;
    for (char c : v) if (isalpha((unsigned char)c)) f += (char)toupper(c);
    if (f.length() > 3) return "driver code is 3 letters";
    s.fav = f;
    return "";
  }
  if (k == "rot") {
    const int n = v.toInt();
    if (n < 5 || n > 120) return "seconds per page must be 5 to 120";
    s.rotateS = n;
    return "";
  }
  if (k == "rotOn" || k == "flip" || k == "h24" || k == "night" || k == "updCheck" || (BOARD_CYD && (k == "autoDim" || k == "led"))) {
    if (!boolish(v)) return "expected on or off";
    const bool b = truthy(v);
    if (k == "updCheck") s.updCheck = b;
    else if (k == "rotOn") s.rotateOn = b; else if (k == "flip") s.flip = b; else if (k == "h24") s.h24 = b;
    else if (k == "night") s.nightDim = b; else if (k == "led") s.led = b; else s.autoDim = b;
    return "";
  }
  if (k == "theme") {
    if (v != "dark" && v != "light") return "dark or light";
    s.light = v == "light";
    return "";
  }
  if (k == "bright") {
    const int n = v.toInt();
    if (n < 5 || n > 100) return "brightness must be 5 to 100";
    s.bright = n;
    return "";
  }
  if (k == "orient") {
    if (v != "landscape" && v != "portrait") return "landscape or portrait";
    s.portrait = v == "portrait";
    return "";
  }
  if (k == "units") {
    if (v != "metric" && v != "imperial") return "metric or imperial";
    s.units = v == "imperial";
    return "";
  }
#if BOARD_CYD
  if (k == "colours") {
    if (v == "auto") s.colours = 0; else if (v == "normal") s.colours = 1; else if (v == "inverted") s.colours = 2;
    else return "auto, normal or inverted";
    return "";
  }
  if (k == "panel") {
    if (v == "auto") s.panel = 0; else if (v == "ili9341") s.panel = 1; else if (v == "st7789") s.panel = 2;
    else return "auto, ili9341 or st7789";
    return "";
  }
#endif
#if BOARD_JC4827
  if (k == "ofUser") {
    String u;
    for (char c : v) if (c > ' ' && c != '"' && c != '\'' && c != '<' && c != '>') u += c;
    if (u.length() > 96) return "email too long";
    s.ofUser = u;
    return "";
  }
  if (k == "ofPass") {               // blank keeps the saved password
    if (value.length() > 128) return "password too long";
    if (value.length()) s.ofPass = value;
    return "";
  }
  if (k == "ofClear") {
    if (truthy(v)) { s.ofUser = ""; s.ofPass = ""; }
    return "";
  }
#endif
  if (k == "board" || k == "version" || k == "ofPassSet") return "";   // read-only, ignored
  return "unknown setting";
}
