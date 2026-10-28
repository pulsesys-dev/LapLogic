// LVGL 8.3 configuration for LapLogic.
// Arduino IDE: copy this file into your Arduino "libraries" folder (next to the lvgl folder).
// PlatformIO: used automatically (see platformio.ini).
// Anything not set here falls back to LVGL's defaults.
#ifndef LV_CONF_H
#define LV_CONF_H

/* Bumped whenever this file changes. Arduino IDE users must copy the new file
   into their libraries folder; LapLogic prints a warning at start-up if they haven't. */
#define LAPLOGIC_LV_CONF_VERSION 4

#include <stdint.h>

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0

/* Find out which chip we're building for (Arduino/ESP-IDF provide sdkconfig.h) */
#if defined(__has_include)
  #if __has_include("sdkconfig.h")
    #include "sdkconfig.h"
  #endif
#endif

#if defined(CONFIG_IDF_TARGET_ESP32)
  /* CYD (no PSRAM): LVGL gets one block reserved at start-up and works inside it,
     so its many small allocations can't fragment the RAM that Wi-Fi and HTTPS need. */
  #define LV_MEM_CUSTOM          0
  #define LV_MEM_SIZE            (48U * 1024U)
  #define LV_MEM_POOL_INCLUDE    <stdlib.h>
  #define LV_MEM_POOL_ALLOC      malloc
#else
  /* Guition (8 MB PSRAM): use the system heap */
  #define LV_MEM_CUSTOM          1
  #define LV_MEM_CUSTOM_INCLUDE  <stdlib.h>
  #define LV_MEM_CUSTOM_ALLOC    malloc
  #define LV_MEM_CUSTOM_FREE     free
  #define LV_MEM_CUSTOM_REALLOC  realloc
#endif

#define LV_TICK_CUSTOM                 1
#define LV_TICK_CUSTOM_INCLUDE         "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR   (millis())

#define LV_DISP_DEF_REFR_PERIOD  20
#define LV_INDEV_DEF_READ_PERIOD 25
#define LV_DPI_DEF               130

#define LV_USE_LOG     0
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
/* If something goes badly wrong (e.g. out of memory), restart instead of freezing */
#define LV_ASSERT_HANDLER_INCLUDE   <stdlib.h>
#define LV_ASSERT_HANDLER           abort();

/* Fonts */
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_MONTSERRAT_48  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

/* Widgets used: label, obj, bar, btn, tileview, msgbox, qrcode */
#define LV_USE_QRCODE  1
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#endif
