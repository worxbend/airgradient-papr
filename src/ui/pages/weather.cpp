// src/ui/pages/weather.cpp — combined outdoor page: current + UV + forecast.
// Three bands: [NOW | AIR | UV]  /  HOURLY row  /  3-DAY range bars.
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <time.h>

#include "domain/weather.hpp"
#include "ui/fonts/fonts.hpp"
#include "ui/pages.hpp"
#include "ui/theme.hpp"

namespace ui {
namespace weather {
namespace {

lv_obj_t* scr = nullptr;
lv_obj_t* hdrRight = nullptr;
// NOW
lv_obj_t* temp = nullptr;
lv_obj_t* desc = nullptr;
lv_obj_t* feels = nullptr;
// AIR
lv_obj_t* hum = nullptr;
lv_obj_t* wind = nullptr;
lv_obj_t* precip = nullptr;
lv_obj_t* sun = nullptr;
// UV
lv_obj_t* uvNum = nullptr;
lv_obj_t* uvBand = nullptr;
lv_obj_t* uvMax = nullptr;
lv_obj_t* uvWin = nullptr;

struct HourCol { lv_obj_t* label; lv_obj_t* t; lv_obj_t* uv; };
HourCol hc[8];

struct DayRow {
  lv_obj_t* name; lv_obj_t* lo; lv_obj_t* hi; lv_obj_t* cond;
  lv_obj_t* track; lv_obj_t* fill;
};
DayRow dr[3];

constexpr int TRACK_W = 430;

lv_obj_t* badge(lv_obj_t* parent, const lv_font_t* font) {
  lv_obj_t* p = lv_obj_create(parent);
  lv_obj_set_size(p, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(p, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(p, 6, 0);
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_style_pad_hor(p, 7, 0);
  lv_obj_set_style_pad_ver(p, 2, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* t = theme::label(p, font, lv_color_white());
  lv_obj_center(t);
  lv_obj_set_user_data(p, t);
  return p;
}
void badgeText(lv_obj_t* b, const char* s) {
  theme::setText((lv_obj_t*)lv_obj_get_user_data(b), s);
}

lv_obj_t* cap(lv_obj_t* card, const char* txt) {
  lv_obj_t* c = theme::label(card, &lv_font_montserrat_16);
  theme::setText(c, txt);
  lv_obj_align(c, LV_ALIGN_TOP_LEFT, 0, 0);
  return c;
}

}  // namespace

lv_obj_t* build() {
  scr = theme::screen();
  theme::header(scr, &lv_font_montserrat_20, "WEATHER", &hdrRight);

  // ---- Band A: NOW | AIR | UV ----
  lv_obj_t* cNow = theme::card(scr, 8, 56, 304, 178, 3);
  cap(cNow, "NOW");
  temp = theme::label(cNow, &font_num_64);
  lv_obj_align(temp, LV_ALIGN_TOP_LEFT, 0, 24);
  desc = theme::label(cNow, &lv_font_montserrat_24);
  lv_obj_align(desc, LV_ALIGN_TOP_LEFT, 0, 98);
  feels = theme::label(cNow, &font_units_18);  // has ° glyph
  lv_obj_align(feels, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t* cAir = theme::card(scr, 320, 56, 304, 178);
  cap(cAir, "AIR");
  hum = theme::label(cAir, &lv_font_montserrat_20);
  lv_obj_align(hum, LV_ALIGN_TOP_LEFT, 0, 30);
  wind = theme::label(cAir, &lv_font_montserrat_20);
  lv_obj_align(wind, LV_ALIGN_TOP_LEFT, 0, 62);
  precip = theme::label(cAir, &lv_font_montserrat_20);
  lv_obj_align(precip, LV_ALIGN_TOP_LEFT, 0, 94);
  sun = theme::label(cAir, &lv_font_montserrat_18);
  lv_obj_align(sun, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t* cUv = theme::card(scr, 632, 56, 320, 178, 3);
  cap(cUv, "UV INDEX");
  uvNum = theme::label(cUv, &font_num_64);
  lv_obj_align(uvNum, LV_ALIGN_TOP_LEFT, 0, 22);
  uvBand = theme::label(cUv, &font_status_30);
  lv_obj_align(uvBand, LV_ALIGN_TOP_LEFT, 120, 44);
  uvMax = theme::label(cUv, &lv_font_montserrat_18);
  lv_obj_align(uvMax, LV_ALIGN_BOTTOM_LEFT, 0, -24);
  uvWin = theme::label(cUv, &lv_font_montserrat_18);
  lv_obj_align(uvWin, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // ---- Band B: HOURLY ----
  lv_obj_t* cHour = theme::card(scr, 8, 242, 944, 130);
  cap(cHour, "HOURLY");
  for (int i = 0; i < 8; ++i) {
    int x = 4 + i * 115;
    hc[i].label = theme::label(cHour, &lv_font_montserrat_16);
    lv_obj_set_width(hc[i].label, 108);
    lv_obj_set_style_text_align(hc[i].label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hc[i].label, LV_ALIGN_TOP_LEFT, x, 34);
    hc[i].t = theme::label(cHour, &font_status_22);
    lv_obj_set_width(hc[i].t, 108);
    lv_obj_set_style_text_align(hc[i].t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hc[i].t, LV_ALIGN_TOP_LEFT, x, 62);
    hc[i].uv = badge(cHour, &lv_font_montserrat_14);
    lv_obj_align(hc[i].uv, LV_ALIGN_TOP_LEFT, x + 32, 96);
  }

  // ---- Band C: 3-DAY ----
  lv_obj_t* cDay = theme::card(scr, 8, 380, 944, 144);
  cap(cDay, "3-DAY FORECAST");
  for (int i = 0; i < 3; ++i) {
    int y = 36 + i * 34;
    dr[i].name = theme::label(cDay, &font_status_22);
    lv_obj_align(dr[i].name, LV_ALIGN_TOP_LEFT, 0, y);
    dr[i].lo = theme::label(cDay, &font_status_22);  // has ° glyph
    lv_obj_align(dr[i].lo, LV_ALIGN_TOP_LEFT, 110, y + 1);
    dr[i].track = lv_obj_create(cDay);
    lv_obj_set_size(dr[i].track, TRACK_W, 16);
    lv_obj_align(dr[i].track, LV_ALIGN_TOP_LEFT, 168, y + 5);
    lv_obj_set_style_radius(dr[i].track, 8, 0);
    lv_obj_set_style_border_color(dr[i].track, lv_color_black(), 0);
    lv_obj_set_style_border_width(dr[i].track, 2, 0);
    lv_obj_set_style_bg_opa(dr[i].track, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(dr[i].track, 0, 0);
    lv_obj_clear_flag(dr[i].track, LV_OBJ_FLAG_SCROLLABLE);
    dr[i].fill = lv_obj_create(dr[i].track);
    lv_obj_set_style_bg_color(dr[i].fill, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dr[i].fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dr[i].fill, 8, 0);
    lv_obj_set_style_border_width(dr[i].fill, 0, 0);
    dr[i].hi = theme::label(cDay, &font_status_22);  // has ° glyph
    lv_obj_align(dr[i].hi, LV_ALIGN_TOP_LEFT, 610, y + 1);
    dr[i].cond = theme::label(cDay, &lv_font_montserrat_18);
    lv_obj_align(dr[i].cond, LV_ALIGN_TOP_LEFT, 700, y + 2);
  }
  return scr;
}

void update(const app::Snapshot& s) {
  const domain::Weather& w = s.weather;
  char b[48];

  char dat[24] = "";
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm t;
    localtime_r(&now, &t);
    strftime(dat, sizeof(dat), "%a %d %b", &t);
  }
  snprintf(b, sizeof(b), "%s   %s", w.city[0] ? w.city : "", dat);
  theme::setText(hdrRight, b);

  if (!w.valid) {
    theme::setText(temp, "--\xC2\xB0");
    theme::setText(desc, "No data");
    theme::setText(feels, "");
    theme::setText(hum, "");
    theme::setText(wind, "");
    theme::setText(precip, "");
    theme::setText(sun, "");
    theme::setText(uvNum, "--");
    theme::setText(uvBand, "");
    theme::setText(uvMax, "");
    theme::setText(uvWin, "No data available");
    for (int i = 0; i < 8; ++i) {
      theme::setText(hc[i].label, "");
      theme::setText(hc[i].t, "");
      lv_obj_add_flag(hc[i].uv, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 3; ++i) {
      theme::setText(dr[i].name, "");
      theme::setText(dr[i].lo, "");
      theme::setText(dr[i].hi, "");
      theme::setText(dr[i].cond, "");
      lv_obj_add_flag(dr[i].track, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  // NOW
  snprintf(b, sizeof(b), "%.0f\xC2\xB0", w.tempC);
  theme::setText(temp, b);
  theme::setText(desc, w.desc[0] ? w.desc : "--");
  snprintf(b, sizeof(b), "Feels %.0f\xC2\xB0", w.apparentC);
  theme::setText(feels, b);

  // AIR
  snprintf(b, sizeof(b), "Humidity   %.0f%%", w.humidity);
  theme::setText(hum, b);
  snprintf(b, sizeof(b), "Wind   %.0f km/h", w.windKmh);
  theme::setText(wind, b);
  snprintf(b, sizeof(b), "Precip   %.1f mm", w.precip);
  theme::setText(precip, b);
  snprintf(b, sizeof(b), LV_SYMBOL_UP " %s   " LV_SYMBOL_DOWN " %s",
           w.sunrise[0] ? w.sunrise : "--", w.sunset[0] ? w.sunset : "--");
  theme::setText(sun, b);

  // UV
  snprintf(b, sizeof(b), "%.0f", w.uvIndex);
  theme::setText(uvNum, b);
  theme::setText(uvBand, domain::uvWord(w.uvIndex));
  snprintf(b, sizeof(b), "Today max %.0f", w.uvMax);
  theme::setText(uvMax, b);
  snprintf(b, sizeof(b), "High UV: %s", w.uvWindow[0] ? w.uvWindow : "--");
  theme::setText(uvWin, b);

  // HOURLY
  for (int i = 0; i < 8; ++i) {
    if (i < w.nHours) {
      theme::setText(hc[i].label, w.hours[i].label);
      snprintf(b, sizeof(b), "%.0f\xC2\xB0", w.hours[i].tempC);
      theme::setText(hc[i].t, b);
      if (w.hours[i].uv > 0) {
        snprintf(b, sizeof(b), "UV%d", w.hours[i].uv);
        badgeText(hc[i].uv, b);
        lv_obj_clear_flag(hc[i].uv, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(hc[i].uv, LV_OBJ_FLAG_HIDDEN);
      }
    } else {
      theme::setText(hc[i].label, "");
      theme::setText(hc[i].t, "");
      lv_obj_add_flag(hc[i].uv, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // 3-DAY range bars
  float gmin = 1e9, gmax = -1e9;
  for (int i = 0; i < w.nDays; ++i) {
    if (w.days[i].tMin < gmin) gmin = w.days[i].tMin;
    if (w.days[i].tMax > gmax) gmax = w.days[i].tMax;
  }
  float span = (gmax > gmin) ? (gmax - gmin) : 1.0f;
  for (int i = 0; i < 3; ++i) {
    if (i < w.nDays) {
      const domain::FcDay& d = w.days[i];
      theme::setText(dr[i].name, d.name);
      snprintf(b, sizeof(b), "%.0f\xC2\xB0", d.tMin);
      theme::setText(dr[i].lo, b);
      snprintf(b, sizeof(b), "%.0f\xC2\xB0", d.tMax);
      theme::setText(dr[i].hi, b);
      theme::setText(dr[i].cond, d.cond);
      int x0 = (int)((d.tMin - gmin) / span * (TRACK_W - 4));
      int x1 = (int)((d.tMax - gmin) / span * (TRACK_W - 4));
      int wdt = x1 - x0;
      if (wdt < 6) wdt = 6;
      lv_obj_set_size(dr[i].fill, wdt, 12);
      lv_obj_set_pos(dr[i].fill, x0, 0);
      lv_obj_clear_flag(dr[i].track, LV_OBJ_FLAG_HIDDEN);
    } else {
      theme::setText(dr[i].name, "");
      theme::setText(dr[i].lo, "");
      theme::setText(dr[i].hi, "");
      theme::setText(dr[i].cond, "");
      lv_obj_add_flag(dr[i].track, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

}  // namespace weather
}  // namespace ui
