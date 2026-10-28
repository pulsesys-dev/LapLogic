// The CYD's RGB LED on the back of the board, as a race-status light:
//   practice: blue   qualifying: purple   race/sprint: green
//   last 10 minutes before a race or sprint: slow amber pulse, then the start lights
//   (red, then out, then green) at the start time.
// Follows the published session times, so it can't show flags or safety cars (those need
// OpenF1's live data). Does nothing on boards without the LED.
#pragma once
#include <Arduino.h>
#include "f1data.h"

bool        ledSupported();
void        ledBegin();
void        ledLoop(const F1Data *d, uint8_t screenLevel);   // call every loop; screenLevel = backlight 0..255
void        ledTest();          // cycles through every colour (to check a housing)
const char *ledStatus();        // "green - Race is on" for the settings page
