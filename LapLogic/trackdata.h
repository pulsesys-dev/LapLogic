// Track map data from OpenF1 (https://openf1.org)
//   Free (no login):  circuit outline + a replay of the last race's final lap
//   Live (OpenF1 sponsor login, 4.3" board only): car positions during sessions
#pragma once
#include <Arduino.h>
#include "f1data.h"

#define TRACK_MAX_PTS       320
#define TRACK_MAX_CARS       24
#define TRACK_REPLAY_FRAMES 100      // one frame per second
#define TRACK_NO_POS     INT16_MIN

enum TrackMode : uint8_t { TM_NONE, TM_OUTLINE, TM_REPLAY, TM_LIVE };

struct TrackCar {
  uint8_t  num;
  char     code[4];
  uint32_t colour;      // 0xRRGGBB
};

struct TrackData {
  uint32_t  version;              // bumps when the page needs rebuilding
  TrackMode mode;
  char      circuit[32];          // "Mexico City"
  char      caption[64];          // "Final lap - Mexico City Grand Prix"
  char      note[96];             // extra line (e.g. how to get live positions)
  char      message[96];          // shown instead of a map when there's nothing to draw

  // All coordinates are OpenF1 x/y, stored as ((v - centre) / scale) to fit int16
  int32_t   cx, cy, scale;
  uint16_t  nPts;
  int16_t   pts[TRACK_MAX_PTS][2];

  uint8_t   nCars;
  TrackCar  cars[TRACK_MAX_CARS];

  uint8_t   nFrames;              // replay length in seconds; the frames themselves live in flash
  int32_t   replaySession;        // which session's replay is stored (0 = none)

  int16_t   live[TRACK_MAX_CARS][2];   // live positions (TRACK_NO_POS until seen)
  uint8_t   livePos[TRACK_MAX_CARS];   // race position, 0 = unknown
  uint32_t  liveVersion;               // bumps on every live update
  int32_t   liveSession;               // OpenF1 session_key being followed live (0 = none)
  // Race control, live (4.3" board with an OpenF1 login): track-wide flag state
  uint8_t   rcFlag;                    // RaceFlag below
  uint32_t  rcSectors;                 // sectors under yellow (bit n = sector n)
  bool      rcDouble;                  // a double yellow among them
  uint32_t  rcAt;                      // millis() of the last change (0 = none)
  float     trackTemp;                 // live track temperature, C (from OpenF1 weather, login only)
  uint32_t  trackTempAt;               // millis() when it arrived (0 = never)
};

extern TrackData        *track;
extern SemaphoreHandle_t trackLock;

void trackBegin();                   // allocate, mount flash storage, start the live task if supported
// Read one second of the replay (positions of every car) from flash. False if unavailable.
bool trackReadFrame(int frame, int16_t out[TRACK_MAX_CARS][2]);
void trackUpdate(const F1Data &d);   // network work; call from the background task every few minutes
bool trackLiveSupported();           // true on boards that can stream live positions
const char *trackLiveStatus();       // short status for the settings page
void trackLoginChanged();
enum RaceFlag : uint8_t { RF_NONE, RF_GREEN, RF_SC, RF_SC_ENDING, RF_VSC, RF_VSC_ENDING, RF_RED, RF_CHEQUERED };
// What race control is showing right now, e.g. "SAFETY CAR" in amber. False when there's nothing
// to show (no live session, no login, or the CYD). Green and chequered show for a short while only.
bool trackRaceControl(char *label, size_t n, uint32_t &colour);            // the OpenF1 login was edited: log in again, re-decide live/replay
