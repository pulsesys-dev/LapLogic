// All screens (LVGL 8.3)
#pragma once
#include <Arduino.h>
#include "f1data.h"

void uiBegin();
void uiShowSplash(const char *line);          // logo + a status line
void uiShowSetup(const char *note);           // first-time setup with QR code
void uiShowMain();
void uiSetData(const F1Data *d);              // new data arrived: rebuild pages
const F1Data *uiData();                       // the data on screen (nullptr until loaded)
String uiDataSummary();                       // "Tue 27 Oct, 1:20 PM" or "not yet" - for status displays
String uiWeatherSummary();                    // "24°C, cloudy ... at Mexico City" or ""
void uiRebuild();                             // settings changed: redraw everything
void uiApplyTheme();
void uiPromptUpdate(const char *version, const char *notes);   // "Update available" - Install / Later                          // light/dark mode changed: rebuild every screen
void uiTick();                                // call every loop: clock, countdown, auto-rotate
