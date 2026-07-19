// src/ui/dashboard.cpp — page 0 (main): CO2 arc gauge + PM2.5 dotted-ring gauge
// + metric tiles. CO2/PM2.5 have well-known thresholds, so they get gauges.
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <time.h>

#include "app/clock.hpp"
#include "config.hpp"
#include "domain/aqi.hpp"
#include "domain/measurement.hpp"
#include "ui/fonts/fonts.hpp"
#include "ui/theme.hpp"
#include "ui/ui.hpp"

using namespace domain;

namespace ui {
namespace aq {
namespace {

constexpr int SCR_CX = 480, SCR_CY = 270;
constexpr int NDOTS = 28;
constexpr float DEG = 0.017453293f;

lv_obj_t* scr = nullptr;
lv_obj_t* hdrLeft = nullptr;
lv_obj_t* hdrRight = nullptr;
lv_obj_t* staleBadge = nullptr;
lv_obj_t* clockLbl = nullptr;
lv_obj_t* dateLbl = nullptr;

// CO2: arc with a knob riding the track (ref image #6).
struct ArcGauge {
  lv_obj_t* arc = nullptr;
  lv_obj_t* value = nullptr;
  lv_obj_t* status = nullptr;
  int min = 0, max = 100, lastVal = INT32_MIN;
};
ArcGauge gCo2;

// PM2.5: ring of dots filled to the value + status check (ref image #7).
struct DotGauge {
  lv_obj_t* dots[NDOTS] = {};
  lv_obj_t* value = nullptr;
  lv_obj_t* badge = nullptr;
  lv_obj_t* badgeSym = nullptr;
  int min = 0, max = 100, lastFilled = -1;
};
DotGauge gPm;

struct Tile {
  lv_obj_t* value = nullptr;
  lv_obj_t* unit = nullptr;
  lv_obj_t* status = nullptr;
};
Tile tTemp, tHum, tTvoc, tNox, tPm1, tPm10, tPm003;

// ---- center label block shared by both gauges --------------------------
void centerLabels(int cx, int cy, int dia, const char* name, const char* unit,
                  lv_obj_t** valueOut) {
  lv_obj_t* nm = theme::label(scr, &font_status_22);
  theme::setText(nm, name);
  lv_obj_align(nm, LV_ALIGN_CENTER, cx - SCR_CX, cy - SCR_CY - dia / 2 - 14);
  lv_obj_t* u = theme::label(scr, &font_units_18);
  theme::setText(u, unit);
  lv_obj_align(u, LV_ALIGN_CENTER, cx - SCR_CX, cy - SCR_CY - 24);
  *valueOut = theme::label(scr, &font_num_64);
  lv_obj_align(*valueOut, LV_ALIGN_CENTER, cx - SCR_CX, cy - SCR_CY + 12);
}

void makeArc(int cx, int cy, int dia, const char* name, const char* unit,
             int mn, int mx) {
  gCo2.min = mn;
  gCo2.max = mx;
  lv_obj_t* a = lv_arc_create(scr);
  lv_obj_set_size(a, dia, dia);
  lv_obj_align(a, LV_ALIGN_CENTER, cx - SCR_CX, cy - SCR_CY);
  lv_arc_set_bg_angles(a, 135, 45);  // 270° with gap at bottom
  lv_arc_set_range(a, mn, mx);
  lv_arc_set_value(a, mn);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_arc_color(a, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, 16, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, lv_color_black(), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(a, 16, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(a, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(a, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_border_color(a, lv_color_black(), LV_PART_KNOB);
  lv_obj_set_style_border_width(a, 3, LV_PART_KNOB);
  lv_obj_set_style_pad_all(a, 7, LV_PART_KNOB);
  lv_obj_set_style_radius(a, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  gCo2.arc = a;

  centerLabels(cx, cy, dia, name, unit, &gCo2.value);
  gCo2.status = theme::label(scr, &lv_font_montserrat_18);
  lv_obj_align(gCo2.status, LV_ALIGN_CENTER, cx - SCR_CX,
               cy - SCR_CY + dia / 2 - 30);
}

void makeDots(int cx, int cy, int dia, const char* name, const char* unit,
              int mn, int mx) {
  gPm.min = mn;
  gPm.max = mx;
  float r = dia / 2.0f - 8.0f;
  const int ds = 13;
  for (int i = 0; i < NDOTS; ++i) {
    float th = (135.0f + 270.0f * i / (NDOTS - 1)) * DEG;
    int x = (int)lroundf(cx + r * cosf(th)) - ds / 2;
    int y = (int)lroundf(cy + r * sinf(th)) - ds / 2;
    lv_obj_t* d = lv_obj_create(scr);
    lv_obj_set_size(d, ds, ds);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(d, lv_color_black(), 0);
    lv_obj_set_style_border_width(d, 2, 0);
    lv_obj_set_style_bg_color(d, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(d, 0, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    gPm.dots[i] = d;
  }
  centerLabels(cx, cy, dia, name, unit, &gPm.value);

  gPm.badge = lv_obj_create(scr);
  lv_obj_set_size(gPm.badge, 32, 32);
  lv_obj_align(gPm.badge, LV_ALIGN_CENTER, cx - SCR_CX,
               cy - SCR_CY + dia / 2 - 30);
  lv_obj_set_style_radius(gPm.badge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(gPm.badge, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(gPm.badge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(gPm.badge, 0, 0);
  lv_obj_set_style_pad_all(gPm.badge, 0, 0);
  lv_obj_clear_flag(gPm.badge, LV_OBJ_FLAG_SCROLLABLE);
  gPm.badgeSym =
      theme::label(gPm.badge, &lv_font_montserrat_18, lv_color_white());
  lv_obj_center(gPm.badgeSym);
}

Tile makeTile(int x, int y, int w, int h, const char* name, const char* unit) {
  lv_obj_t* c = theme::card(scr, x, y, w, h);
  lv_obj_t* cap = theme::label(c, &lv_font_montserrat_14);
  theme::setText(cap, name);
  lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);
  Tile t;
  t.value = theme::label(c, &font_num_40);
  lv_obj_align(t.value, LV_ALIGN_LEFT_MID, 0, -2);
  t.unit = theme::label(c, &font_units_18);
  theme::setText(t.unit, unit);
  lv_obj_align_to(t.unit, t.value, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);
  t.status = theme::label(c, &font_status_22);
  lv_obj_align(t.status, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  return t;
}

void fmt1(char* b, size_t n, float v) {
  if (isnan(v))
    snprintf(b, n, "--");
  else
    snprintf(b, n, "%.1f", v);
}
void fmt0(char* b, size_t n, float v) {
  if (isnan(v))
    snprintf(b, n, "--");
  else
    snprintf(b, n, "%.0f", v);
}

void setArc(ArcGauge& g, float v, Band band) {
  char b[24];
  fmt0(b, sizeof(b), v);
  theme::setText(g.value, b);
  theme::setText(g.status, bandWord(band));
  if (!isnan(v)) {
    int iv = (int)lroundf(v);
    if (iv < g.min) iv = g.min;
    if (iv > g.max) iv = g.max;
    if (iv != g.lastVal) {
      g.lastVal = iv;
      lv_arc_set_value(g.arc, iv);
    }
  }
}

void setDots(DotGauge& g, float v, Band band) {
  char b[24];
  fmt1(b, sizeof(b), v);
  theme::setText(g.value, b);
  const char* sym = (band <= Band::Moderate) ? LV_SYMBOL_OK : LV_SYMBOL_WARNING;
  theme::setText(g.badgeSym, sym);

  int filled = 0;
  if (!isnan(v)) {
    float f = (v - g.min) / (float)(g.max - g.min);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    filled = (int)lroundf(f * NDOTS);
  }
  if (filled != g.lastFilled) {
    for (int i = 0; i < NDOTS; ++i)
      lv_obj_set_style_bg_color(
          g.dots[i], (i < filled) ? lv_color_black() : lv_color_white(), 0);
    g.lastFilled = filled;
  }
}

}  // namespace

lv_obj_t* build() {
  scr = theme::screen();

  hdrLeft = theme::label(scr, &lv_font_montserrat_18);
  lv_obj_align(hdrLeft, LV_ALIGN_TOP_LEFT, 10, 10);
  hdrRight = theme::label(scr, &lv_font_montserrat_18);
  lv_obj_align(hdrRight, LV_ALIGN_TOP_RIGHT, -10, 10);

  staleBadge = theme::pill(scr, &lv_font_montserrat_14);
  lv_obj_align(staleBadge, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_add_flag(staleBadge, LV_OBJ_FLAG_HIDDEN);

  makeArc(180, 214, 300, "CO2", "ppm", 0, 2000);
  makeDots(490, 214, 300, "PM2.5", "\xC2\xB5g/m\xC2\xB3", 0, 100);

  tTemp = makeTile(648, 50, 150, 150, "TEMP",
                   "\xC2\xB0"
                   "C");
  tHum = makeTile(802, 50, 150, 150, "HUMIDITY", "%");
  tTvoc = makeTile(648, 206, 150, 150, "TVOC", "idx");
  tNox = makeTile(802, 206, 150, 150, "NOX", "idx");

  tPm1 = makeTile(8, 362, 228, 154, "PM1.0", "\xC2\xB5g/m\xC2\xB3");
  tPm10 = makeTile(244, 362, 228, 154, "PM10", "\xC2\xB5g/m\xC2\xB3");
  tPm003 = makeTile(480, 362, 228, 154, "PM0.3", "count");

  lv_obj_t* cClock = theme::card(scr, 716, 362, 236, 154, 3);
  clockLbl = theme::label(cClock, &font_num_64);
  lv_obj_align(clockLbl, LV_ALIGN_CENTER, 0, -14);
  dateLbl = theme::label(cClock, &lv_font_montserrat_18);
  lv_obj_align(dateLbl, LV_ALIGN_BOTTOM_MID, 0, 0);
  return scr;
}

void update(const app::Snapshot& s) {
  const Measurement& m = s.m;
  char b[48];

  if (m.valid && m.model[0])
    snprintf(b, sizeof(b), LV_SYMBOL_HOME " AIRGRADIENT ONE  %s", m.model);
  else
    snprintf(b, sizeof(b), LV_SYMBOL_HOME " AIRGRADIENT ONE");
  theme::setText(hdrLeft, b);
  snprintf(b, sizeof(b), LV_SYMBOL_WIFI " %d dBm", s.rssi);
  theme::setText(hdrRight, b);

  char clk[8] = "--:--", dat[24] = "";
  time_t now = time(nullptr);
  if (now > app::kClockSyncedAfter) {
    struct tm t;
    localtime_r(&now, &t);
    snprintf(clk, sizeof(clk), "%02d:%02d", t.tm_hour, t.tm_min);
    strftime(dat, sizeof(dat), "%a %d %b", &t);
  }
  theme::setText(clockLbl, clk);
  theme::setText(dateLbl, dat);

  if (s.state == app::NetState::Stale || s.state == app::NetState::Offline) {
    uint32_t mins = s.lastOkMs ? (millis() - s.lastOkMs) / 60000 : 0;
    snprintf(b, sizeof(b), LV_SYMBOL_WARNING " LAST SEEN %lu MIN",
             (unsigned long)mins);
    theme::pillText(staleBadge, b);
    lv_obj_clear_flag(staleBadge, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(staleBadge, LV_OBJ_FLAG_HIDDEN);
  }

  setArc(gCo2, m.rco2, bandCo2(m.rco2));
  setDots(gPm, m.pm02, bandPm25(m.pm02));

  fmt0(b, sizeof(b), m.tempC());
  theme::setText(tTemp.value, b);
  theme::setText(tTemp.status, comfortWord(bandTemp(m.tempC())));
  fmt0(b, sizeof(b), m.humidity());
  theme::setText(tHum.value, b);
  theme::setText(tHum.status, comfortWord(bandRhum(m.humidity())));
  fmt0(b, sizeof(b), m.tvocIndex);
  theme::setText(tTvoc.value, b);
  theme::setText(tTvoc.status, bandWord(bandTvoc(m.tvocIndex)));
  fmt0(b, sizeof(b), m.noxIndex);
  theme::setText(tNox.value, b);
  theme::setText(tNox.status, bandWord(bandNox(m.noxIndex)));

  fmt1(b, sizeof(b), m.pm01);
  theme::setText(tPm1.value, b);
  theme::setText(tPm1.status, bandWord(bandPm25(m.pm01)));
  fmt1(b, sizeof(b), m.pm10);
  theme::setText(tPm10.value, b);
  theme::setText(tPm10.status, bandWord(bandPm25(m.pm10)));
  fmt0(b, sizeof(b), m.pm003Count);
  theme::setText(tPm003.value, b);
  theme::setText(tPm003.status, "");
}

}  // namespace aq
}  // namespace ui
