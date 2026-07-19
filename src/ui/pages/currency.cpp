// src/ui/pages/currency.cpp — exchange rates + 30-day USD/UAH chart.
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>

#include "domain/currency.hpp"
#include "ui/fonts/fonts.hpp"
#include "ui/pages.hpp"
#include "ui/theme.hpp"

namespace ui {
namespace currency {
namespace {

lv_obj_t* scr = nullptr;
lv_obj_t* hdrRight = nullptr;
lv_obj_t* chart = nullptr;
lv_chart_series_t* series = nullptr;
lv_obj_t* chartHi = nullptr;   // max label
lv_obj_t* chartLo = nullptr;   // min label

struct Row { lv_obj_t* value; };
Row rUsd, rEur, rCny, rBtc, rEth;

Row makeRow(int y, const char* code, const char* name) {
  lv_obj_t* c = theme::card(scr, 8, y, 944, 50);
  lv_obj_set_style_pad_ver(c, 6, 0);
  lv_obj_t* codeL = theme::label(c, &font_status_22);
  theme::setText(codeL, code);
  lv_obj_align(codeL, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* nameL = theme::label(c, &lv_font_montserrat_18);
  theme::setText(nameL, name);
  lv_obj_align(nameL, LV_ALIGN_LEFT_MID, 200, 0);
  Row r;
  r.value = theme::label(c, &font_num_40);
  lv_obj_align(r.value, LV_ALIGN_RIGHT_MID, 0, 0);
  return r;
}

}  // namespace

lv_obj_t* build() {
  scr = theme::screen();
  theme::header(scr, &lv_font_montserrat_20, "CURRENCY", &hdrRight);

  // 30-day USD/UAH chart.
  lv_obj_t* cCard = theme::card(scr, 8, 56, 944, 176);
  lv_obj_t* cap = theme::label(cCard, &lv_font_montserrat_16);
  theme::setText(cap, "USD/UAH - 30 DAYS");
  lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);

  chart = lv_chart_create(cCard);
  lv_obj_set_size(chart, 860, 116);
  lv_obj_align(chart, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_div_line_count(chart, 3, 0);
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_line_color(chart, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
  lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);   // hide point markers
  lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
  lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
  series = lv_chart_add_series(chart, lv_color_black(),
                               LV_CHART_AXIS_PRIMARY_Y);

  chartHi = theme::label(cCard, &font_status_22);
  lv_obj_align(chartHi, LV_ALIGN_TOP_RIGHT, 0, 22);
  chartLo = theme::label(cCard, &font_status_22);
  lv_obj_align(chartLo, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  // Rate rows.
  rUsd = makeRow(240, "USD to UAH", "US Dollar");
  rEur = makeRow(295, "EUR to UAH", "Euro");
  rCny = makeRow(350, "CNY to USD", "Chinese Yuan");
  rBtc = makeRow(405, "BTC to USD", "Bitcoin");
  rEth = makeRow(460, "ETH to USD", "Ethereum");
  return scr;
}

void update(const app::Snapshot& s) {
  const domain::Currency& c = s.currency;
  char b[32];

  theme::setText(hdrRight, c.valid && c.date[0] ? c.date : "");

  if (!c.valid) {
    theme::setText(rUsd.value, "--");
    theme::setText(rEur.value, "--");
    theme::setText(rCny.value, "--");
    theme::setText(rBtc.value, "--");
    theme::setText(rEth.value, "--");
    theme::setText(chartHi, "");
    theme::setText(chartLo, "");
    return;
  }

  snprintf(b, sizeof(b), "%.2f", c.usdUah);
  theme::setText(rUsd.value, b);
  snprintf(b, sizeof(b), "%.2f", c.eurUah);
  theme::setText(rEur.value, b);
  snprintf(b, sizeof(b), "%.3f", c.cnyUsd);
  theme::setText(rCny.value, isnan(c.cnyUsd) ? "--" : b);
  snprintf(b, sizeof(b), "%.0f", c.btcUsd);
  theme::setText(rBtc.value, isnan(c.btcUsd) ? "--" : b);
  snprintf(b, sizeof(b), "%.1f", c.ethUsd);
  theme::setText(rEth.value, isnan(c.ethUsd) ? "--" : b);

  // chart
  if (c.histCount >= 2) {
    float mn = c.hist[0], mx = c.hist[0];
    for (int i = 1; i < c.histCount; ++i) {
      if (c.hist[i] < mn) mn = c.hist[i];
      if (c.hist[i] > mx) mx = c.hist[i];
    }
    float pad = (mx - mn) * 0.1f + 0.01f;
    int lo = (int)((mn - pad) * 100);
    int hi = (int)((mx + pad) * 100);
    lv_chart_set_point_count(chart, c.histCount);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
    for (int i = 0; i < c.histCount; ++i)
      lv_chart_set_value_by_id(chart, series, i, (int)(c.hist[i] * 100));
    lv_chart_refresh(chart);
    snprintf(b, sizeof(b), "%.2f", mx);
    theme::setText(chartHi, b);
    snprintf(b, sizeof(b), "%.2f", mn);
    theme::setText(chartLo, b);
  }
}

}  // namespace currency
}  // namespace ui
