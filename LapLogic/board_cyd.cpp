// ESP32-2432S028R "Cheap Yellow Display"
// ILI9341 (1 USB port) or ST7789 (2 USB ports) + XPT2046 resistive touch
#include "config.h"
#if BOARD_CYD

#include "board.h"
#include "settings.h"
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <Preferences.h>
#include <lvgl.h>

// ------------------------------------------------------------------ display

// The 2-USB CYD's ST7789 is wired blue/red swapped, so set the BGR bit.
class ST7789_BGR : public Arduino_ST7789 {
public:
  using Arduino_ST7789::Arduino_ST7789;
  void setRotation(uint8_t r) override {
    Arduino_ST7789::setRotation(r);
    uint8_t m;
    switch (_rotation) {
      case 1: m = 0x40 | 0x20; break;   // MX | MV
      case 2: m = 0x40 | 0x80; break;   // MX | MY
      case 3: m = 0x80 | 0x20; break;   // MY | MV
      default: m = 0x00;
    }
    _bus->beginWrite();
    _bus->writeC8D8(0x36, m | 0x08);    // MADCTL with BGR
    _bus->endWrite();
  }
};

enum Panel : uint8_t { P_AUTO = 0, P_ILI9341 = 1, P_ST7789 = 2 };

static Arduino_DataBus *bus = nullptr;
static Arduino_GFX     *gfx = nullptr;
static Panel panel = P_ILI9341;
static bool  panelDetected = false;
static bool  flip = false;

static const int BL_CH = 0;
static uint8_t rot = 1;   // panel rotation in use
// Landscape is rotation 1/3 on this panel, portrait 0/2
static uint8_t rotationFor(bool portrait, bool flipped) { return portrait ? (flipped ? 2 : 0) : (flipped ? 3 : 1); }

static bool autoInvert = false;   // detected: this panel needs its colours inverted
static bool inverted = false;     // what's applied now

// Ask the controller who it is. ILI9341 answers 0x93 0x41 to RDID4 (0xD3);
// ST7789 answers 0x85 0x85 0x52 to RDDID (0x04), after one dummy clock.
// Some newer CYD batches answer neither and report RDDID 81 81 B3; those need inverted colours.
static Panel detectPanel() {
  SPIClass spi(HSPI);
  spi.begin(LCD_SCK, LCD_MISO, LCD_MOSI, -1);
  pinMode(LCD_CS, OUTPUT);
  pinMode(LCD_DC, OUTPUT);
  digitalWrite(LCD_CS, HIGH);

  auto readCmd = [&](uint8_t cmd, uint8_t *out, int n) {
    spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(LCD_CS, LOW);
    digitalWrite(LCD_DC, LOW);
    spi.transfer(cmd);
    digitalWrite(LCD_DC, HIGH);
    for (int i = 0; i < n; i++) out[i] = spi.transfer(0x00);
    digitalWrite(LCD_CS, HIGH);
    spi.endTransaction();
  };

  uint8_t a[4] = {0}, b[4] = {0};
  delay(120);                       // controller ready after power-on reset
  readCmd(0xD3, a, 4);
  readCmd(0x04, b, 4);
  spi.end();

  const uint32_t raw = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
  const uint32_t stId = (raw >> 7) & 0xFFFFFF;
  Serial.printf("[cyd] RDID4 %02X %02X %02X %02X  RDDID %06lX\n", a[0], a[1], a[2], a[3], (unsigned long)stId);

  if (a[2] == 0x93 && a[3] == 0x41) return P_ILI9341;
  if ((stId >> 16) == 0x85) return P_ST7789;
  // Seen on a 2026 CYD batch: reads back as 81 81 B3, sometimes 00 81 B3 - match the last two bytes
  if ((stId & 0xFFFF) == 0x81B3) { autoInvert = true; return P_ILI9341; }
  return P_AUTO;   // couldn't tell
}

// ------------------------------------------------------------------ touch

static SPIClass touchSpi(VSPI);

struct Cal { float ax, bx, cx, ay, by, cy; uint8_t rot; bool ok; };   // rot: panel rotation when calibrated
// Typical values for a CYD in landscape; replaced by calibration.
static Cal cal = {320.0f / 3500.0f, 0, -200.0f * 320.0f / 3500.0f,
                  0, 240.0f / 3560.0f, -240.0f * 240.0f / 3560.0f, 1, false};

static volatile bool calibrating = false;
static volatile uint32_t lastTouch = 0;
static volatile bool touching = false;

static uint16_t xfer16(uint16_t v) { return touchSpi.transfer16(v); }

// Raw 12-bit reading. Returns true while pressed hard enough.
static bool touchRaw(int &rx, int &ry) {
  if (digitalRead(TP_IRQ) == HIGH) return false;   // pen-up shortcut
  touchSpi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(TP_CS, LOW);
  touchSpi.transfer(0xB1);                          // Z1
  int z1 = xfer16(0xC1) >> 3;                       // Z2 next
  int z = z1 + 4095;
  int z2 = xfer16(0x91) >> 3;                       // Y next
  z -= z2;
  int d[6] = {0};
  if (z >= TP_Z_THRESHOLD) {
    xfer16(0x91);                                   // first Y is noisy, discard
    d[0] = xfer16(0xD1) >> 3;  d[1] = xfer16(0x91) >> 3;
    d[2] = xfer16(0xD1) >> 3;  d[3] = xfer16(0x91) >> 3;
  }
  d[4] = xfer16(0xD0) >> 3;                         // last Y, power down with IRQ enabled
  d[5] = xfer16(0) >> 3;
  digitalWrite(TP_CS, HIGH);
  touchSpi.endTransaction();
  if (z < TP_Z_THRESHOLD) return false;

  // the two closest of three samples, averaged (noise rejection)
  auto best = [](int a, int b, int c) {
    int dab = abs(a - b), dac = abs(a - c), dbc = abs(b - c);
    if (dab <= dac && dab <= dbc) return (a + b) / 2;
    if (dac <= dab && dac <= dbc) return (a + c) / 2;
    return (b + c) / 2;
  };
  ry = best(d[1], d[3], d[5]);   // 0x91 channel
  rx = best(d[0], d[2], d[4]);   // 0xD1 channel
  return true;
}

static void loadCal() {
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  Cal c;
  if (p.getBytesLength("cal2") == sizeof(Cal) && p.getBytes("cal2", &c, sizeof(Cal)) == sizeof(Cal) && c.ok) {
    cal = c;
  } else if (p.getBytesLength("cal") == sizeof(Cal) && p.getBytes("cal", &c, sizeof(Cal)) == sizeof(Cal) && c.ok) {
    c.rot = c.rot ? 3 : 1;   // v1.1 stored a landscape "flipped" flag in this byte
    cal = c;
  }
  p.end();
}

static void saveCal() {
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putBytes("cal2", &cal, sizeof(Cal));
  p.end();
}

static bool mapTouch(int rx, int ry, int &x, int &y) {
  const int w = gfx->width(), h = gfx->height();
  float fx = cal.ax * rx + cal.bx * ry + cal.cx;
  float fy = cal.ay * rx + cal.by * ry + cal.cy;
  if (((rot - cal.rot) & 3) == 2) { fx = w - 1 - fx; fy = h - 1 - fy; }   // calibrated upside down
  x = constrain((int)lroundf(fx), 0, w - 1);
  y = constrain((int)lroundf(fy), 0, h - 1);
  return true;
}

// ------------------------------------------------------------------ LVGL

static lv_disp_draw_buf_t drawBuf;
static lv_disp_drv_t      dispDrv;
static lv_indev_drv_t     indevDrv;

static void flushCb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  const int32_t w = a->x2 - a->x1 + 1;
  const int32_t h = a->y2 - a->y1 + 1;
  gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t *)&px->full, w, h);
  lv_disp_flush_ready(drv);
}

static void touchCb(lv_indev_drv_t *, lv_indev_data_t *data) {
  int rx, ry;
  const bool down = !calibrating && touchRaw(rx, ry);
  touching = down;
  if (down) {
    int x, y;
    mapTouch(rx, ry, x, y);
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
    lastTouch = millis();
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ------------------------------------------------------------------ public

void boardSetBacklight(uint8_t level) { ledcWrite(BL_CH, level); }

void boardBegin() {
  flip = settings.flip;
  rot = rotationFor(settings.portrait, flip);

  // RGB LED off (active low)
  for (int p : {LED_R, LED_G, LED_B}) { pinMode(p, OUTPUT); digitalWrite(p, HIGH); }

  pinMode(LDR_PIN, INPUT);
  analogSetPinAttenuation(LDR_PIN, ADC_0db);   // the sensor only swings a few hundred mV

  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(LCD_BL, BL_CH);
  boardSetBacklight(0);

  if (settings.panel == P_ILI9341 || settings.panel == P_ST7789) {
    panel = (Panel)settings.panel;
  } else {
    // The ID read can glitch, so remember the last confident answer and fall back to it
    Preferences pref;
    pref.begin(NVS_NAMESPACE, false);
    Panel d = detectPanel();
    panelDetected = d != P_AUTO;
    if (panelDetected) {
      const uint8_t code = d == P_ST7789 ? 3 : (autoInvert ? 2 : 1);
      if (pref.getUChar("pdet", 0) != code) pref.putUChar("pdet", code);
    } else {
      const uint8_t code = pref.getUChar("pdet", 0);
      if (code) { d = code == 3 ? P_ST7789 : P_ILI9341; autoInvert = code == 2; panelDetected = true; }
      Serial.printf("[cyd] panel ID unclear, using remembered type %u\n", code);
    }
    pref.end();
    panel = panelDetected ? d : P_ILI9341;   // the original CYD is by far the most common
  }

  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, LCD_MISO, HSPI);
  if (panel == P_ST7789) gfx = new ST7789_BGR(bus, GFX_NOT_DEFINED, rot, false, 240, 320);
  else                   gfx = new Arduino_ILI9341(bus, GFX_NOT_DEFINED, rot, false);
  if (!gfx->begin(LCD_SPEED_HZ)) Serial.println("Display init failed");
  inverted = settings.colours == 2 || (settings.colours == 0 && autoInvert);
  gfx->invertDisplay(inverted);
  gfx->fillScreen(0x0000);
  Serial.printf("[cyd] panel %s\n", boardPanelName());

  touchSpi.begin(TP_CLK, TP_MISO, TP_MOSI, TP_CS);
  pinMode(TP_CS, OUTPUT);
  digitalWrite(TP_CS, HIGH);
  pinMode(TP_IRQ, INPUT);
  loadCal();

  lv_init();
  const size_t px = LCD_W * LV_BUF_LINES;
  lv_color_t *b1 = (lv_color_t *)malloc(px * sizeof(lv_color_t));
  lv_disp_draw_buf_init(&drawBuf, b1, nullptr, px);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res  = gfx->width();     // swapped in portrait
  dispDrv.ver_res  = gfx->height();
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type    = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchCb;
  lv_indev_drv_register(&indevDrv);

  boardSetBacklight(255 * settings.bright / 100);
}

void boardSetFlip(bool flipped) {
  if (flipped == flip) return;
  flip = flipped;
  rot = rotationFor(settings.portrait, flip);
  gfx->setRotation(rot);
  lv_obj_invalidate(lv_scr_act());
}

void boardSetColours(uint8_t mode) {
  const bool inv = mode == 2 || (mode == 0 && autoInvert);
  if (inv == inverted) return;
  inverted = inv;
  gfx->invertDisplay(inv);
  lv_obj_invalidate(lv_scr_act());
}

bool boardColoursInverted() { return inverted; }

uint32_t boardLastTouchMs() { return lastTouch; }
bool     boardTouching()    { return touching; }
void     boardLoop()        { lv_timer_handler(); }
int      boardLightRaw()    { return analogRead(LDR_PIN); }
bool     boardCanCalibrate(){ return true; }
// Calibration done in the other orientation (portrait vs landscape) has to be redone
bool     boardIsCalibrated(){ return cal.ok && ((rot - cal.rot) & 1) == 0; }

const char *boardPanelName() {
  static char buf[32];
  snprintf(buf, sizeof(buf), "%s%s (%s)", panel == P_ST7789 ? "ST7789" : "ILI9341", inverted ? ", inverted" : "",
           settings.panel ? "set manually" : (panelDetected ? "detected" : "default"));
  return buf;
}

// ------------------------------------------------------------------ calibration

static void pump(uint32_t ms) {
  const uint32_t until = millis() + ms;
  do { lv_timer_handler(); delay(10); } while ((int32_t)(millis() - until) < 0);
}

// Wait for a press, average it, wait for release. Returns false on timeout.
static bool samplePoint(int &rx, int &ry, uint32_t timeoutMs) {
  int x, y;
  uint32_t start = millis();
  while (touchRaw(x, y)) pump(20);                   // finger still down from before
  while (!touchRaw(x, y)) {
    pump(20);
    if (timeoutMs && millis() - start > timeoutMs) return false;
  }
  long sx = 0, sy = 0; int n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 400 || n < 8) {
    if (touchRaw(x, y)) { sx += x; sy += y; n++; }
    else if (n >= 4) break;
    delay(15);
  }
  while (touchRaw(x, y)) pump(20);
  if (n == 0) return false;
  rx = sx / n; ry = sy / n;
  return true;
}

void boardCalibrate() {
  calibrating = true;
  const bool firstTime = !cal.ok;
  lv_obj_t *prev = lv_scr_act();

  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B0B10), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *title = lv_label_create(scr);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xF2F2F5), 0);
  lv_label_set_text(title, "Touch calibration");
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -18);
  lv_obj_t *hint = lv_label_create(scr);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0x8C8C9A), 0);
  lv_label_set_text(hint, "Tap the centre of each red cross.\nA fingernail or stylus works best.");
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, 14);

  auto bar = [&](int w, int h) {
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(0xE10600), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
  };
  lv_obj_t *hx = bar(25, 3), *vx = bar(3, 25);
  lv_scr_load(scr);

  const int W = gfx->width(), H = gfx->height();
  const int px[3][2] = {{30, 30}, {W - 30, H / 2}, {W / 2, H - 30}};
  int rx[3], ry[3];
  bool ok = true;
  for (int i = 0; i < 3 && ok; i++) {
    lv_obj_set_pos(hx, px[i][0] - 12, px[i][1] - 1);
    lv_obj_set_pos(vx, px[i][0] - 1, px[i][1] - 12);
    pump(50);
    ok = samplePoint(rx[i], ry[i], firstTime ? 60000 : 0);
    if (ok) {
      lv_obj_set_style_bg_color(hx, lv_color_hex(0x2BD47D), 0);
      lv_obj_set_style_bg_color(vx, lv_color_hex(0x2BD47D), 0);
      pump(250);
      lv_obj_set_style_bg_color(hx, lv_color_hex(0xE10600), 0);
      lv_obj_set_style_bg_color(vx, lv_color_hex(0xE10600), 0);
    }
  }

  if (ok) {
    // Solve screen = a*rx + b*ry + c for x and y through the three points
    const float det = (float)rx[0] * (ry[1] - ry[2]) - (float)ry[0] * (rx[1] - rx[2]) +
                      ((float)rx[1] * ry[2] - (float)rx[2] * ry[1]);
    if (fabsf(det) < 1000.0f) ok = false;   // taps too close together
    else {
      auto solve = [&](int s0, int s1, int s2, float &a, float &b, float &c) {
        a = ((float)s0 * (ry[1] - ry[2]) - (float)ry[0] * (s1 - s2) + ((float)s1 * ry[2] - (float)s2 * ry[1])) / det;
        b = ((float)rx[0] * (s1 - s2) - (float)s0 * (rx[1] - rx[2]) + ((float)rx[1] * s2 - (float)rx[2] * s1)) / det;
        c = ((float)rx[0] * ((float)ry[1] * s2 - (float)ry[2] * s1) - (float)ry[0] * ((float)rx[1] * s2 - (float)rx[2] * s1) +
             (float)s0 * ((float)rx[1] * ry[2] - (float)rx[2] * ry[1])) / det;
      };
      Cal c;
      solve(px[0][0], px[1][0], px[2][0], c.ax, c.bx, c.cx);
      solve(px[0][1], px[1][1], px[2][1], c.ay, c.by, c.cy);
      c.rot = rot;
      c.ok = true;
      // sanity check: a 4096-count axis should span roughly the screen
      const float sx = fabsf(c.ax) + fabsf(c.bx), sy = fabsf(c.ay) + fabsf(c.by);
      if (sx < 0.02f || sx > 0.5f || sy < 0.02f || sy > 0.5f) ok = false;
      else { cal = c; saveCal(); }
    }
  }

  lv_label_set_text(title, ok ? "Calibrated" : (firstTime ? "Skipped - using defaults" : "Didn't work - try again"));
  lv_label_set_text(hint, ok ? "" : "You can calibrate later from the Settings page.");
  lv_obj_add_flag(hx, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(vx, LV_OBJ_FLAG_HIDDEN);
  pump(1200);

  lv_scr_load(prev);
  lv_obj_del(scr);
  calibrating = false;
}

#endif  // BOARD_CYD
