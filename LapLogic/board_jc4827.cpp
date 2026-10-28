// Guition JC4827W543C: NV3041A (QSPI) + GT911 capacitive touch
#include "config.h"
#if BOARD_JC4827

#include "board.h"
#include "settings.h"
#include "gt911.h"
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

static Arduino_DataBus *bus = nullptr;
static Arduino_GFX     *gfx = nullptr;
static GT911            touch;

static lv_disp_draw_buf_t drawBuf;
static lv_disp_drv_t      dispDrv;
static lv_indev_drv_t     indevDrv;

static volatile uint32_t lastTouch = 0;
static volatile bool touching = false;
static bool flip = false;
static uint8_t rot = 0;   // display rotation 0..3 (see rotationFor)

// 0 = landscape, 1 = portrait, 2 = landscape flipped, 3 = portrait flipped
static uint8_t rotationFor(bool portrait, bool flipped) { return (portrait ? 1 : 0) + (flipped ? 2 : 0); }

static const int BL_CH = 0;

static void flushCb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  const int32_t w = a->x2 - a->x1 + 1;
  const int32_t h = a->y2 - a->y1 + 1;
  gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t *)&px->full, w, h);
  lv_disp_flush_ready(drv);
}

static void touchCb(lv_indev_drv_t *, lv_indev_data_t *data) {
  uint16_t x, y;
  touching = touch.read(x, y);
  if (touching) {
    int32_t tx = x, ty = y;                       // panel coordinates (landscape)
    if (TOUCH_MIRROR_X) tx = LCD_W - 1 - tx;
    if (TOUCH_MIRROR_Y) ty = LCD_H - 1 - ty;
    // Map to screen coordinates for the NV3041A rotation in use (derived from its MADCTL settings)
    int32_t sx, sy;
    switch (rot) {
      case 1:  sx = ty;             sy = LCD_W - 1 - tx; break;
      case 2:  sx = LCD_W - 1 - tx; sy = LCD_H - 1 - ty; break;
      case 3:  sx = LCD_H - 1 - ty; sy = tx;             break;
      default: sx = tx;             sy = ty;
    }
    if (TOUCH_PORTRAIT_FIX && (rot & 1)) { sx = gfx->width() - 1 - sx; sy = gfx->height() - 1 - sy; }
    data->point.x = constrain(sx, 0, (int32_t)gfx->width() - 1);
    data->point.y = constrain(sy, 0, (int32_t)gfx->height() - 1);
    data->state = LV_INDEV_STATE_PRESSED;
    lastTouch = millis();
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void boardSetBacklight(uint8_t level) { ledcWrite(BL_CH, level); }
int  boardLightRaw() { return -1; }   // no light sensor on this board

void boardBegin() {
  flip = settings.flip;
  rot = rotationFor(settings.portrait, flip);

  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(LCD_BL, BL_CH);
  boardSetBacklight(0);

  bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
  gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, rot, true /* IPS */);
  if (!gfx->begin(LCD_SPEED_HZ)) Serial.println("Display init failed");
  gfx->fillScreen(0x0000);

  touch.begin(TP_SDA, TP_SCL, TP_INT, TP_RST);
  Serial.printf("Touch %s\n", touch.ok() ? "found" : "NOT found");

  lv_init();

  const size_t px = LCD_W * LV_BUF_LINES;
  lv_color_t *b1 = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_color_t *b2 = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!b1) b1 = (lv_color_t *)ps_malloc(px * sizeof(lv_color_t));
  lv_disp_draw_buf_init(&drawBuf, b1, b2, px);

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

void boardSetColours(uint8_t) {}   // IPS panel: not needed
bool boardColoursInverted() { return false; }

uint32_t boardLastTouchMs() { return lastTouch; }
bool     boardTouching()    { return touching; }
void     boardLoop()        { lv_timer_handler(); }

bool        boardCanCalibrate() { return false; }
bool        boardIsCalibrated() { return true; }
void        boardCalibrate()    {}
const char *boardPanelName()    { return "NV3041A"; }

#endif  // BOARD_JC4827
