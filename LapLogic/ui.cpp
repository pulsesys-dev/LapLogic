#include "ui.h"
#include "config.h"
#include "settings.h"
#include "net.h"
#include "board.h"
#include "trackdata.h"
#include "logos.h"
#include <lvgl.h>
#include <time.h>

// ------------------------------------------------------------------ palette

// Dark mode (default) and light mode. Edit freely.
struct Palette { uint32_t bg, surf, surf2, line, text, muted, dim, accent, fav, live, ok, track, rain; };
static const Palette DARK  = {0x0B0B10, 0x17171F, 0x23232D, 0x2A2A35, 0xF2F2F5, 0x8C8C9A, 0x55555F,
                              0xE10600, 0xFFB800, 0xFF2D2D, 0x2BD47D, 0x464655, 0x5AB4FF};
static const Palette LIGHT = {0xF3F3F6, 0xFFFFFF, 0xE4E4EA, 0xD3D3DB, 0x14141A, 0x5A5A66, 0x9C9CA8,
                              0xD10500, 0xB86E00, 0xE0001B, 0x0E9A52, 0xB4B4C0, 0x1A73D1};
static Palette pal = DARK;

#define C_BG     (pal.bg)
#define C_SURF   (pal.surf)
#define C_SURF2  (pal.surf2)
#define C_LINE   (pal.line)
#define C_TEXT   (pal.text)
#define C_MUTED  (pal.muted)
#define C_DIM    (pal.dim)
#define C_ACCENT (pal.accent)
#define C_FAV    (pal.fav)
#define C_LIVE   (pal.live)
#define C_OK     (pal.ok)
#define C_TRACK  (pal.track)
#define C_RAIN   (pal.rain)

// Teams by Jolpica constructorId: colour, short code for badges, and display name. Edit freely.
struct TeamInfo { const char *id; uint32_t c; const char *abbr; const char *name; };
static const TeamInfo TEAM_COLOURS[] = {
  {"red_bull", 0x3671C6, "RBR", "Red Bull"},   {"ferrari", 0xE8002D, "FER", "Ferrari"},
  {"mercedes", 0x27F4D2, "MER", "Mercedes"},   {"mclaren", 0xFF8000, "MCL", "McLaren"},
  {"aston_martin", 0x229971, "AMR", "Aston Martin"}, {"alpine", 0x0093CC, "ALP", "Alpine"},
  {"williams", 0x64C4FF, "WIL", "Williams"},   {"rb", 0x6692FF, "RB", "Racing Bulls"},
  {"racing_bulls", 0x6692FF, "RB", "Racing Bulls"}, {"sauber", 0x52E252, "SAU", "Sauber"},
  {"audi", 0xBB0A30, "AUD", "Audi"},           {"haas", 0xB6BABD, "HAA", "Haas"},
  {"cadillac", 0xC9A96E, "CAD", "Cadillac"},
};

static const TeamInfo *teamInfo(const char *id) {
  for (auto &t : TEAM_COLOURS) if (!strcmp(t.id, id)) return &t;
  return nullptr;
}

static uint32_t teamHex(const char *id) { const TeamInfo *t = teamInfo(id); return t ? t->c : 0x777780; }
static lv_color_t teamColour(const char *id) { return lv_color_hex(teamHex(id)); }

#define F12 (&lv_font_montserrat_12)
#define F14 (&lv_font_montserrat_14)
#define F16 (&lv_font_montserrat_16)
#define F20 (&lv_font_montserrat_20)
#define F28 (&lv_font_montserrat_28)
#define F48 (&lv_font_montserrat_48)

// PICK(4.3" value, 2.8" value) - chosen by board
#define PICK(big, small) (COMPACT ? (small) : (big))

// Screen size is decided at start-up (landscape or portrait)
static int  SW = LCD_W, SH = LCD_H;
static bool PORT = false;
static int  HDR_H = 30, BODY_H = 242;

// ------------------------------------------------------------------ state

enum { PG_NEXT, PG_MAP, PG_DRIVERS, PG_TEAMS, PG_RESULT, PG_CAL, PG_INFO, PG_COUNT };
static const char *PAGE_TITLES[PG_COUNT] = {"NEXT UP", "TRACK", "DRIVERS", "CONSTRUCTORS", "LAST RACE", "CALENDAR", "SETTINGS"};
// The pages in swipe order. The 2.8" CYD has no Track page: it can't hold the live map.
#define HAS_TRACK_PAGE (!BOARD_CYD)
static const int PAGE_ORDER[] = {PG_NEXT,
#if HAS_TRACK_PAGE
                                 PG_MAP,
#endif
                                 PG_DRIVERS, PG_TEAMS, PG_RESULT, PG_CAL, PG_INFO};
static const int NPAGES = sizeof(PAGE_ORDER) / sizeof(PAGE_ORDER[0]);
static int tileIndex(int p) { for (int i = 0; i < NPAGES; i++) if (PAGE_ORDER[i] == p) return i; return 0; }
static bool autoScrolls(int p) { return p != PG_CAL && p != PG_INFO; }   // auto scroll skips these

static const F1Data *D = nullptr;

static lv_obj_t *scrMain = nullptr, *scrSplash = nullptr, *scrSetup = nullptr;
static lv_obj_t *splashLine = nullptr, *setupNote = nullptr;
static lv_obj_t *tv = nullptr, *tiles[PG_COUNT];
static lv_obj_t *hdrTitle, *hdrClock, *hdrWifi, *dots[PG_COUNT];
static lv_obj_t *hdrLine = nullptr, *hdrFlag = nullptr;   // race control: coloured rule + flag pill
static int hdrTitleW = 0, hdrFlagRight = 0;
static char flagShown[24] = "";

static lv_obj_t *nxCount = nullptr;
static time_t    nxTarget = 0;
static bool      nxLive = false;


static int curPage = 0;
static uint32_t pageSince = 0;
static char subtitle[PG_COUNT][24];

// ------------------------------------------------------------------ helpers

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, uint32_t bg, lv_opa_t opa = LV_OPA_COVER, int r = 0) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  if (r) lv_obj_set_style_radius(o, r, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

static lv_obj_t *teamBar(lv_obj_t *p, int x, int y, int w, int h, const char *teamId, int r = 0) {
  lv_obj_t *o = box(p, x, y, w, h, 0, LV_OPA_COVER, r);
  lv_obj_set_style_bg_color(o, teamColour(teamId), 0);
  return o;
}


// Single-line label; with a width it truncates with "..."
static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, uint32_t col, int x, int y, const char *txt,
                       int w = 0, lv_text_align_t al = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  if (w > 0) {
    lv_obj_set_size(l, w, lv_font_get_line_height(f));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, al, 0);
  }
  lv_label_set_text(l, txt);
  lv_obj_set_pos(l, x, y);
  return l;
}

// Multi-line wrapping label
static lv_obj_t *para(lv_obj_t *p, const lv_font_t *f, uint32_t col, int x, int y, int w, const char *txt,
                      lv_text_align_t al = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(l, al, 0);
  lv_label_set_text(l, txt);
  lv_obj_set_pos(l, x, y);
  return l;
}

static const char *teamName(const char *id);   // below, once the data is in scope

// A pill in the team's colour with its short code ("MCL") or name ("McLaren").
// Dark text on light team colours, white on dark ones. w = 0 sizes it to the text.
static int teamBadge(lv_obj_t *p, int x, int y, int w, int h, const char *teamId, bool fullName = false) {
  char abbr[8];
  const TeamInfo *ti = teamInfo(teamId);
  // your own logo from the SD card, if there's one for this team
  if (const lv_img_dsc_t *lg = logoFor(teamId, teamName(teamId))) {
    lv_obj_t *img = lv_img_create(p);
    lv_img_set_src(img, lg);
    if (fullName) {   // logo, then the team's name
      lv_obj_set_pos(img, x, y + (h - (int)lg->header.h) / 2);
      const char *nm = teamName(teamId);
      if (!nm) return lg->header.w;
      lv_obj_t *l = label(p, F12, C_TEXT, x + lg->header.w + 6, y + (h - lv_font_get_line_height(F12)) / 2, nm);
      lv_obj_update_layout(l);
      return lg->header.w + 6 + lv_obj_get_width(l);
    }
    if (!w) w = lg->header.w;
    lv_obj_set_pos(img, x + (w - (int)lg->header.w) / 2, y + (h - (int)lg->header.h) / 2);
    return w;
  }
  const char *txt = fullName ? teamName(teamId) : ti ? ti->abbr : nullptr;
  if (!txt) {   // a team that's not in the table: first three letters of its id
    int k = 0;
    for (const char *c = teamId; *c && k < 3; c++) if (isalpha((unsigned char)*c)) abbr[k++] = toupper(*c);
    abbr[k] = 0;
    txt = abbr;
  }
  if (!w) {
    lv_point_t sz;
    lv_txt_get_size(&sz, txt, F12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    w = sz.x + 12;
  }
  const uint32_t c = teamHex(teamId);
  const int lum = (((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000;
  // one label with a shared style: badges appear on every row, so keep them light on memory
  static lv_style_t st;
  static bool stReady = false;
  if (!stReady) {
    lv_style_init(&st);
    lv_style_set_text_font(&st, F12);
    lv_style_set_text_align(&st, LV_TEXT_ALIGN_CENTER);
    lv_style_set_bg_opa(&st, LV_OPA_COVER);
    lv_style_set_radius(&st, 4);
    stReady = true;
  }
  lv_obj_t *b = lv_label_create(p);
  lv_obj_add_style(b, &st, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(c), 0);
  lv_obj_set_style_text_color(b, lv_color_hex(lum > 150 ? 0x101014 : 0xFFFFFF), 0);
  const int padT = (h - lv_font_get_line_height(F12)) / 2;
  if (padT > 0) lv_obj_set_style_pad_top(b, padT, 0);
  lv_label_set_long_mode(b, LV_LABEL_LONG_CLIP);
  lv_label_set_text(b, txt);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
  return w;
}

static int heightOf(lv_obj_t *o) { lv_obj_update_layout(o); return lv_obj_get_height(o); }

static void setColour(lv_obj_t *l, uint32_t c) { lv_obj_set_style_text_color(l, lv_color_hex(c), 0); }

// Largest of the given fonts that fits `sample` into maxW
static const lv_font_t *fitFont(const char *sample, int maxW, const lv_font_t *a, const lv_font_t *b, const lv_font_t *c) {
  const lv_font_t *fs[3] = {a, b, c};
  for (const lv_font_t *f : fs) {
    if (!f) continue;
    lv_point_t sz;
    lv_txt_get_size(&sz, sample, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x <= maxW) return f;
  }
  return c ? c : (b ? b : a);
}

static void fmtPts(float p, char *out, size_t n) {
  if (p == (int)p) snprintf(out, n, "%d", (int)p);
  else snprintf(out, n, "%.1f", p);
}

static void fmtClock(const struct tm &tm, char *out, size_t n) {
  if (settings.h24) snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
  else {
    int h = tm.tm_hour % 12; if (!h) h = 12;
    snprintf(out, n, "%d:%02d %s", h, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
  }
}

// "Sat 25 Oct, 7:00 AM"
static void fmtWhen(time_t t, char *out, size_t n) {
  struct tm tm; localtime_r(&t, &tm);
  char d[16], c[12];
  strftime(d, sizeof(d), "%a %d %b", &tm);
  fmtClock(tm, c, sizeof(c));
  snprintf(out, n, "%s, %s", d, c);
}

static void upper(char *s) { for (; *s; s++) *s = toupper(*s); }

static void centreMessage(lv_obj_t *t, const char *big, const char *small) {
  const int y = BODY_H / 2 - PICK(30, 26);
  para(t, PICK(F20, F16), C_TEXT, 10, y, SW - 20, big, LV_TEXT_ALIGN_CENTER);
  if (small) para(t, PICK(F14, F12), C_MUTED, 10, y + PICK(32, 26), SW - 20, small, LV_TEXT_ALIGN_CENTER);
}

static const char *teamName(const char *id) {
  if (const TeamInfo *t = teamInfo(id)) return t->name;    // short, familiar names
  if (D) for (int i = 0; i < D->nTeams; i++) if (!strcmp(D->teams[i].id, id)) return D->teams[i].name;
  return nullptr;
}

static const DriverRow *findDriver(const char *code) {
  if (!D || !code || !*code) return nullptr;
  for (int i = 0; i < D->nDrivers; i++) if (!strcasecmp(D->drivers[i].code, code)) return &D->drivers[i];
  return nullptr;
}

static bool isFav(const char *code) { return settings.fav.length() && settings.fav.equalsIgnoreCase(code); }

// ---- weather
#define DEG "\xC2\xB0"   // degree sign (in the built-in fonts)

// Weather for the race with this round (-1 = any), or nullptr if there's none or it's stale
static const Weather *weatherFor(int round) {
  if (!D || !D->weather.round || !D->weather.at) return nullptr;
  if (round >= 0 && D->weather.round != round) return nullptr;
  if (netTimeValid() && time(nullptr) - D->weather.at > 3 * 3600) return nullptr;
  return &D->weather;
}

// Live track temperature from OpenF1 (4.3" board with a login), e.g. "38°C"; nullptr if none recently
static const char *trackTempText() {
  static char b[16];
  if (!track) return nullptr;
  xSemaphoreTake(trackLock, portMAX_DELAY);
  const uint32_t at = track->trackTempAt;
  float v = track->trackTemp;
  xSemaphoreGive(trackLock);
  if (!at || millis() - at > 10UL * 60000UL) return nullptr;
  if (settings.units == 1) v = v * 9 / 5 + 32;
  snprintf(b, sizeof(b), "%.0f" DEG "%s", v, settings.units == 1 ? "F" : "C");
  return b;
}

static void fmtTemp(const Weather &w, char *out, size_t n) { snprintf(out, n, "%.0f" DEG "%s", w.temp, w.imperial ? "F" : "C"); }

// small blue drop + "40%" (rain chance); returns the width used
static int rainTag(lv_obj_t *p, int x, int y, int pct, int w = 0) {
  char b[8]; snprintf(b, sizeof(b), "%d%%", pct);
  lv_point_t sz;
  lv_txt_get_size(&sz, b, F12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int tw = 7 + sz.x;
  if (w) x += (w - tw) / 2;   // centre within w
  box(p, x, y + 5, 5, 5, C_RAIN, pct >= 30 ? LV_OPA_COVER : LV_OPA_50, LV_RADIUS_CIRCLE);
  label(p, F12, pct >= 30 ? C_RAIN : C_MUTED, x + 7, y, b);
  return tw;
}

// Logo: red bar + letter-spaced name, in a container sized to fit
static lv_obj_t *logo(lv_obj_t *p, const lv_font_t *f) {
  lv_obj_t *c = lv_obj_create(p);
  lv_obj_remove_style_all(c);
  lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
  const int h = lv_font_get_line_height(f);
  box(c, 0, h / 6, max(3, h / 4), h * 2 / 3, C_ACCENT, LV_OPA_COVER, 2);
  lv_obj_t *l = label(c, f, C_TEXT, max(3, h / 4) + max(5, h / 3), 0, "LAPLOGIC");
  lv_obj_set_style_text_letter_space(l, max(1, h / 9), 0);
  return c;
}

// ------------------------------------------------------------------ header & paging

static void updateHeader() {
  char t[40];
  snprintf(t, sizeof(t), "%s", PAGE_TITLES[curPage]);
  if (!COMPACT && !PORT && subtitle[curPage][0]) {
    // add the round etc. only if it fits beside the page dots
    char full[48];
    snprintf(full, sizeof(full), "%s  %s", PAGE_TITLES[curPage], subtitle[curPage]);
    lv_point_t sz;
    lv_txt_get_size(&sz, full, lv_obj_get_style_text_font(hdrTitle, 0), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x <= lv_obj_get_width(hdrTitle)) snprintf(t, sizeof(t), "%s", full);
  }
  lv_label_set_text(hdrTitle, t);
  for (int i = 0; i < NPAGES; i++)
    if (dots[i]) lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == tileIndex(curPage) ? C_ACCENT : C_DIM), 0);
}

// On boards without PSRAM only the visible page is kept in memory (UI_LAZY_PAGES);
// the others are built when you move to them and freed when you leave.
static void buildPage(int p);
static void mapDropped();
static void infoForget();
static bool pageBuilt(int p) { return tiles[p] && lv_obj_get_child_cnt(tiles[p]) > 0; }

static void dropPage(int p) {
  if (!tiles[p]) return;
  if (p == PG_MAP) mapDropped();
  lv_obj_clean(tiles[p]);
  if (p == PG_NEXT) nxCount = nullptr;
  if (p == PG_INFO) infoForget();
}

static void keepOnly(int p) {
  for (int i = 0; i < PG_COUNT; i++) if (i != p && pageBuilt(i)) dropPage(i);
}

static uint32_t mapStart = 0;   // when the final-lap replay started playing

static void onTileChanged(lv_event_t *) {
  lv_obj_t *act = lv_tileview_get_tile_act(tv);
  for (int i = 0; i < PG_COUNT; i++) if (tiles[i] && tiles[i] == act) curPage = i;
  pageSince = millis();
  if (curPage == PG_MAP) mapStart = millis();   // replay from the start each visit
  if (UI_LAZY_PAGES) {
    keepOnly(curPage);                             // free the old page first...
    if (!pageBuilt(curPage)) buildPage(curPage);   // ...then build the new one
  }
  updateHeader();
}

static void goPage(int p, bool anim = true) {
  if (UI_LAZY_PAGES) {
    // switch instantly: a slide would need both pages in memory at once
    keepOnly(-1);
    buildPage(p);
    anim = false;
  }
  lv_obj_set_tile_id(tv, tileIndex(p), 0, anim ? LV_ANIM_ON : LV_ANIM_OFF);
  curPage = p;
  pageSince = millis();
  if (p == PG_MAP) mapStart = millis();
  updateHeader();
}

// Swiping past the last page goes to the first, and back past the first to the last.
// At either end the page can't scroll that way, so LVGL reports a gesture instead.
static void onGesture(lv_event_t *e) {
  (void)e;
  const lv_obj_t *from = lv_indev_get_obj_act();   // where the finger went down
  if (from && (lv_obj_check_type(from, &lv_slider_class) || lv_obj_check_type(from, &lv_switch_class))) return;   // dragging a control
  const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
  const int i = tileIndex(curPage);
  int to = -1;
  if (dir == LV_DIR_LEFT && i == NPAGES - 1) to = PAGE_ORDER[0];              // finger moves left: next page
  else if (dir == LV_DIR_RIGHT && i == 0) to = PAGE_ORDER[NPAGES - 1];        // finger moves right: previous page
  if (to < 0) return;
  lv_indev_wait_release(lv_indev_get_act());   // the rest of this swipe mustn't move the new page
  goPage(to);
}

// ------------------------------------------------------------------ page: next session

static void buildNext() {
  lv_obj_t *t = tiles[PG_NEXT];
  lv_obj_clean(t);
  nxCount = nullptr;
  nxTarget = 0;
  subtitle[PG_NEXT][0] = 0;

  if (!netTimeValid()) { centreMessage(t, "Waiting for the network", "Setting the clock..."); return; }
  if (!D || !D->nRaces) { centreMessage(t, "Loading F1 data...", "This takes a few seconds"); return; }

  const time_t now = time(nullptr);
  const int ri = nextRaceIndex(*D, now);
  char buf[96];
  const int X = PICK(16, 10), W = SW - 2 * X;

  if (ri < 0) {   // season finished and next calendar not out yet
    int y = PICK(14, 8);
    label(t, PICK(F14, F12), C_ACCENT, X, y, "SEASON COMPLETE"); y += PICK(26, 20);
    snprintf(buf, sizeof(buf), "%u World Champions", D->season);
    label(t, PICK(F16, F14), C_MUTED, X, y, buf); y += PICK(34, 28);
    if (D->nDrivers) {
      const DriverRow &c = D->drivers[0];
      teamBar(t, X, y, PICK(6, 5), PICK(40, 28), c.teamId);
      snprintf(buf, sizeof(buf), "%s  %s", c.code, c.last);
      label(t, PORT ? PICK(F20, F16) : PICK(F28, F20), C_TEXT, X + 16, y + PICK(4, 3), buf, W - 16);
      y += PICK(56, 42);
    }
    if (D->nTeams) {
      const TeamRow &c = D->teams[0];
      teamBar(t, X, y, PICK(6, 5), PICK(30, 24), c.id);
      label(t, PICK(F20, F16), C_TEXT, X + 16, y + PICK(4, 3), c.name, W - 16);
      y += PICK(56, 42);
    }
    para(t, PICK(F14, F12), C_MUTED, X, y, W, "Next season's calendar will appear here once it's published.");
    return;
  }

  const Race &r = D->races[ri];
  int si = -1;
  bool live = false;
  for (int i = 0; i < r.ns; i++) {
    const Session &s = r.s[i];
    if (now >= s.start && now < s.start + s.mins * 60) { si = i; live = true; break; }
    if (s.start > now) { si = i; break; }
  }
  if (si < 0) si = r.ns - 1;
  const Session &s = r.s[si];

  snprintf(subtitle[PG_NEXT], sizeof(subtitle[PG_NEXT]), "R%u", r.round);

  // Round + place, weather now, sprint badge
  const Weather *wx = weatherFor(r.round);
  const char *where = wx ? r.locality : r.place;    // shorter when the weather shares the row
  if (COMPACT || PORT) snprintf(buf, sizeof(buf), "R%u  %s", r.round, where);
  else snprintf(buf, sizeof(buf), "%s", where);
  upper(buf);
  const int badgeW = PICK(92, 60);
  int rowW = r.sprint ? W - badgeW - 10 : W;
  if (wx) {
    char tmp[16], wb[48];
    fmtTemp(*wx, tmp, sizeof(tmp));
    const char *txt = weatherText(wx->code);
    snprintf(wb, sizeof(wb), "%s  %s", tmp, txt);
    lv_point_t sz;
    lv_txt_get_size(&sz, wb, F12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x > rowW / 2 - 10) {   // too long beside the place name: temperature only
      snprintf(wb, sizeof(wb), "%s", tmp);
      lv_txt_get_size(&sz, wb, F12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    }
    const int ww = min((int)sz.x + 2, rowW / 2);
    label(t, F12, C_TEXT, X + rowW - ww, PICK(8, 4), wb, ww, LV_TEXT_ALIGN_RIGHT);
    rowW -= ww + 8;
  }
  label(t, F12, C_MUTED, X, PICK(8, 4), buf, rowW);
  if (r.sprint) {
    lv_obj_t *b = box(t, SW - badgeW - PICK(16, 8), PICK(4, 2), badgeW, PICK(20, 17), C_ACCENT, LV_OPA_20, 4);
    lv_obj_set_style_border_color(b, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    label(b, F12, C_TEXT, 0, PICK(3, 1), "SPRINT", badgeW, LV_TEXT_ALIGN_CENTER);
  }
  const lv_font_t *nameFont = PORT ? fitFont(r.name, W, PICK(F28, F20), PICK(F20, F16), F14) : PICK(F28, F20);
  label(t, nameFont, C_TEXT, X - 2, PICK(24, 19), r.name, W + 2);

  // Status + countdown
  snprintf(buf, sizeof(buf), live ? "LIVE NOW  -  %s" : "%s STARTS IN", sessionName(s.kind));
  upper(buf);
  const int favW = PORT ? 0 : PICK(136, 108);           // landscape: driver card sits beside the countdown
  const int leftW = W - (favW ? favW + 8 : 0);
  label(t, PICK(F14, F12), live ? C_LIVE : C_MUTED, X, PICK(66, 46), buf, leftW);
  const lv_font_t *cf = fitFont("00d 00:00:00", leftW, PICK(F48, F28), F28, F20);
  nxCount = label(t, cf, live ? C_LIVE : C_TEXT, X - 4, PICK(84, 62), "--:--:--");
  nxLive = live;
  nxTarget = live ? s.start + s.mins * 60 : s.start;
  int y = PICK(84, 62) + lv_font_get_line_height(cf) + 2;

  char when[40];
  fmtWhen(s.start, when, sizeof(when));
  if (live) snprintf(buf, sizeof(buf), "Started %s", when);
  else snprintf(buf, sizeof(buf), "%s", when);
  label(t, PICK(F16, F12), C_MUTED, X, y, buf, leftW);
  y += PICK(26, 20);

  // Favourite driver card
  if (const DriverRow *f = findDriver(settings.fav.c_str())) {
    char pts[12]; fmtPts(f->pts, pts, sizeof(pts));
    if (!PORT) {
      const int ch = PICK(92, 76);
      lv_obj_t *card = box(t, SW - favW - PICK(14, 8), PICK(66, 44), favW, ch, C_SURF, LV_OPA_COVER, PICK(8, 6));
      teamBar(card, 0, 0, PICK(5, 4), ch, f->teamId);
      label(card, F12, C_FAV, PICK(14, 10), PICK(8, 4), "YOUR DRIVER");
      snprintf(buf, sizeof(buf), "%s  P%u", f->code, f->pos);
      label(card, PICK(F20, F16), C_TEXT, PICK(14, 10), PICK(26, 20), buf);
      teamBadge(card, PICK(14, 10), PICK(52, 40), 0, 16, f->teamId, true);
      if (COMPACT) snprintf(buf, sizeof(buf), "%s pts  %uW", pts, f->wins);
      else snprintf(buf, sizeof(buf), "%s pts  %u win%s", pts, f->wins, f->wins == 1 ? "" : "s");
      label(card, F12, C_MUTED, PICK(14, 10), PICK(72, 58), buf, favW - PICK(18, 14));
    } else {
      const int ch = PICK(44, 36);
      lv_obj_t *card = box(t, X, y, W, ch, C_SURF, LV_OPA_COVER, 6);
      teamBar(card, 0, 0, 4, ch, f->teamId);
      label(card, F12, C_FAV, 12, PICK(4, 3), "YOUR DRIVER");
      snprintf(buf, sizeof(buf), "%s  P%u", f->code, f->pos);
      lv_obj_t *nm = label(card, PICK(F16, F14), C_TEXT, 12, PICK(20, 17), buf);
      lv_obj_update_layout(nm);
      teamBadge(card, 12 + lv_obj_get_width(nm) + 10, PICK(21, 18), 0, 16, f->teamId, true);
      snprintf(buf, sizeof(buf), "%s pts\n%u win%s", pts, f->wins, f->wins == 1 ? "" : "s");
      lv_obj_t *l = para(card, F12, C_MUTED, W - 90, PICK(6, 3), 82, buf, LV_TEXT_ALIGN_RIGHT);
      (void)l;
      y += ch + PICK(10, 6);
    }
  }

  // Weekend sessions: a strip of chips in landscape, a list in portrait
  const int n = r.ns;
  if (!PORT) {
    const bool rainRow = wx && [&] { for (int i = 0; i < n; i++) if (wx->rain[i] >= 0) return true; return false; }();
    const int gap = PICK(6, 4), x0 = PICK(12, 8), yy = PICK(rainRow ? 162 : 166, 122), h = rainRow ? PICK(76, 80) : PICK(70, 68);
    const int w = (SW - 2 * x0 - (n - 1) * gap) / n;
    for (int i = 0; i < n; i++) {
      const Session &ss = r.s[i];
      const bool past = now >= ss.start + ss.mins * 60;
      const bool isLive = live && i == si;
      const bool isNext = !live && i == si;
      lv_obj_t *c = box(t, x0 + i * (w + gap), yy, w, h, isLive ? C_LIVE : C_SURF,
                        isLive ? LV_OPA_30 : (past ? LV_OPA_50 : LV_OPA_COVER), PICK(8, 6));
      if (isLive || isNext) {
        lv_obj_set_style_border_color(c, lv_color_hex(isLive ? C_LIVE : C_ACCENT), 0);
        lv_obj_set_style_border_width(c, 2, 0);
      }
      const uint32_t tc = past ? C_DIM : C_TEXT;
      label(c, PICK(F16, F14), isLive ? C_LIVE : tc, 0, PICK(8, 7), sessionShort(ss.kind), w, LV_TEXT_ALIGN_CENTER);
      struct tm tm; localtime_r(&ss.start, &tm);
      char d[16], wd[8], cl[12];
      strftime(wd, sizeof(wd), "%a", &tm);
      snprintf(d, sizeof(d), "%s %d", wd, tm.tm_mday);   // "Sat 31"
      fmtClock(tm, cl, sizeof(cl));
      if (rainRow) {
        label(c, F12, past ? C_DIM : C_MUTED, 0, PICK(27, 27), d, w, LV_TEXT_ALIGN_CENTER);
        label(c, F12, past ? C_DIM : C_MUTED, 0, PICK(42, 43), cl, w, LV_TEXT_ALIGN_CENTER);
        if (!past && wx->rain[i] >= 0) rainTag(c, 0, PICK(57, 60), wx->rain[i], w);
      } else {
        label(c, F12, past ? C_DIM : C_MUTED, 0, PICK(30, 27), d, w, LV_TEXT_ALIGN_CENTER);
        label(c, F12, past ? C_DIM : C_MUTED, 0, PICK(47, 44), cl, w, LV_TEXT_ALIGN_CENTER);
      }
    }
  } else {
    label(t, F12, C_MUTED, X, y, "THIS WEEKEND");
    y += PICK(20, 16);
    const int rowH = min(PICK(30, 24), (BODY_H - y - 4) / max(n, 1));
    for (int i = 0; i < n; i++) {
      const Session &ss = r.s[i];
      const bool past = now >= ss.start + ss.mins * 60;
      const bool isLive = live && i == si;
      const bool isNext = !live && i == si;
      lv_obj_t *row = box(t, X, y, W, rowH - 3, isLive ? C_LIVE : C_SURF,
                          isLive ? LV_OPA_30 : (past ? LV_OPA_50 : LV_OPA_COVER), 5);
      if (isLive || isNext) {
        lv_obj_set_style_border_color(row, lv_color_hex(isLive ? C_LIVE : C_ACCENT), 0);
        lv_obj_set_style_border_width(row, 2, 0);
      }
      const int ty = (rowH - 3 - 16) / 2;
      const uint32_t tc = past ? C_DIM : C_TEXT;
      label(row, F14, isLive ? C_LIVE : tc, 8, ty, sessionShort(ss.kind));
      if (wx && !past && wx->rain[i] >= 0) rainTag(row, 54, ty + 1, wx->rain[i]);
      struct tm tm; localtime_r(&ss.start, &tm);
      char d[16], cl[12], b[32];
      strftime(d, sizeof(d), (COMPACT && wx && !past && wx->rain[i] >= 0) ? "%a %d" : "%a %d %b", &tm);
      fmtClock(tm, cl, sizeof(cl));
      snprintf(b, sizeof(b), "%s   %s", d, cl);
      label(row, F12, past ? C_DIM : C_MUTED, 50, ty + 1, b, W - 58, LV_TEXT_ALIGN_RIGHT);
      y += rowH;
    }
  }
}

static void tickNext(time_t now) {
  if (!nxCount) return;
  long rem = (long)(nxTarget - now);
  if (rem <= 0) { buildNext(); updateHeader(); return; }   // session started/ended: refresh view
  char buf[24];
  const long d = rem / 86400, hh = (rem % 86400) / 3600, mm = (rem % 3600) / 60, ss = rem % 60;
  if (nxLive) snprintf(buf, sizeof(buf), "ends ~%02ld:%02ld", hh * 60 + mm, ss);
  else if (d > 0) snprintf(buf, sizeof(buf), "%ldd %02ld:%02ld:%02ld", d, hh, mm, ss);
  else snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld", hh, mm, ss);
  lv_label_set_text(nxCount, buf);
}

// ------------------------------------------------------------------ page: track map

static lv_point_t mapPts[TRACK_MAX_PTS];
static lv_obj_t  *mapDots[TRACK_MAX_CARS];
static lv_obj_t  *mapTags[TRACK_MAX_CARS];
static lv_timer_t *mapTimer = nullptr;
static uint32_t   mapBuiltVersion = 0, mapLiveSeen = 0;
static int        mapCars = 0, dotR = 4;
static TrackMode  mapMode = TM_NONE;
static struct { float c, s, k; int ox, oy; float minx, miny; } fit;

static void toScreen(int16_t x, int16_t y, int &sx, int &sy) {
  const float px = x, py = -(float)y;   // OpenF1 y points up; the screen's points down
  const float rx = px * fit.c - py * fit.s, ry = px * fit.s + py * fit.c;
  sx = fit.ox + (int)((rx - fit.minx) * fit.k);
  sy = fit.oy + (int)((ry - fit.miny) * fit.k);
}

// Rotate the circuit to whichever angle fills the rectangle best
static void computeFit(int rx, int ry, int rw, int rh) {
  float best = -1;
  for (int a = 0; a < 180; a += 5) {
    const float c = cosf(a * 0.0174533f), s = sinf(a * 0.0174533f);
    float minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (int i = 0; i < track->nPts; i += 2) {
      const float px = track->pts[i][0], py = -(float)track->pts[i][1];
      const float x = px * c - py * s, y = px * s + py * c;
      minx = min(minx, x); maxx = max(maxx, x); miny = min(miny, y); maxy = max(maxy, y);
    }
    const float k = min(rw / max(maxx - minx, 1.0f), rh / max(maxy - miny, 1.0f));
    if (k > best) {
      best = k;
      fit.c = c; fit.s = s; fit.k = k; fit.minx = minx; fit.miny = miny;
      fit.ox = rx + (int)((rw - (maxx - minx) * k) / 2);
      fit.oy = ry + (int)((rh - (maxy - miny) * k) / 2);
    }
  }
}

static void placeDot(int i, int16_t x, int16_t y) {
  if (!mapDots[i]) return;
  if (x == TRACK_NO_POS) { lv_obj_add_flag(mapDots[i], LV_OBJ_FLAG_HIDDEN); if (mapTags[i]) lv_obj_add_flag(mapTags[i], LV_OBJ_FLAG_HIDDEN); return; }
  int sx, sy;
  toScreen(x, y, sx, sy);
  lv_obj_clear_flag(mapDots[i], LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(mapDots[i], sx - dotR, sy - dotR);
  if (mapTags[i]) { lv_obj_clear_flag(mapTags[i], LV_OBJ_FLAG_HIDDEN); lv_obj_set_pos(mapTags[i], sx + dotR + 2, sy - 8); }
}

static void mapTick(lv_timer_t *) {
  if (curPage != PG_MAP || !track || !mapCars) return;
  xSemaphoreTake(trackLock, portMAX_DELAY);
  if (mapMode == TM_REPLAY && track->nFrames > 1) {
    // play the lap at real speed, pause 3 s on the last frame, then loop
    const int nf = track->nFrames;
    xSemaphoreGive(trackLock);
    const uint32_t period = (nf - 1) * 1000 + 3000;
    const uint32_t t = (millis() - mapStart) % period;
    const int f = min((int)(t / 1000), nf - 2);
    const float u = t >= (uint32_t)(nf - 1) * 1000 ? 1.0f : (t % 1000) / 1000.0f;
    // the replay lives in flash: read the two seconds we're between, once per second
    static int16_t fa[TRACK_MAX_CARS][2], fb[TRACK_MAX_CARS][2];
    static int loaded = -1;
    static uint32_t loadedVersion = 0;
    if (f != loaded || loadedVersion != mapBuiltVersion) {
      if (!trackReadFrame(f, fa) || !trackReadFrame(f + 1, fb)) return;
      loaded = f;
      loadedVersion = mapBuiltVersion;
    }
    for (int i = 0; i < mapCars; i++) {
      const int16_t *a = fa[i], *b = fb[i];
      if (a[0] == TRACK_NO_POS || b[0] == TRACK_NO_POS) { placeDot(i, b[0], b[1]); continue; }
      placeDot(i, (int16_t)(a[0] + (b[0] - a[0]) * u), (int16_t)(a[1] + (b[1] - a[1]) * u));
    }
    return;
  } else if (mapMode == TM_LIVE && track->liveVersion != mapLiveSeen) {
    mapLiveSeen = track->liveVersion;
    for (int i = 0; i < mapCars; i++) placeDot(i, track->live[i][0], track->live[i][1]);
  }
  xSemaphoreGive(trackLock);
}

static void mapDropped() {
  if (mapTimer) { lv_timer_del(mapTimer); mapTimer = nullptr; }
  for (int i = 0; i < TRACK_MAX_CARS; i++) mapDots[i] = mapTags[i] = nullptr;
  mapCars = 0;
}

static void buildMap() {
  lv_obj_t *t = tiles[PG_MAP];
  if (!t) return;
  mapDropped();
  lv_obj_clean(t);
  subtitle[PG_MAP][0] = 0;
  if (!track) { centreMessage(t, "Track map unavailable", nullptr); return; }

  xSemaphoreTake(trackLock, portMAX_DELAY);
  mapBuiltVersion = track->version;
  mapMode = track->mode;
  const bool hasMap = track->nPts > 1 && track->mode != TM_NONE;
  char caption[64], note[96], message[96], circuit[32];
  strncpy(caption, track->caption, sizeof(caption));
  strncpy(note, track->note, sizeof(note));
  strncpy(message, track->message, sizeof(message));
  strncpy(circuit, track->circuit, sizeof(circuit));
  xSemaphoreGive(trackLock);

  if (!hasMap) {
    centreMessage(t, caption[0] ? caption : "Track map", message[0] ? message : note);
    return;
  }
  if (mapMode == TM_LIVE) snprintf(subtitle[PG_MAP], sizeof(subtitle[PG_MAP]), "LIVE");

  // Layout: map + side panel (wide 4.3"), map + text below (portrait), map only (2.8" landscape)
  const bool side = !PORT && !COMPACT;
  int mx, my, mw, mh, tx, ty, tw;
  if (side) { mx = 10; my = 8; mw = SW * 6 / 10 - 14; mh = BODY_H - 16; tx = SW * 6 / 10 + 4; ty = 8; tw = SW - tx - 10; }
  else if (PORT) { mx = 8; my = PICK(30, 26); mw = SW - 16; mh = min(SW - 16, BODY_H * 6 / 10); tx = 10; ty = my + mh + 8; tw = SW - 20; }
  else { mx = 8; my = 22; mw = SW - 16; mh = BODY_H - 44; tx = 10; ty = BODY_H - 18; tw = SW - 20; }

  // Conditions at the circuit during its race weekend
  const Weather *wx = nullptr;
  int wxRain = -1;
  if (D && D->nRaces && netTimeValid()) {
    const time_t now = time(nullptr);
    const int ri = nextRaceIndex(*D, now);
    if (ri >= 0 && D->races[ri].ns && now >= D->races[ri].s[0].start - 2 * 86400) {
      wx = weatherFor(D->races[ri].round);
      if (wx) for (int i = 0; i < D->races[ri].ns; i++)
        if (D->races[ri].s[i].start + D->races[ri].s[i].mins * 60 > now) { wxRain = wx->rain[i]; break; }
    }
  }
  const char *trackT = wx ? trackTempText() : nullptr;
  char wxA[48] = "", wxB[64] = "";
  if (wx) {
    char tmp[16]; fmtTemp(*wx, tmp, sizeof(tmp));
    if (trackT) snprintf(wxA, sizeof(wxA), "Air %s   Track %s", tmp, trackT);
    else snprintf(wxA, sizeof(wxA), "Air %s   %s", tmp, weatherText(wx->code));
    int k = 0;
    if (trackT && wxRain < 0) k += snprintf(wxB + k, sizeof(wxB) - k, "%s  ", weatherText(wx->code));
    if (wxRain >= 0) k += snprintf(wxB + k, sizeof(wxB) - k, "Rain %d%%  ", wxRain);
    snprintf(wxB + k, sizeof(wxB) - k, "Wind %.0f %s", wx->wind, wx->imperial ? "mph" : "km/h");
  }

  // caption
  if (side) {
    lv_obj_t *c = para(t, F16, C_TEXT, tx, ty, tw, caption);
    ty += heightOf(c) + 6;
    if (mapMode == TM_LIVE) { label(t, F12, C_LIVE, tx, ty, "LIVE POSITIONS"); ty += 20; }
  } else if (wx && !PORT) {
    // 2.8" landscape: weather shares the caption row
    char b[40], tmp[16]; fmtTemp(*wx, tmp, sizeof(tmp));
    int k2 = snprintf(b, sizeof(b), "%s %s", trackT ? "Track" : "Air", trackT ? trackT : tmp);
    if (wxRain >= 0) snprintf(b + k2, sizeof(b) - k2, "  Rain %d%%", wxRain);
    lv_point_t sz;
    lv_txt_get_size(&sz, b, F12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    label(t, F12, C_TEXT, SW - 10 - sz.x - 2, 4, b, sz.x + 2, LV_TEXT_ALIGN_RIGHT);
    label(t, F12, mapMode == TM_LIVE ? C_LIVE : C_MUTED, 10, 4, caption, SW - 20 - sz.x - 10);
  } else {
    label(t, F12, mapMode == TM_LIVE ? C_LIVE : C_MUTED, 10, 4, caption, SW - 20);
  }

  // circuit outline
  xSemaphoreTake(trackLock, portMAX_DELAY);
  computeFit(mx, my, mw, mh);
  const int n = track->nPts;
  for (int i = 0; i < n; i++) {
    int sx, sy;
    toScreen(track->pts[i][0], track->pts[i][1], sx, sy);
    mapPts[i].x = sx; mapPts[i].y = sy;
  }
  mapCars = (mapMode == TM_REPLAY || mapMode == TM_LIVE) ? track->nCars : 0;
  TrackCar cars[TRACK_MAX_CARS];
  memcpy(cars, track->cars, sizeof(TrackCar) * mapCars);
  uint8_t pos[TRACK_MAX_CARS];
  memcpy(pos, track->livePos, sizeof(pos));
  xSemaphoreGive(trackLock);

  lv_obj_t *line = lv_line_create(t);
  lv_line_set_points(line, mapPts, n);
  lv_obj_set_style_line_width(line, PICK(5, 4), 0);
  lv_obj_set_style_line_color(line, lv_color_hex(C_TRACK), 0);
  lv_obj_set_style_line_rounded(line, true, 0);
  lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);

  // cars: coloured dots; your driver (and the leader, live) get a name tag
  dotR = PICK(5, 4);
  for (int i = 0; i < mapCars; i++) {
    lv_obj_t *d = box(t, 0, 0, dotR * 2, dotR * 2, cars[i].colour, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_color(d, lv_color_hex(isFav(cars[i].code) ? C_TEXT : C_BG), 0);
    lv_obj_set_style_border_width(d, isFav(cars[i].code) ? 2 : 1, 0);
    lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    mapDots[i] = d;
    if (isFav(cars[i].code) || (mapMode == TM_LIVE && pos[i] == 1)) {
      mapTags[i] = label(t, F12, isFav(cars[i].code) ? C_FAV : C_TEXT, 0, 0, cars[i].code);
      lv_obj_add_flag(mapTags[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Running order: live positions, or the race result under a final-lap replay.
  // Shown beside the map on the wide 4.3" screen and under it in 4.3" portrait.
  struct Entry { const char *code; uint32_t colour; const char *teamId; };
  Entry list[10];
  int k = 0;
  const char *listTitle = nullptr;
  if (mapMode == TM_LIVE) {
    for (int p = 1; p <= TRACK_MAX_CARS && k < 10; p++)
      for (int i = 0; i < mapCars && k < 10; i++) if (pos[i] == p) list[k++] = {cars[i].code, cars[i].colour, nullptr};
    listTitle = "RUNNING ORDER";
  } else if (mapMode == TM_REPLAY && D && D->nResults && !strstr(caption, "(Sprint)") && strstr(caption, D->lastRaceName)) {
    for (int j = 0; j < min(10, (int)D->nResults); j++) list[k++] = {D->results[j].code, 0, D->results[j].teamId};
    listTitle = "RESULT";
  }
  // the conditions block sits at the bottom of the text area; keep the list above it
  const int wxTop = !wx ? BODY_H : side ? BODY_H - 56 : PORT ? BODY_H - PICK(40, 34) : BODY_H;
  const bool showList = k && (side || (PORT && !COMPACT));
  if (showList) {
    const int cols = side ? 1 : 2;
    const int fit = max(0, (wxTop - 8 - (ty + 18)) / 19);
    k = min(k, fit * cols);
  }
  if (showList && k) {
    const int cols = side ? 1 : 2, per = (k + cols - 1) / cols, colW = tw / cols;
    label(t, F12, C_MUTED, tx, ty, listTitle); ty += 18;
    for (int j = 0; j < k; j++) {
      const int lx = tx + (j / per) * colW, ly = ty + (j % per) * 19;
      char b[8]; snprintf(b, sizeof(b), "%d", j + 1);
      label(t, F14, C_MUTED, lx, ly, b, 20, LV_TEXT_ALIGN_RIGHT);
      if (list[j].teamId) teamBar(t, lx + 26, ly + 3, 4, 13, list[j].teamId);
      else box(t, lx + 26, ly + 3, 4, 13, list[j].colour);
      label(t, F14, isFav(list[j].code) ? C_FAV : C_TEXT, lx + 36, ly, list[j].code);
    }
    ty += per * 19 + 6;
  }
  // conditions: bottom of the side panel, or the bottom line in portrait
  int noteLimit = BODY_H;
  if (wx && side) {
    const int wy = wxTop;
    box(t, tx, wy - 6, tw, 1, C_LINE);
    label(t, F12, C_MUTED, tx, wy, "TRACK CONDITIONS");
    label(t, F14, C_TEXT, tx, wy + 16, wxA, tw);
    label(t, F12, wxRain >= 30 ? C_RAIN : C_MUTED, tx, wy + 34, wxB, tw);
    noteLimit = wy - 8;
  } else if (wx && PORT) {
    const int wy = wxTop;
    label(t, PICK(F14, F12), C_TEXT, tx, wy, wxA, tw);
    label(t, F12, wxRain >= 30 ? C_RAIN : C_MUTED, tx, wy + PICK(18, 15), wxB, tw);
    noteLimit = wy - 4;
  }
  if (note[0]) {
    lv_obj_t *np = nullptr;
    if (side) { if (ty < noteLimit - 30) np = para(t, F12, C_MUTED, tx, ty, tw, note); }
    else if (PORT) { if (ty < noteLimit - 16) np = para(t, PICK(F14, F12), C_MUTED, tx, ty, tw, note); }
    if (np && ty + heightOf(np) > noteLimit) lv_obj_del(np);   // no room for all of it
    else label(t, F12, C_DIM, tx, ty, note, tw);
  }

  mapStart = millis();
  mapLiveSeen = 0;
  if (mapCars) {
    mapTimer = lv_timer_create(mapTick, 100, nullptr);
    mapTick(nullptr);
  }
}

// ------------------------------------------------------------------ standings-style rows

// Your logo for a team (from the SD card), scaled down to fit slotW x slotH if needed.
// Returns the width it takes, or 0 if there's no logo for this team.
static int teamMark(lv_obj_t *p, int x, int y, int slotW, int slotH, const char *teamId) {
  const lv_img_dsc_t *lg = logoFor(teamId, teamName(teamId));
  if (!lg) return 0;
  const int lw = lg->header.w, lh = lg->header.h;
  int zoom = 256;
  if (lw > slotW || lh > slotH) zoom = min(256 * slotW / lw, 256 * slotH / lh);
  const int dw = max(1, lw * zoom / 256), dh = max(1, lh * zoom / 256);
  lv_obj_t *img = lv_img_create(p);
  lv_img_set_src(img, lg);
  if (zoom != 256) {
    lv_img_set_pivot(img, 0, 0);
    lv_img_set_zoom(img, zoom);
    lv_img_set_antialias(img, true);
  }
  lv_obj_set_pos(img, x, y + (slotH - dh) / 2);
  return dw;
}

// One row in a list: position, team bar, code, then an optional detail
// (surname / time) and a right-aligned value (points).
static void listRow(lv_obj_t *t, int x, int y, int w, int rowH, int index, uint8_t pos, const char *code,
                    const char *teamId, const char *detail, const char *value, uint32_t valueColour, bool fav,
                    const lv_font_t *valueFont = F14, bool alwaysBadge = false) {
  lv_obj_t *rb = box(t, x, y, w, rowH - 1, fav ? C_FAV : C_SURF, fav ? LV_OPA_20 : (index % 2 ? LV_OPA_0 : LV_OPA_60), 4);
  const lv_font_t *fb = F14;
  const int ty = (rowH - 1 - lv_font_get_line_height(fb)) / 2;
  char b[8];
  snprintf(b, sizeof(b), "%u", pos);
  label(rb, fb, pos <= 3 ? C_TEXT : C_MUTED, 0, ty, b, PICK(22, 18), LV_TEXT_ALIGN_RIGHT);
  const int valW = PICK(52, 40);
  int dx = PICK(80, 66);
  // team badge, unless it would squeeze the detail (e.g. race times) too much; otherwise a colour bar
  const int bw = PICK(38, 32), bh = min(rowH - 5, 16);
  // decided for the worst case (a value shown), so every row of a list looks the same
  const int dwWith = w - (dx + bw + 6) - (valW + 6);
  const bool roomy = dx - PICK(8, 2) + bw + 4 <= w - valW - 4;   // badge clear of the value column
  const bool badge = roomy && (alwaysBadge || !(detail && *detail) || dwWith >= 56);
  int codeX = badge ? PICK(30, 24) : PICK(38, 30);
  if (!badge) {
    // narrow rows: a small logo in place of the colour bar, if you've added one
    const int mw = teamMark(rb, PICK(26, 20), 3, PICK(16, 12), rowH - 7, teamId);
    if (mw) { codeX = PICK(26, 20) + mw + 4; dx = max(dx, codeX + PICK(40, 36)); }
    else teamBar(rb, PICK(28, 22), 3, PICK(4, 3), rowH - 7, teamId);
  }
  label(rb, fb, fav ? C_FAV : C_TEXT, codeX, ty, code);
  if (badge) {
    teamBadge(rb, dx - PICK(8, 2), (rowH - 1 - bh) / 2, bw, bh, teamId);
    dx += bw + PICK(-2, 2);
  }
  if (detail && *detail) {
    const int dw = w - dx - (value ? valW + 6 : 4);
    if (dw >= (badge ? 64 : 24)) label(rb, F12, C_MUTED, dx, ty + 1, detail, dw);
  }
  if (value) {
    lv_point_t sz;   // a long value (e.g. "346.5") in a narrow column drops to the small font
    lv_txt_get_size(&sz, value, valueFont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_font_t *vf = (sz.x > valW - 2 || codeX + 36 > w - sz.x - 6) ? F12 : valueFont;
    label(rb, vf, valueColour, w - valW - 4, ty + (vf == F12 ? 1 : 0), value, valW, LV_TEXT_ALIGN_RIGHT);
  }
}

// Columns for a list of n rows in the space available
static void listGeometry(int n, int top, int maxRowH, int &cols, int &per, int &rowH, int &colW) {
  const int avail = BODY_H - top - 4;
  cols = (n * 15 <= avail && (PORT || n <= 11)) ? 1 : 2;      // one column if rows stay readable
  if (!PORT && n > 11) cols = 2;
  per = (n + cols - 1) / cols;
  rowH = min(maxRowH, avail / max(per, 1));
  if (cols == 1 && rowH < 15) { cols = 2; per = (n + 1) / 2; rowH = min(maxRowH, avail / per); }
  colW = (SW - PICK(24, 16) - (cols - 1) * PICK(8, 4)) / cols;
}

// ------------------------------------------------------------------ page: drivers

static void buildDrivers() {
  lv_obj_t *t = tiles[PG_DRIVERS];
  lv_obj_clean(t);
  subtitle[PG_DRIVERS][0] = 0;
  if (!D || !D->nDrivers) { centreMessage(t, "No standings yet", "They appear after the first race"); return; }
  snprintf(subtitle[PG_DRIVERS], sizeof(subtitle[PG_DRIVERS]), "R%u", D->standingsRound);

  const int n = D->nDrivers;
  int cols, per, rowH, colW;
  listGeometry(n, 3, PICK(21, 19), cols, per, rowH, colW);
  for (int i = 0; i < n; i++) {
    const DriverRow &r = D->drivers[i];
    char pts[12];
    fmtPts(r.pts, pts, sizeof(pts));
    const bool fav = isFav(r.code);
    listRow(t, PICK(8, 6) + (i / per) * (colW + PICK(8, 4)), 3 + (i % per) * rowH, colW, rowH, i % per,
            r.pos, r.code, r.teamId, colW >= 200 ? r.last : nullptr, pts, fav ? C_FAV : C_TEXT, fav, F14, true);
  }
}

// ------------------------------------------------------------------ page: constructors

static void buildTeams() {
  lv_obj_t *t = tiles[PG_TEAMS];
  lv_obj_clean(t);
  subtitle[PG_TEAMS][0] = 0;
  if (!D || !D->nTeams) { centreMessage(t, "No standings yet", "They appear after the first race"); return; }
  snprintf(subtitle[PG_TEAMS], sizeof(subtitle[PG_TEAMS]), "R%u", D->standingsRound);

  const int n = D->nTeams;
  const int x = PICK(8, 6), w = SW - 2 * x;
  float maxPts = 1;
  for (int i = 0; i < n; i++) maxPts = max(maxPts, D->teams[i].pts);
  const int ptsW = PICK(58, 42);

  if (!PORT) {
    const int rowH = min(PICK(21, 19), (BODY_H - 6) / n);
    const int nameX = PICK(34, 26), nameW = PICK(140, 104);
    const int barX = nameX + nameW + 8, barMax = w - barX - ptsW - 10;
    for (int i = 0; i < n; i++) {
      const TeamRow &r = D->teams[i];
      lv_obj_t *rb = box(t, x, 3 + i * rowH, w, rowH - 1, C_SURF, i % 2 ? LV_OPA_0 : LV_OPA_60, 4);
      const int ty = (rowH - 1 - 16) / 2;
      char b[12];
      snprintf(b, sizeof(b), "%u", r.pos);
      label(rb, F14, r.pos <= 3 ? C_TEXT : C_MUTED, 0, ty, b, PICK(22, 18), LV_TEXT_ALIGN_RIGHT);
      const int mw = teamMark(rb, nameX - 4, 2, PICK(30, 24), rowH - 5, r.id);
      const int nx = mw ? nameX - 4 + mw + 6 : nameX;
      label(rb, PICK(F14, F12), C_TEXT, nx, PICK(ty, ty + 1), r.name, nameW - (nx - nameX));
      teamBar(rb, barX, 4, max((int)(barMax * r.pts / maxPts), 3), rowH - 9, r.id, 3);
      fmtPts(r.pts, b, sizeof(b));
      label(rb, F14, C_TEXT, w - ptsW - 6, ty, b, ptsW, LV_TEXT_ALIGN_RIGHT);
    }
  } else {
    // portrait: name and points on one line, the bar underneath
    const int rowH = min(PICK(38, 26), (BODY_H - 6) / n);
    for (int i = 0; i < n; i++) {
      const TeamRow &r = D->teams[i];
      lv_obj_t *rb = box(t, x, 3 + i * rowH, w, rowH - 2, C_SURF, i % 2 ? LV_OPA_0 : LV_OPA_60, 4);
      char b[12];
      snprintf(b, sizeof(b), "%u", r.pos);
      const lv_font_t *f = PICK(F14, F12);
      label(rb, f, r.pos <= 3 ? C_TEXT : C_MUTED, 0, 2, b, PICK(22, 18), LV_TEXT_ALIGN_RIGHT);
      const int mw = teamMark(rb, PICK(30, 24), 2, PICK(30, 24), lv_font_get_line_height(f), r.id);
      const int nx = PICK(30, 24) + (mw ? mw + 6 : 0);
      label(rb, f, C_TEXT, nx, 2, r.name, w - ptsW - 10 - nx);
      fmtPts(r.pts, b, sizeof(b));
      label(rb, f, C_TEXT, w - ptsW - 6, 2, b, ptsW, LV_TEXT_ALIGN_RIGHT);
      const int barW = w - PICK(30, 24) - 8;
      teamBar(rb, PICK(30, 24), rowH - PICK(10, 7), max((int)(barW * r.pts / maxPts), 3), PICK(5, 3), r.id, 2);
    }
  }
}

// ------------------------------------------------------------------ page: last race

static void buildResult() {
  lv_obj_t *t = tiles[PG_RESULT];
  lv_obj_clean(t);
  subtitle[PG_RESULT][0] = 0;
  if (!D || !D->nResults) { centreMessage(t, "No results yet", "The first race of the season hasn't run"); return; }
  snprintf(subtitle[PG_RESULT], sizeof(subtitle[PG_RESULT]), "R%u", D->lastRound);

  char b[48];
  if (COMPACT || PORT) snprintf(b, sizeof(b), "R%u  %s", D->lastRound, D->lastRaceName);
  else snprintf(b, sizeof(b), "%s", D->lastRaceName);
  upper(b);
  label(t, PICK(F14, F12), C_ACCENT, PICK(12, 8), PICK(4, 2), b, SW - PICK(24, 16));

  const int top = PICK(24, 18);
  const int n = D->nResults;
  int cols, per, rowH, colW;
  listGeometry(n, top, PICK(20, 18), cols, per, rowH, colW);
  lv_obj_t *area = t;
  int y0 = top;
  if (PORT) {
    // portrait: one column with every finishing time, in a list you swipe up and down
    cols = 1; per = n; rowH = PICK(24, 21); colW = SW - PICK(24, 16);
    area = lv_obj_create(t);
    lv_obj_remove_style_all(area);
    lv_obj_set_pos(area, 0, top);
    lv_obj_set_size(area, SW, BODY_H - top);
    lv_obj_set_style_pad_bottom(area, 6, 0);
    lv_obj_set_scroll_dir(area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(area, LV_SCROLLBAR_MODE_ACTIVE);
    y0 = 0;
  }
  for (int i = 0; i < n; i++) {
    const ResultRow &r = D->results[i];
    char p[12] = "";
    if (colW >= 200 && r.pts > 0) { char q[10]; fmtPts(r.pts, q, sizeof(q)); snprintf(p, sizeof(p), "+%s", q); }
    const bool fav = isFav(r.code);
    listRow(area, PICK(8, 6) + (i / per) * (colW + PICK(8, 4)), y0 + (i % per) * rowH, colW, rowH, i % per,
            r.pos, r.code, r.teamId, r.info, p[0] ? p : nullptr, C_OK, fav, F12);
  }
}

// ------------------------------------------------------------------ page: calendar

static void buildCalendar() {
  lv_obj_t *t = tiles[PG_CAL];
  lv_obj_clean(t);
  subtitle[PG_CAL][0] = 0;
  if (!D || !D->nRaces) { centreMessage(t, "Loading calendar...", nullptr); return; }
  snprintf(subtitle[PG_CAL], sizeof(subtitle[PG_CAL]), "%u", D->season);

  lv_obj_t *list = lv_obj_create(t);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, SW, BODY_H);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(list, PICK(6, 4), 0);
  lv_obj_set_style_pad_row(list, 2, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);

  const int rowW = SW - PICK(12, 8), rowH = PICK(26, 22);
  const int dateW = PORT ? PICK(60, 50) : PICK(100, 74), dateX = rowW - dateW - 8;
  const bool showSprint = rowW >= 300;
  const int sprintX = showSprint ? dateX - PICK(54, 30) : dateX;
  const int nameX = PICK(48, 34), nameW = sprintX - nameX - 6;

  const time_t now = time(nullptr);
  const int next = nextRaceIndex(*D, now);
  lv_obj_t *nextRow = nullptr;
  for (int i = 0; i < D->nRaces; i++) {
    const Race &r = D->races[i];
    const bool past = next < 0 || i < next;
    const bool isNext = i == next;
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, rowW, rowH);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(isNext ? C_ACCENT : C_SURF), 0);
    lv_obj_set_style_bg_opa(row, isNext ? LV_OPA_30 : (i % 2 ? LV_OPA_0 : LV_OPA_60), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    const uint32_t tc = past ? C_DIM : C_TEXT;
    const int ty = (rowH - 16) / 2;
    char b[48];
    snprintf(b, sizeof(b), "R%u", r.round);
    label(row, PICK(F14, F12), isNext ? C_TEXT : (past ? C_DIM : C_MUTED), PICK(8, 6), ty + PICK(0, 1), b);
    label(row, F14, tc, nameX, ty, r.name, nameW);
    if (r.sprint && showSprint) label(row, F12, past ? C_DIM : C_ACCENT, sprintX, ty + 1, PICK("SPRINT", "SPR"));
    const time_t rs = r.ns ? r.s[r.ns - 1].start : 0;
    struct tm tm; localtime_r(&rs, &tm);
    strftime(b, sizeof(b), PORT ? "%d %b" : "%a %d %b", &tm);
    label(row, PICK(F14, F12), tc, dateX, ty + PICK(0, 1), b, dateW, LV_TEXT_ALIGN_RIGHT);
    if (isNext) nextRow = row;
  }
  if (nextRow) {
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, max((lv_coord_t)0, (lv_coord_t)(lv_obj_get_y(nextRow) - rowH * 2)), LV_ANIM_OFF);
  }
}

// ------------------------------------------------------------------ page: settings / info

// Status lines, in the order they're shown; the ones that fit are picked by priority
enum InfoKind : uint8_t { IK_IP, IK_WIFI, IK_TZ, IK_DATA, IK_LIVE, IK_DISPLAY, IK_COUNT };
static lv_obj_t *infoLine[IK_COUNT];
static lv_obj_t *infoIpInline = nullptr;      // "or 192.168.1.50" beside the address (landscape)
static lv_obj_t *scrollLbl = nullptr, *timeLbl = nullptr, *brightSl = nullptr, *brightPct = nullptr, *autoSw = nullptr;
static bool infoBuilt = false;
static uint8_t brightAtPress = 100;

static void infoForget() {
  for (auto &l : infoLine) l = nullptr;
  infoIpInline = scrollLbl = timeLbl = brightSl = brightPct = autoSw = nullptr;
  infoBuilt = false;
}

static const char *scrollText() {
  if (PORT) return settings.rotateOn ? "Auto scroll: On" : "Auto scroll: Off";
  return settings.rotateOn ? "Scroll: On" : "Scroll: Off";
}

static const char *timeText() {
  static char b[24];
  if (PORT) snprintf(b, sizeof(b), "Scroll time: %u s", settings.rotateS);
  else snprintf(b, sizeof(b), "%u s / page", settings.rotateS);
  return b;
}

static void onFlip(lv_event_t *) {
  const Settings before = settings;
  settings.flip = !settings.flip;
  settingsApply(before);
}

static void onOrient(lv_event_t *) {
  const Settings before = settings;
  settings.portrait = !settings.portrait;
  settingsApply(before);    // restarts: every screen is laid out at start-up
}

static void onScroll(lv_event_t *) {
  const Settings before = settings;
  settings.rotateOn = !settings.rotateOn;
  settingsApply(before);
  if (scrollLbl) lv_label_set_text(scrollLbl, scrollText());
}

static void onScrollTime(lv_event_t *) {
  static const uint8_t STEPS[] = {5, 10, 15, 20, 30, 45, 60, 90, 120};
  const Settings before = settings;
  uint8_t next = STEPS[0];
  for (uint8_t v : STEPS) if (v > settings.rotateS) { next = v; break; }
  settings.rotateS = next;
  settingsApply(before);
  if (timeLbl) lv_label_set_text(timeLbl, timeText());
}

static void onTheme(lv_event_t *) {
  const Settings before = settings;
  settings.light = !settings.light;
  settingsApply(before);    // the main loop rebuilds every screen in the new colours
}

static void onRefresh(lv_event_t *) { appRequest("refresh"); }
static void onCalibrate(lv_event_t *) { appRequest("calibrate"); }

static void onBright(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) brightAtPress = settings.bright;
  settings.bright = lv_slider_get_value(brightSl);   // the backlight follows straight away
  if (brightPct) lv_label_set_text_fmt(brightPct, "%u%%", settings.bright);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {   // save once, when you let go
    Settings before = settings;
    before.bright = brightAtPress;
    settingsApply(before);
  }
}

static void onAutoDim(lv_event_t *) {
  const Settings before = settings;
  settings.autoDim = lv_obj_has_state(autoSw, LV_STATE_CHECKED);
  settingsApply(before);
}

static void onUpdateChoice(lv_event_t *e) {
  lv_obj_t *mb = lv_event_get_current_target(e);
  const char *txt = lv_msgbox_get_active_btn_text(mb);
  if (txt && !strcmp(txt, "Install")) appRequest("update");
  lv_msgbox_close(mb);
}

void uiPromptUpdate(const char *version, const char *notes) {
  // release notes: plain text, a couple of lines (the fonts can't show markdown or emoji)
  char clean[160], folded[160];
  foldAscii(notes ? notes : "", folded, sizeof(folded));
  size_t o = 0;
  for (const char *c = folded; *c && o + 1 < sizeof(clean); c++) {
    if (*c == '#' || *c == '*' || *c == '`' || *c == '\r') continue;
    if (*c == '\n' && o && clean[o - 1] == '\n') continue;
    if (*c == ' ' && (!o || clean[o - 1] == '\n' || clean[o - 1] == ' ')) continue;   // left over from markdown
    clean[o++] = *c;
  }
  clean[o] = 0;
  if (o > 120) strcpy(clean + 117, "...");
  char body[240];
  snprintf(body, sizeof(body), "LapLogic %s is ready to install. It takes about a minute; keep LapLogic plugged in.%s%s",
           version, clean[0] ? "\n\n" : "", clean);
  static const char *btns[] = {"Install", "Later", ""};
  lv_obj_t *mb = lv_msgbox_create(NULL, "Update available", body, btns, false);
  lv_obj_set_width(mb, min(SW - 20, PICK(360, 290)));
  lv_obj_center(mb);
  lv_obj_add_event_cb(mb, onUpdateChoice, LV_EVENT_VALUE_CHANGED, NULL);
}

static void onResetConfirm(lv_event_t *e) {
  lv_obj_t *mb = lv_event_get_current_target(e);
  const char *txt = lv_msgbox_get_active_btn_text(mb);
  if (txt && !strcmp(txt, "Reset")) appRequest("wifi");
  lv_msgbox_close(mb);
}

static void onReset(lv_event_t *) {
  static const char *btns[] = {"Reset", "Cancel", ""};
  lv_obj_t *mb = lv_msgbox_create(NULL, "Reset Wi-Fi?",
      "LapLogic will forget this network and restart in setup mode.", btns, false);
  lv_obj_set_width(mb, min(SW - 20, PICK(320, 280)));
  lv_obj_center(mb);
  lv_obj_add_event_cb(mb, onResetConfirm, LV_EVENT_VALUE_CHANGED, NULL);
}

static lv_obj_t *button(lv_obj_t *p, int x, int y, int w, int h, const char *txt, lv_event_cb_t cb) {
  lv_obj_t *b = lv_btn_create(p);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(C_SURF2), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(C_ACCENT), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, PICK(8, 6), 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 2, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *l = lv_label_create(b);
  lv_obj_set_style_text_font(l, PICK(F14, F12), 0);
  lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  return b;
}

static void tickInfo();

static void styleSlider(lv_obj_t *sl) {
  lv_obj_set_style_bg_color(sl, lv_color_hex(C_SURF2), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sl, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, lv_color_hex(C_TEXT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(sl, PICK(6, 5), LV_PART_KNOB);
  lv_obj_set_ext_click_area(sl, PICK(14, 10));
}

static void buildInfo() {
  lv_obj_t *t = tiles[PG_INFO];
  lv_obj_clean(t);
  infoForget();
  const int x = PICK(16, 8), w = SW - 2 * x;
  const lv_font_t *fs = PICK(F14, F12);

  // ---- buttons along the bottom
  struct Btn { const char *txt; lv_event_cb_t cb; lv_obj_t **lbl; };
  Btn btns[9];
  int nb = 0;
  const bool tight = COMPACT && !PORT;
  btns[nb++] = {tight ? "Flip" : "Flip screen", onFlip, nullptr};
  btns[nb++] = {settings.portrait ? "Landscape" : "Portrait", onOrient, nullptr};
  btns[nb++] = {scrollText(), onScroll, &scrollLbl};
  btns[nb++] = {timeText(), onScrollTime, &timeLbl};
  btns[nb++] = {settings.light ? "Dark mode" : "Light mode", onTheme, nullptr};
  btns[nb++] = {tight ? "Refresh" : "Refresh data", onRefresh, nullptr};
  if (boardCanCalibrate()) btns[nb++] = {tight ? "Calibrate" : "Calibrate touch", onCalibrate, nullptr};
  btns[nb++] = {"Reset Wi-Fi", onReset, nullptr};

  const int cols = PORT ? 2 : 4;
  const int rows = (nb + cols - 1) / cols;
  const int gap = PICK(6, 4), bh = PORT ? PICK(38, 30) : PICK(34, 28);
  const int bw = (w - (cols - 1) * gap) / cols;
  const int by = BODY_H - 4 - rows * bh - (rows - 1) * gap;
  for (int i = 0; i < nb; i++) {
    lv_obj_t *b = button(t, x + (i % cols) * (bw + gap), by + (i / cols) * (bh + gap), bw, bh, btns[i].txt, btns[i].cb);
    if (btns[i].lbl) *btns[i].lbl = lv_obj_get_child(b, 0);
  }

  // ---- brightness slider (+ auto-dim switch on boards with a light sensor)
  const int sh = PICK(30, 24), sy = by - sh - PICK(6, 4), mid = sy + sh / 2;
  const bool hasLdr = boardLightRaw() >= 0;
  label(t, F12, C_MUTED, x, mid - 8, "Brightness");
  int sx = x + PICK(80, 68), sr = x + w - PICK(48, 40);
  if (hasLdr) {
    const int swW = PICK(40, 34), swH = PICK(22, 18);
    autoSw = lv_switch_create(t);
    lv_obj_set_size(autoSw, swW, swH);
    lv_obj_set_pos(autoSw, x + w - swW, mid - swH / 2);
    lv_obj_set_style_bg_color(autoSw, lv_color_hex(C_SURF2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(autoSw, lv_color_hex(C_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_ext_click_area(autoSw, 8);
    if (settings.autoDim) lv_obj_add_state(autoSw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(autoSw, onAutoDim, LV_EVENT_VALUE_CHANGED, NULL);
    label(t, F12, C_MUTED, x + w - swW - PICK(38, 32), mid - 8, "Auto");
    sr = x + w - swW - PICK(46, 38) - PICK(40, 34);
  }
  brightSl = lv_slider_create(t);
  lv_slider_set_range(brightSl, 5, 100);
  lv_slider_set_value(brightSl, settings.bright, LV_ANIM_OFF);
  lv_obj_set_size(brightSl, sr - sx - PICK(10, 8), PICK(8, 6));
  lv_obj_set_pos(brightSl, sx + 4, mid - PICK(4, 3));
  styleSlider(brightSl);
  for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(brightSl, onBright, c, NULL);
  brightPct = label(t, F12, C_TEXT, sr - 4, mid - 8, "", PICK(40, 34), LV_TEXT_ALIGN_RIGHT);
  lv_label_set_text_fmt(brightPct, "%u%%", settings.bright);

  // ---- address, version, then as many status lines as fit
  label(t, F12, C_MUTED, x, PICK(6, 3), (COMPACT || PORT) ? "On the same Wi-Fi, open:"
                                                          : "Change settings from a phone or PC on the same Wi-Fi:", w);
  lv_obj_t *url = label(t, PICK(F20, F16), C_TEXT, x, PICK(24, 18), "http://" HOSTNAME ".local");
  lv_obj_t *ver = label(t, F12, C_DIM, x, PICK(30, 21), APP_VERSION, w, LV_TEXT_ALIGN_RIGHT);
  (void)ver;
  if (!PORT) {
    lv_obj_update_layout(url);
    const int ix = x + lv_obj_get_width(url) + PICK(10, 6);
    infoIpInline = label(t, fs, C_MUTED, ix, PICK(28, 20), "", x + w - ix - PICK(52, 44));
  }
  const int y0 = PICK(52, 38), lh = PICK(19, 15);
  const int fit = max(0, (sy - 2 - y0) / lh);
  const InfoKind prio[] = {IK_WIFI, IK_DATA, IK_IP, IK_TZ, IK_LIVE, IK_DISPLAY};
  bool want[IK_COUNT] = {};
  int n = 0;
  for (InfoKind k : prio) {
    if (n >= fit) break;
    if (k == IK_IP && !PORT) continue;              // shown beside the address instead
    if (k == IK_LIVE && !trackLiveSupported()) continue;
    want[k] = true; n++;
  }
  int y = y0;
  for (int k = 0; k < IK_COUNT; k++)
    if (want[k]) { infoLine[k] = label(t, fs, C_MUTED, x, y, "", w); y += lh; }
  infoBuilt = true;
}

static void tickInfo() {
  if (!infoBuilt) return;
  char b[96];
  const bool online = netState() == NET_ONLINE;
  if (scrollLbl && strcmp(lv_label_get_text(scrollLbl), scrollText())) lv_label_set_text(scrollLbl, scrollText());
  if (timeLbl && strcmp(lv_label_get_text(timeLbl), timeText())) lv_label_set_text(timeLbl, timeText());
  // follow changes made from the web page (but not while someone is dragging the slider)
  if (brightSl && !lv_obj_has_state(brightSl, LV_STATE_PRESSED) && lv_slider_get_value(brightSl) != settings.bright) {
    lv_slider_set_value(brightSl, settings.bright, LV_ANIM_OFF);
    lv_label_set_text_fmt(brightPct, "%u%%", settings.bright);
  }
  if (autoSw && lv_obj_has_state(autoSw, LV_STATE_CHECKED) != settings.autoDim) {
    if (settings.autoDim) lv_obj_add_state(autoSw, LV_STATE_CHECKED); else lv_obj_clear_state(autoSw, LV_STATE_CHECKED);
  }

  const String url = online ? netUrl() : String("");
  if (infoIpInline) { snprintf(b, sizeof(b), "or %s", url.length() > 7 ? url.c_str() + 7 : ""); lv_label_set_text(infoIpInline, online ? b : ""); }
  if (infoLine[IK_IP]) { snprintf(b, sizeof(b), "or %s", url.c_str()); lv_label_set_text(infoLine[IK_IP], online ? b : ""); }
  if (infoLine[IK_WIFI]) {
    if (online) snprintf(b, sizeof(b), "Wi-Fi: %s  (%d dBm)", netSsid().c_str(), netRssi());
    else snprintf(b, sizeof(b), "Wi-Fi: not connected");
    lv_label_set_text(infoLine[IK_WIFI], b);
  }
  if (infoLine[IK_TZ]) { snprintf(b, sizeof(b), "Time zone: %s", netTzLabel().c_str()); lv_label_set_text(infoLine[IK_TZ], b); }
  if (infoLine[IK_DATA]) {
    if (D && D->standingsAt) { char w[40]; fmtWhen(D->standingsAt, w, sizeof(w)); snprintf(b, sizeof(b), "Data updated: %s", w); }
    else snprintf(b, sizeof(b), "Data updated: not yet");
    lv_label_set_text(infoLine[IK_DATA], b);
  }
  if (infoLine[IK_LIVE]) { snprintf(b, sizeof(b), "Live map: %s", trackLiveStatus()); lv_label_set_text(infoLine[IK_LIVE], b); }
  if (infoLine[IK_DISPLAY]) { snprintf(b, sizeof(b), "Display: %s", boardPanelName()); lv_label_set_text(infoLine[IK_DISPLAY], b); }
}

// ------------------------------------------------------------------ screens

static lv_obj_t *newScreen() {
  lv_obj_t *s = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

static void buildPage(int p) {
  if (UI_LAZY_PAGES) {   // only this page exists now: free the logos the last one used
    lv_obj_clean(tiles[p]);
    logosRelease();
  }
  switch (p) {
    case PG_NEXT:    buildNext(); break;
    case PG_MAP:     buildMap(); break;
    case PG_DRIVERS: buildDrivers(); break;
    case PG_TEAMS:   buildTeams(); break;
    case PG_RESULT:  buildResult(); break;
    case PG_CAL:     buildCalendar(); break;
    default:         buildInfo(); tickInfo(); break;
  }
}

static void buildMain() {
  scrMain = newScreen();

  // header
  lv_obj_t *hdr = box(scrMain, 0, 0, SW, HDR_H, C_BG);
  hdrLine = box(hdr, 0, HDR_H - 1, SW, 1, C_LINE);
  const lv_font_t *hf = PICK(F14, F12);
  const int ty = (HDR_H - lv_font_get_line_height(hf)) / 2;
  const int clockW = PICK(98, 84);
  const int clockX = SW - PICK(24, 18) - clockW - 4;
  for (int i = 0; i < PG_COUNT; i++) dots[i] = nullptr;
  if (PORT) {
    // narrow: page title on the left instead of the logo
    hdrTitle = label(hdr, hf, C_TEXT, 10, ty, "", clockX - 14);
  } else {
    lv_obj_t *lg = logo(hdr, hf);
    lv_obj_set_pos(lg, PICK(10, 8), ty);
    const int titleX = PICK(122, 104);
    const int dotsX = clockX - NPAGES * 10 - 2;
    hdrTitle = label(hdr, hf, C_MUTED, titleX, ty, "", (COMPACT ? clockX : dotsX) - titleX - 8);
    if (!COMPACT)
      for (int i = 0; i < NPAGES; i++) dots[i] = box(hdr, dotsX + i * 10, HDR_H / 2 - 3, 6, 6, C_DIM, LV_OPA_COVER, 3);
  }
  hdrTitleW = lv_obj_get_style_width(hdrTitle, 0);
  hdrFlagRight = clockX - 6;
  flagShown[0] = 0;   // a rebuilt header shows the current flag again
  // race control flag pill (live sessions on the 4.3" board), hidden until there's something to show
  hdrFlag = lv_label_create(hdr);
  lv_obj_set_style_text_font(hdrFlag, F12, 0);
  lv_obj_set_style_bg_opa(hdrFlag, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(hdrFlag, 4, 0);
  lv_obj_set_style_pad_hor(hdrFlag, 8, 0);
  lv_obj_set_style_pad_ver(hdrFlag, 2, 0);
  lv_obj_add_flag(hdrFlag, LV_OBJ_FLAG_HIDDEN);
  hdrClock = label(hdr, hf, C_TEXT, clockX, ty, "", clockW, LV_TEXT_ALIGN_RIGHT);
  hdrWifi = label(hdr, hf, C_DIM, SW - PICK(24, 18), ty, LV_SYMBOL_WIFI);

  // pages
  tv = lv_tileview_create(scrMain);
  lv_obj_set_pos(tv, 0, HDR_H);
  lv_obj_set_size(tv, SW, BODY_H);
  lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  for (int i = 0; i < PG_COUNT; i++) tiles[i] = nullptr;
  for (int i = 0; i < NPAGES; i++) {
    lv_dir_t dir = (lv_dir_t)((i > 0 ? LV_DIR_LEFT : 0) | (i < NPAGES - 1 ? LV_DIR_RIGHT : 0));
    lv_obj_t *tile = lv_tileview_add_tile(tv, i, 0, dir);
    lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_OFF);
    tiles[PAGE_ORDER[i]] = tile;
  }
  lv_obj_add_event_cb(tv, onTileChanged, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(tv, onGesture, LV_EVENT_GESTURE, NULL);
  lv_obj_clear_flag(tv, LV_OBJ_FLAG_GESTURE_BUBBLE);   // gestures on any page stop here

  if (UI_LAZY_PAGES) buildPage(curPage);
  else for (int i = 0; i < NPAGES; i++) buildPage(PAGE_ORDER[i]);
  updateHeader();
}

static void buildSetup() {
  scrSetup = newScreen();
  lv_obj_t *lg = logo(scrSetup, PICK(F20, F16));
  lv_obj_set_pos(lg, PICK(16, 10), PICK(12, 8));

  const int qs = PICK(150, 112), qp = PICK(10, 8), qb = qs + 2 * qp;
  const int qx = PORT ? (SW - qb) / 2 : PICK(16, 8), qy = PICK(56, 42);
  lv_obj_t *qbg = box(scrSetup, qx, qy, qb, qb, 0xFFFFFF, LV_OPA_COVER, 8);
  lv_obj_t *qr = lv_qrcode_create(qbg, qs, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
  const char *wifiQr = "WIFI:T:nopass;S:" SETUP_AP_NAME ";;";
  lv_qrcode_update(qr, wifiQr, strlen(wifiQr));
  lv_obj_center(qr);

  const int x = PORT ? PICK(20, 12) : qx + qb + PICK(28, 12);
  const int w = SW - x - PICK(12, 6), ind = PICK(18, 12);
  const lv_font_t *fs = PICK(F14, F12);
  int y = PORT ? qy + qb + PICK(16, 10) : qy;
  label(scrSetup, PICK(F20, F16), C_TEXT, x, y, "Let's get online", w); y += PICK(34, 26);
  label(scrSetup, fs, C_MUTED, x, y, PICK("1. Scan the code, or join Wi-Fi", "1. Scan, or join Wi-Fi"), w); y += PICK(20, 16);
  label(scrSetup, PICK(F16, F14), C_FAV, x + ind, y, SETUP_AP_NAME, w - ind); y += PICK(28, 22);
  label(scrSetup, fs, C_MUTED, x, y, PICK("2. A setup page opens. If not,", "2. Setup page opens, or"), w); y += PICK(18, 15);
  label(scrSetup, fs, C_MUTED, x + ind, y, PICK("browse to 192.168.4.1", "go to 192.168.4.1"), w - ind); y += PICK(26, 21);
  label(scrSetup, fs, C_MUTED, x, y, PICK("3. Pick your Wi-Fi network", "3. Pick your network"), w); y += PICK(30, 24);
  setupNote = para(scrSetup, F12, C_ACCENT, x, y, w, "");
}

static void setTheme() {
  pal = settings.light ? LIGHT : DARK;
  lv_disp_t *disp = lv_disp_get_default();
  lv_theme_t *th = lv_theme_default_init(disp, lv_color_hex(C_ACCENT), lv_color_hex(C_FAV), !settings.light, PICK(F14, F12));
  lv_disp_set_theme(disp, th);
}

static void buildSplash() {
  scrSplash = newScreen();
  lv_obj_t *lg = logo(scrSplash, PICK(F28, F20));
  lv_obj_align(lg, LV_ALIGN_CENTER, 0, -PICK(30, 24));
  splashLine = para(scrSplash, PICK(F14, F12), C_MUTED, 10, SH / 2 + PICK(14, 10), SW - 20, "", LV_TEXT_ALIGN_CENTER);
  label(scrSplash, F12, C_DIM, 0, SH - 24, APP_VERSION[0] == 'b' ? APP_VERSION " (beta)" : APP_VERSION, SW, LV_TEXT_ALIGN_CENTER);
}

void uiBegin() {
  lv_disp_t *disp = lv_disp_get_default();
  SW = lv_disp_get_hor_res(disp);
  SH = lv_disp_get_ver_res(disp);
  PORT = SH > SW;
  HDR_H = PICK(30, 26);
  BODY_H = SH - HDR_H;
  setTheme();
  buildSplash();
  buildSetup();
  buildMain();
  lv_scr_load(scrSplash);
}

// Light/dark mode changed: throw every screen away and build it again in the new colours.
// The old ones go first, so the CYD never needs memory for two sets at once.
void uiApplyTheme() {
  lv_obj_t *act = lv_scr_act();
  const int which = act == scrMain ? 2 : act == scrSetup ? 1 : 0;
  String splashText = lv_label_get_text(splashLine), setupText = lv_label_get_text(setupNote);
  lv_obj_t *tmp = lv_obj_create(NULL);
  lv_scr_load(tmp);
  mapDropped();
  infoForget();
  nxCount = nullptr;
  lv_obj_del(scrMain);
  lv_obj_del(scrSetup);
  lv_obj_del(scrSplash);
  setTheme();
  buildSplash();
  buildSetup();
  lv_label_set_text(splashLine, splashText.c_str());
  lv_label_set_text(setupNote, setupText.c_str());
  buildMain();
  lv_obj_set_tile_id(tv, tileIndex(curPage), 0, LV_ANIM_OFF);
  lv_scr_load(which == 2 ? scrMain : which == 1 ? scrSetup : scrSplash);
  lv_obj_del(tmp);
}

void uiShowSplash(const char *line) {
  lv_label_set_text(splashLine, line ? line : "");
  if (lv_scr_act() != scrSplash) lv_scr_load(scrSplash);
}

void uiShowSetup(const char *note) {
  lv_label_set_text(setupNote, note ? note : "");
  if (lv_scr_act() != scrSetup) lv_scr_load(scrSetup);
}

void uiShowMain() {
  if (lv_scr_act() != scrMain) lv_scr_load(scrMain);
}

void uiSetData(const F1Data *d) {
  D = d;
  uiRebuild();
}

void uiRebuild() {
  if (UI_LAZY_PAGES) {
    // don't rebuild the settings page under someone's finger; just refresh its text
    if (curPage == PG_INFO && pageBuilt(PG_INFO)) tickInfo();
    else buildPage(curPage);
    keepOnly(curPage);
  } else {
    buildNext(); if (HAS_TRACK_PAGE) buildMap(); buildDrivers(); buildTeams(); buildResult(); buildCalendar();
    tickInfo();
  }
  updateHeader();
}

// Race control in the header: a pill ("SAFETY CAR") and the header's bottom rule in the flag colour
static void updateFlag() {
  if (!hdrFlag) return;
  char txt[24];
  uint32_t c = 0;
  const bool on = trackRaceControl(txt, sizeof(txt), c);
  if (!on) {
    if (!flagShown[0]) return;
    flagShown[0] = 0;
    lv_obj_add_flag(hdrFlag, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(hdrLine, lv_color_hex(C_LINE), 0);
    lv_obj_set_height(hdrLine, 1);
    lv_obj_set_y(hdrLine, HDR_H - 1);
    lv_obj_set_width(hdrTitle, hdrTitleW);
    lv_obj_clear_flag(hdrTitle, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < NPAGES; i++) if (dots[i]) lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (!strcmp(flagShown, txt)) return;
  strlcpy(flagShown, txt, sizeof(flagShown));
  const int lum = (((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000;
  lv_label_set_text(hdrFlag, txt);
  lv_obj_set_style_bg_color(hdrFlag, lv_color_hex(c), 0);
  lv_obj_set_style_text_color(hdrFlag, lv_color_hex(lum > 150 ? 0x101014 : 0xFFFFFF), 0);
  lv_obj_clear_flag(hdrFlag, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(hdrFlag);
  const int w = lv_obj_get_width(hdrFlag), h = lv_obj_get_height(hdrFlag);
  lv_obj_set_pos(hdrFlag, hdrFlagRight - w, (HDR_H - h) / 2);
  for (int i = 0; i < NPAGES; i++) if (dots[i]) lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);   // the pill sits where the dots were
  const int titleW = hdrFlagRight - w - 8 - (int)lv_obj_get_x(hdrTitle);
  if (titleW < 60) {   // narrow (portrait): the flag takes the title's place
    lv_obj_add_flag(hdrTitle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(hdrFlag, lv_obj_get_x(hdrTitle));
  }
  else { lv_obj_clear_flag(hdrTitle, LV_OBJ_FLAG_HIDDEN); lv_obj_set_width(hdrTitle, titleW); }
  lv_obj_set_style_bg_color(hdrLine, lv_color_hex(c), 0);
  lv_obj_set_height(hdrLine, 3);
  lv_obj_set_y(hdrLine, HDR_H - 3);
}

void uiTick() {
  static uint32_t lastSec = 0;
  static bool hadTime = false;
  if (millis() - lastSec >= 1000) {
    lastSec = millis();
    const time_t now = time(nullptr);
    const bool timeOk = netTimeValid();
    if (timeOk && !hadTime) { hadTime = true; uiRebuild(); }   // clock just got set

    if (timeOk) {
      struct tm tm; localtime_r(&now, &tm);
      char d[8], c[12], b[24];
      strftime(d, sizeof(d), "%a", &tm);
      fmtClock(tm, c, sizeof(c));
      snprintf(b, sizeof(b), "%s %s", d, c);
      lv_label_set_text(hdrClock, b);
      tickNext(now);
    }
    setColour(hdrWifi, netState() == NET_ONLINE ? C_OK : C_DIM);
    updateFlag();
    if (curPage == PG_INFO) tickInfo();

    // new circuit / replay / live session: rebuild the map if it's on screen (or always kept built)
    if (track && track->version != mapBuiltVersion && pageBuilt(PG_MAP)) { buildMap(); updateHeader(); }
  }

  // auto-rotate through the first pages
  if (settings.rotateOn && settings.rotateS && lv_scr_act() == scrMain) {
    const uint32_t lt = boardLastTouchMs();
    const bool paused = lt && millis() - lt < TOUCH_PAUSE_MS;
    // give the final-lap replay time to play through once
    const uint32_t dwell = (curPage == PG_MAP && mapMode == TM_REPLAY && track)
                             ? max((uint32_t)settings.rotateS * 1000UL, (uint32_t)track->nFrames * 1000UL + 3000UL)
                             : settings.rotateS * 1000UL;
    if (!paused && millis() - pageSince > dwell) {
      int i = tileIndex(curPage);
      do { i = (i + 1) % NPAGES; } while (!autoScrolls(PAGE_ORDER[i]));   // wraps round to the first
      goPage(PAGE_ORDER[i]);
    }
  }
}

// ------------------------------------------------------------------ for the web page / USB setup

const F1Data *uiData() { return D && D->nRaces ? D : nullptr; }

String uiDataSummary() {
  if (!D || !D->standingsAt) return "not yet";
  char w[40]; fmtWhen(D->standingsAt, w, sizeof(w));
  return String(w);
}

String uiWeatherSummary() {
  const Weather *w = weatherFor(-1);
  if (!w) return "";
  char b[160], t[24] = "";
  if (const char *tt = trackTempText()) snprintf(t, sizeof(t), ", track %s", tt);
  const Race *r = nullptr;
  for (int i = 0; i < D->nRaces; i++) if (D->races[i].round == w->round) r = &D->races[i];
  char when[40]; fmtWhen(w->at, when, sizeof(when));
  snprintf(b, sizeof(b), "%.0f\xC2\xB0%s, %s%s, wind %.0f %s, humidity %u%% at %s (%s)", w->temp, w->imperial ? "F" : "C",
           weatherText(w->code), t, w->wind, w->imperial ? "mph" : "km/h", w->humidity, r ? r->locality : "the circuit", when);
  String out(b);
  out[0] = toupper(out[0]);
  return out;
}
