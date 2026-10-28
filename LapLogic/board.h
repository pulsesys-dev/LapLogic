// Display, touch, backlight and LVGL glue.
// Implemented in board_jc4827.cpp (ESP32-S3) and board_cyd.cpp (ESP32);
// only the one matching the chip is compiled.
#pragma once
#include <Arduino.h>

void        boardBegin();                // reads settings.flip / panel / invert
void        boardSetFlip(bool flipped);
void        boardSetColours(uint8_t mode);      // 0 = automatic, 1 = normal, 2 = inverted
bool        boardColoursInverted();          // what is actually applied right now
void        boardSetBacklight(uint8_t level);   // 0..255
int         boardLightRaw();             // light sensor reading (0..4095), -1 if the board has none
uint32_t    boardLastTouchMs();
bool        boardTouching();             // finger currently down
void        boardLoop();                 // call often: runs LVGL

// Resistive touch needs calibrating; capacitive doesn't.
bool        boardCanCalibrate();
bool        boardIsCalibrated();
void        boardCalibrate();            // blocking: shows its own screen, then returns
const char *boardPanelName();            // e.g. "ILI9341 (auto)"
