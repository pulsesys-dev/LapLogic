#include "led.h"
#include "config.h"
#include "settings.h"
#include <math.h>

#if BOARD_CYD

static const int CH_R = 1, CH_G = 2, CH_B = 3;   // LEDC channels (0 is the backlight)
static char status[48] = "off";
static uint32_t testUntil = 0;

bool ledSupported() { return true; }

void ledBegin() {
  const int pins[3] = {LED_R, LED_G, LED_B}, ch[3] = {CH_R, CH_G, CH_B};
  for (int i = 0; i < 3; i++) {
    ledcSetup(ch[i], 5000, 8);
    ledcAttachPin(pins[i], ch[i]);
    ledcWrite(ch[i], 255);   // active low: 255 = off
  }
}

static void show(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CH_R, 255 - r);
  ledcWrite(CH_G, 255 - g);
  ledcWrite(CH_B, 255 - b);
}

void ledTest() { testUntil = millis() + 8000; }

const char *ledStatus() { return status; }

// What the light should be right now: colour (0xRRGGBB) and how bright (0..1)
static uint32_t pick(const F1Data *d, time_t now, float &level) {
  level = 1;
  if (testUntil && (int32_t)(testUntil - millis()) > 0) {
    static const uint32_t cycle[] = {0xFF0000, 0x00FF00, 0x0050FF, 0xA000FF, 0xFF6E00, 0xFFFFFF};
    snprintf(status, sizeof(status), "testing");
    return cycle[(millis() / 1000) % 6];
  }
  testUntil = 0;
  if (!settings.led) { snprintf(status, sizeof(status), "off (switched off)"); return 0; }
  if (!d || !d->nRaces || now < 1600000000) { snprintf(status, sizeof(status), "off"); return 0; }
  const int ri = nextRaceIndex(*d, now);
  if (ri < 0) { snprintf(status, sizeof(status), "off - season over"); return 0; }
  const Race &r = d->races[ri];
  for (int i = 0; i < r.ns; i++) {
    const Session &s = r.s[i];
    const long t = (long)(now - s.start);               // seconds since the start (negative = before)
    const bool raceLike = s.kind == S_RACE || s.kind == S_SPRINT;
    if (raceLike && t >= -600 && t < -10) {             // grid time: slow amber breathing
      level = 0.25f + 0.75f * (0.5f + 0.5f * sinf(millis() / 1000.0f * 2.0f));
      snprintf(status, sizeof(status), "amber - %s about to start", sessionName(s.kind));
      return 0xFF6E00;
    }
    if (raceLike && t >= -10 && t < 0) {                // start lights: red, building up
      level = 0.3f + 0.07f * (t + 10);
      snprintf(status, sizeof(status), "red - start lights");
      return 0xFF0000;
    }
    if (raceLike && t >= 0 && t < 1) { snprintf(status, sizeof(status), "lights out"); return 0; }
    if (t >= 0 && t < (long)s.mins * 60) {
      uint32_t c = s.kind == S_RACE || s.kind == S_SPRINT ? 0x00FF00
                 : (s.kind == S_QUALI || s.kind == S_SQ) ? 0xA000FF : 0x0050FF;
      snprintf(status, sizeof(status), "%s - %s is on", c == 0x00FF00 ? "green" : c == 0xA000FF ? "purple" : "blue",
               sessionName(s.kind));
      return c;
    }
  }
  snprintf(status, sizeof(status), "off - no session running");
  return 0;
}

void ledLoop(const F1Data *d, uint8_t screenLevel) {
  static uint32_t last = 0;
  if (millis() - last < 30) return;
  last = millis();
  float level;
  const uint32_t c = pick(d, time(nullptr), level);
  // follow the screen's brightness (night dimming, auto-dim), but never brighter than LED_MAX
  const float k = level * LED_MAX / 255.0f * (0.15f + 0.85f * screenLevel / 255.0f);
  show((uint8_t)(((c >> 16) & 255) * k), (uint8_t)(((c >> 8) & 255) * k), (uint8_t)((c & 255) * k));
}

#else
bool ledSupported() { return false; }
void ledBegin() {}
void ledLoop(const F1Data *, uint8_t) {}
void ledTest() {}
const char *ledStatus() { return ""; }
#endif
