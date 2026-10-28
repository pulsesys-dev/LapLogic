// Team logos you supply yourself on the CYD's MicroSD card (see docs/using.md, "Team logos").
// They're copied into the board's flash at start-up, shrunk to badge size, and shown in place
// of the coloured team badges. Teams without a logo keep their coloured badge.
#pragma once
#include <Arduino.h>
#include <lvgl.h>

void logosBegin();       // at start-up, before the display: reads /logos on the SD card, if there is one
// The logo for a team (Jolpica constructorId and display name, e.g. "red_bull", "Red Bull"),
// or nullptr to use colours. Valid until logosRelease().
const lv_img_dsc_t *logoFor(const char *teamId, const char *teamName);
void logosRelease();     // free the logos held in memory (boards without PSRAM, between pages)
String logosStatus();    // "8 team logos" / "none - add PNGs to /logos on the SD card"
