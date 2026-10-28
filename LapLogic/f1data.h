// Formula 1 data model + fetching from the Jolpica (Ergast-compatible) API
#pragma once
#include <Arduino.h>
#include <time.h>

#define MAX_RACES 30
#define MAX_SESS   6
#define MAX_ROWS  26
#define MAX_TEAMS 14

enum SessionKind : uint8_t { S_FP1, S_FP2, S_FP3, S_SQ, S_SPRINT, S_QUALI, S_RACE };

struct Session {
  SessionKind kind;
  time_t start;      // UTC epoch
  uint16_t mins;     // typical duration, used to show LIVE
};

struct Race {
  uint8_t round;
  char name[40];       // "Mexico City Grand Prix"
  char circuit[48];    // "Autodromo Hermanos Rodriguez"
  char place[48];      // "Mexico City, Mexico"
  char locality[28];   // "Mexico City"   (for matching OpenF1 meetings)
  char country[24];    // "Mexico"
  float lat, lon;      // circuit location, for the weather
  uint8_t ns;
  Session s[MAX_SESS + 1];   // practice/quali/sprint sessions + the race, sorted by time
  bool sprint;
};

struct DriverRow { uint8_t pos; char code[4]; char last[24]; char teamId[24]; float pts; uint8_t wins; };
struct TeamRow   { uint8_t pos; char id[24]; char name[24]; float pts; uint8_t wins; };
struct ResultRow { uint8_t pos; char code[4]; char teamId[24]; char info[14]; float pts; };

// Weather at the next race's circuit (Open-Meteo, free, no key)
struct Weather {
  uint8_t round;       // race round this is for (0 = nothing yet)
  time_t  at;          // when it was fetched
  bool    imperial;    // units below: F + mph, otherwise C + km/h
  float   temp;        // air temperature now
  uint8_t humidity;    // %
  float   wind;
  float   rainMm;      // precipitation in the last hour
  uint8_t code;        // WMO weather code
  int8_t  rain[MAX_SESS + 1];   // chance of rain for each session of that weekend (%), -1 = not forecast yet
};

struct F1Data {
  uint16_t season;
  uint8_t  nRaces;
  Race     races[MAX_RACES];

  uint8_t   standingsRound;
  uint8_t   nDrivers;
  DriverRow drivers[MAX_ROWS];
  uint8_t   nTeams;
  TeamRow   teams[MAX_TEAMS];

  uint8_t   lastRound;
  char      lastRaceName[40];
  uint8_t   nResults;
  ResultRow results[MAX_ROWS];

  Weather weather;

  time_t calendarAt;   // when each block was last refreshed (0 = never)
  time_t standingsAt;
  time_t resultsAt;
};

bool fetchCalendar(F1Data &d, const char *season);   // "current" or "2027"
bool fetchDriverStandings(F1Data &d);
bool fetchTeamStandings(F1Data &d);
bool fetchLastResults(F1Data &d);
// Weather for race index ri. Leaves d.weather alone on failure.
bool fetchWeather(F1Data &d, int ri, bool imperial);
const char *weatherText(uint8_t wmoCode);   // "Partly cloudy"


// helpers
const char *sessionName(SessionKind k);   // "Practice 1"
const char *sessionShort(SessionKind k);  // "FP1"
time_t      raceEnd(const Race &r);
int         nextRaceIndex(const F1Data &d, time_t now);  // -1 = season over
void        foldAscii(const char *in, char *out, size_t n);
