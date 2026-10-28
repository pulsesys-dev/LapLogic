// Firmware updates over Wi-Fi from the project's GitHub releases.
// LapLogic checks the latest release once a day (and soon after starting). If it's newer than
// this firmware, it asks before installing; nothing is installed without you saying so.
#pragma once
#include <Arduino.h>

enum OtaState : uint8_t { OTA_IDLE, OTA_CHECKING, OTA_AVAILABLE, OTA_DOWNLOADING, OTA_FAILED, OTA_DONE };

void        otaLoop();                 // call from the background task: checks and installs
void        otaCheckNow();             // look for a new release straight away
void        otaInstall();              // install the release found (no-op unless one is available)
OtaState    otaState();
const char *otaVersion();              // "b1.7.1" or "v1.8.0" when one is available
String      otaNotes();                // the release notes (first few lines)
int         otaProgress();             // 0..100 while downloading
String      otaStatus();               // one line for the settings pages, e.g. "v1.6.1, up to date"
