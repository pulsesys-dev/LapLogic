#include "logos.h"
#include "config.h"
#include "pngsmall.h"
#include <LittleFS.h>
#if BOARD_CYD
  #include <SD.h>
  #include <SPI.h>
#endif

// Badge-sized: rows on the Drivers page and the "your driver" card
#define LOGO_BOX_W (COMPACT ? 32 : 38)
#define LOGO_BOX_H (COMPACT ? 14 : 16)
#define MAX_LOGOS  24

struct Cached { char key[20]; uint8_t *data; lv_img_dsc_t dsc; bool missing; };
static char avail[MAX_LOGOS][20];   // logos stored in flash, by normalised name
static int nAvail = 0;
static Cached held[MAX_LOGOS];
static int nHeld = 0;
static char sdNote[64] = "";

// "Red Bull", "red_bull.png", "RedBull" -> "redbull"
static void norm(const char *in, char *out, size_t n) {
  size_t o = 0;
  for (; *in && o + 1 < n; in++) {
    if (*in == '.') break;   // stop at the file extension
    if (isalnum((unsigned char)*in)) out[o++] = tolower((unsigned char)*in);
  }
  out[o] = 0;
}

static String cachePath(const char *key) { return String("/lg_") + key + ".bin"; }

static void *bigAlloc(size_t n) {
#if BOARD_JC4827
  if (void *p = ps_malloc(n)) return p;
#endif
  return malloc(n);
}

// Decode one PNG and store it in flash as ready-to-draw pixels: "LLG1", w, h, then 3 bytes a pixel
static bool convert(const uint8_t *png, size_t len, const char *key, const char *fileName) {
  SmallImage im;
  const char *err = pngDecodeFit(png, len, LOGO_BOX_W, LOGO_BOX_H, im);
  if (err) { Serial.printf("[logo] %s: %s\n", fileName, err); return false; }
  const size_t n = (size_t)im.w * im.h;
  uint8_t *px = (uint8_t *)malloc(n * 3);
  if (!px) { free(im.rgba); return false; }
  for (size_t i = 0; i < n; i++) {
    const uint8_t *s = im.rgba + i * 4;
    lv_color_t c = lv_color_make(s[0], s[1], s[2]);
    px[i * 3] = c.full & 0xFF;
    px[i * 3 + 1] = c.full >> 8;
    px[i * 3 + 2] = s[3];
  }
  File f = LittleFS.open(cachePath(key), "w");
  bool ok = false;
  if (f) {
    const uint8_t hdr[6] = {'L', 'L', 'G', '1', (uint8_t)im.w, (uint8_t)im.h};
    ok = f.write(hdr, 6) == 6 && f.write(px, n * 3) == n * 3;
    f.close();
  }
  free(px);
  free(im.rgba);
  if (ok) Serial.printf("[logo] %s -> %dx%d\n", fileName, im.w, im.h);
  return ok;
}

static void scanFlash() {
  nAvail = 0;
  File root = LittleFS.open("/");
  if (!root) return;
  for (File f = root.openNextFile(); f && nAvail < MAX_LOGOS; f = root.openNextFile()) {
    String name = f.name();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name.startsWith("lg_") && name.endsWith(".bin")) {
      String key = name.substring(3, name.length() - 4);
      strlcpy(avail[nAvail++], key.c_str(), sizeof(avail[0]));
    }
    f.close();
  }
}

#if BOARD_CYD
// The SD card shares the bus the touch screen uses later, so it's read once, here, and released.
static void readSdCard() {
  SPIClass sdSpi(VSPI);
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSpi, 20000000)) {
    Serial.println("[logo] no SD card");
    sdSpi.end();
    return;
  }
  File dir = SD.open("/logos");
  if (!dir || !dir.isDirectory()) {
    Serial.println("[logo] SD card has no /logos folder");
    snprintf(sdNote, sizeof(sdNote), "SD card has no /logos folder");
  } else {
    // The card is the master copy: replace whatever logos were copied before
    scanFlash();
    for (int i = 0; i < nAvail; i++) LittleFS.remove(cachePath(avail[i]));
    int ok = 0, bad = 0;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String name = f.name();
      const int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      String lower = name; lower.toLowerCase();
      if (f.isDirectory() || name.startsWith(".") || !lower.endsWith(".png")) { f.close(); continue; }
      char key[20];
      norm(name.c_str(), key, sizeof(key));
      const size_t len = f.size();
      if (!*key || len > 200000) { Serial.printf("[logo] %s: skipped (%s)\n", name.c_str(), *key ? "file over 200 KB" : "bad name"); bad++; f.close(); continue; }
      uint8_t *buf = (uint8_t *)malloc(len);
      if (buf && f.read(buf, len) == len && convert(buf, len, key, name.c_str())) ok++;
      else bad++;
      free(buf);
      f.close();
    }
    snprintf(sdNote, sizeof(sdNote), "%d read from the SD card%s", ok, bad ? ", some skipped (see log)" : "");
  }
  if (dir) dir.close();
  SD.end();
  sdSpi.end();
}
#endif

void logosBegin() {
  if (!LittleFS.begin(true)) { Serial.println("[logo] flash storage unavailable"); return; }
#if BOARD_CYD
  readSdCard();
#endif
  scanFlash();
  Serial.printf("[logo] %d team logo%s available\n", nAvail, nAvail == 1 ? "" : "s");
}

static const lv_img_dsc_t *load(const char *key) {
  for (int i = 0; i < nHeld; i++)
    if (!strcmp(held[i].key, key)) return held[i].missing ? nullptr : &held[i].dsc;
  if (nHeld >= MAX_LOGOS) return nullptr;
  Cached &c = held[nHeld++];
  memset(&c, 0, sizeof(c));
  strlcpy(c.key, key, sizeof(c.key));
  c.missing = true;
  File f = LittleFS.open(cachePath(key), "r");
  if (!f) return nullptr;
  uint8_t hdr[6];
  if (f.read(hdr, 6) == 6 && !memcmp(hdr, "LLG1", 4) && hdr[4] && hdr[5]) {
    const size_t n = (size_t)hdr[4] * hdr[5] * 3;
    c.data = (uint8_t *)bigAlloc(n);
    if (c.data && f.read(c.data, n) == n) {
      c.dsc.header.always_zero = 0;
      c.dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
      c.dsc.header.w = hdr[4];
      c.dsc.header.h = hdr[5];
      c.dsc.data_size = n;
      c.dsc.data = c.data;
      c.missing = false;
    } else { free(c.data); c.data = nullptr; }
  }
  f.close();
  return c.missing ? nullptr : &c.dsc;
}

const lv_img_dsc_t *logoFor(const char *teamId, const char *teamName) {
  if (!nAvail) return nullptr;
  char k[2][20];
  norm(teamId ? teamId : "", k[0], sizeof(k[0]));
  norm(teamName ? teamName : "", k[1], sizeof(k[1]));
  for (auto &key : k)
    for (int i = 0; i < nAvail; i++)
      if (*key && !strcmp(avail[i], key)) return load(key);
  return nullptr;
}

void logosRelease() {
  for (int i = 0; i < nHeld; i++) free(held[i].data);
  nHeld = 0;
}

String logosStatus() {
  String s = nAvail ? String(nAvail) + " team logo" + (nAvail == 1 ? "" : "s") : String("none");
#if BOARD_CYD
  if (*sdNote) s += String(" (") + sdNote + ")";
  else if (!nAvail) s += " - add PNG files to a /logos folder on the SD card";
#endif
  return s;
}
