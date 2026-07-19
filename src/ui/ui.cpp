// src/ui/ui.cpp — page manager, splash screen, navigation.
//
// Boot flow: show a splash while the net task fetches AG + weather + currency;
// once all data is ready (or a timeout elapses) render the pages. Every page
// update is a full e-paper refresh (partial updates disabled by request).
#include "ui/ui.hpp"

#include <Arduino.h>
#include <lvgl.h>

#include "ui/fonts/fonts.hpp"
#include "ui/pages.hpp"
#include "ui/theme.hpp"

namespace ui {
namespace {

lv_obj_t* pages[PAGE_COUNT] = {nullptr};
lv_obj_t* splash = nullptr;
lv_obj_t* splashStatus = nullptr;

int cur = PAGE_AQ;
bool ready = false;
uint32_t splashStart = 0;
constexpr uint32_t kSplashTimeoutMs = 45000;

lv_obj_t* pageDots[PAGE_COUNT][PAGE_COUNT] = {};

void addPaging(int page, lv_obj_t* screen) {
  int base = -((PAGE_COUNT - 1) * 18) / 2;
  for (int i = 0; i < PAGE_COUNT; ++i) {
    lv_obj_t* d = lv_obj_create(screen);
    lv_obj_set_size(d, 10, 10);
    lv_obj_align(d, LV_ALIGN_TOP_MID, base + i * 18, 14);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(d, lv_color_black(), 0);
    lv_obj_set_style_border_width(d, 2, 0);
    lv_obj_set_style_bg_color(d, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(d, 0, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    pageDots[page][i] = d;
  }
}

void updatePaging() {
  for (int i = 0; i < PAGE_COUNT; ++i)
    if (pageDots[cur][i])
      lv_obj_set_style_bg_color(
          pageDots[cur][i], (i == cur) ? lv_color_black() : lv_color_white(), 0);
}

void loadCurrent() {
  if (lv_screen_active() != pages[cur]) lv_screen_load(pages[cur]);
  updatePaging();
  lv_obj_invalidate(pages[cur]);  // full refresh on switch
}

void buildSplash() {
  splash = theme::screen();

  // Logo mark: a ring with an inner dot (a stylised sensor).
  lv_obj_t* ring = lv_obj_create(splash);
  lv_obj_set_size(ring, 92, 92);
  lv_obj_align(ring, LV_ALIGN_CENTER, 0, -132);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ring, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(ring, lv_color_black(), 0);
  lv_obj_set_style_border_width(ring, 5, 0);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* dot = lv_obj_create(ring);
  lv_obj_set_size(dot, 34, 34);
  lv_obj_center(dot);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(dot, 0, 0);

  lv_obj_t* title = theme::label(splash, &lv_font_montserrat_48);
  theme::setText(title, "AIRDECK");
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -32);

  lv_obj_t* rule = lv_obj_create(splash);
  lv_obj_set_size(rule, 320, 3);
  lv_obj_align(rule, LV_ALIGN_CENTER, 0, 6);
  lv_obj_set_style_bg_color(rule, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_set_style_radius(rule, 0, 0);

  lv_obj_t* sub = theme::label(splash, &lv_font_montserrat_18);
  theme::setText(sub, "AIR QUALITY    WEATHER    CURRENCY");
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 34);

  splashStatus = theme::label(splash, &lv_font_montserrat_20);
  theme::setText(splashStatus, "Starting");
  lv_obj_align(splashStatus, LV_ALIGN_CENTER, 0, 140);
}

void setSplashStatus(const app::Snapshot& s) {
  auto mark = [](bool ok) { return ok ? LV_SYMBOL_OK : "..."; };
  char b[96];
  snprintf(b, sizeof(b), "Air %s     Weather %s     Rates %s",
           mark(s.m.valid), mark(s.weather.valid), mark(s.currency.valid));
  theme::setText(splashStatus, b);
}

}  // namespace

void update(const app::Snapshot& s) {
  // Keep every page's model current; only the active screen touches the panel.
  aq::update(s);
  weather::update(s);
  currency::update(s);

  if (!ready) {
    setSplashStatus(s);
    bool allData = s.m.valid && s.weather.valid && s.currency.valid;
    bool timedOut = (millis() - splashStart) > kSplashTimeoutMs;
    if (allData || timedOut) {
      ready = true;
      loadCurrent();  // switch from splash to the main page
    }
  }
}

void build() {
  pages[PAGE_AQ] = aq::build();
  pages[PAGE_WEATHER] = weather::build();
  pages[PAGE_CURRENCY] = currency::build();
  for (int p = 0; p < PAGE_COUNT; ++p) addPaging(p, pages[p]);

  buildSplash();

  app::Snapshot empty;
  update(empty);          // seed page fallbacks
  ready = false;          // (update() may have flipped it on the empty seed)
  cur = PAGE_AQ;
  splashStart = millis();
  lv_screen_load(splash);
}

void showMessage(const char* title, const char* line1, const char* line2) {
  (void)title;
  (void)line2;
  if (splashStatus) theme::setText(splashStatus, line1 ? line1 : "");
  if (lv_screen_active() != splash) lv_screen_load(splash);
}

void nextPage() {
  if (!ready) return;
  cur = (cur + 1) % PAGE_COUNT;
  loadCurrent();
}
void prevPage() {
  if (!ready) return;
  cur = (cur - 1 + PAGE_COUNT) % PAGE_COUNT;
  loadCurrent();
}
void gotoMain() {
  if (!ready) return;
  cur = PAGE_AQ;
  loadCurrent();
}
int currentPage() { return cur; }

void refreshCurrent() {
  if (ready) lv_obj_invalidate(pages[cur]);
}

}  // namespace ui
