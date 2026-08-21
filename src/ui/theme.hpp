// src/ui/theme.hpp
// Small shared builders for the strict-B/W look, used by the secondary pages.
#pragma once
#include <lvgl.h>
#include <string.h>

namespace ui {
namespace theme {

// Panel geometry. These mirror EPD_WIDTH/EPD_HEIGHT of the LILYGO T5 4.7"
// display, which the driver re-exports as adapters::EpdGuard::W/H. They are
// repeated here rather than included because that header pulls in Arduino and
// the e-paper driver, and theme.hpp is included by every page — so a page that
// only wants to place a card would end up dragging in the display driver.
// If the board ever changes, these two numbers and EpdGuard's must move
// together.
constexpr int kScreenW = 960;
constexpr int kScreenH = 540;
// Every page insets its cards by this much on the left and the right, so a
// full-width element is the screen minus both margins.
constexpr int kMargin = 8;
constexpr int kContentW = kScreenW - 2 * kMargin;
constexpr int kCenterX = kScreenW / 2;
constexpr int kCenterY = kScreenH / 2;

// Set label text ONLY when it actually changed. lv_label_set_text always
// invalidates the object, so blindly re-setting every label each refresh
// dirties the whole screen and forces a full e-paper redraw. Comparing first
// keeps the dirty region limited to values that really changed -> small partial
// updates.
inline void setText(lv_obj_t* label, const char* s) {
  const char* cur = lv_label_get_text(label);
  if (!cur || strcmp(cur, s) != 0) lv_label_set_text(label, s);
}

inline lv_obj_t* screen() {
  lv_obj_t* s = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s, 0, 0);  // absolute child coords == screen coords
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

inline lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font,
                       lv_color_t color = lv_color_black()) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

inline lv_obj_t* card(lv_obj_t* parent, int x, int y, int w, int h,
                      int stroke = 2) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, lv_color_black(), 0);
  lv_obj_set_style_border_width(o, stroke, 0);
  lv_obj_set_style_radius(o, 10, 0);
  lv_obj_set_style_pad_all(o, 12, 0);
  lv_obj_set_style_text_color(o, lv_color_black(), 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
  return o;
}

// Inverted (solid black) pill with white caps text — the page title tab.
// The default geometry is the title-tab size; the smaller UV badges on the
// weather page pass their own radius and padding.
inline lv_obj_t* pill(lv_obj_t* parent, const lv_font_t* font, int radius = 8,
                      int padHor = 14, int padVer = 5) {
  lv_obj_t* p = lv_obj_create(parent);
  lv_obj_set_size(p, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(p, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(p, radius, 0);
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_style_pad_hor(p, padHor, 0);
  lv_obj_set_style_pad_ver(p, padVer, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* t = label(p, font, lv_color_white());
  lv_obj_center(t);
  lv_obj_set_user_data(p, t);
  return p;
}

// The compact variant of the same widget: the per-hour UV chips on the weather
// page. Same construction, tighter geometry — named so the call site says what
// it is building rather than spelling out three unexplained numbers.
inline lv_obj_t* badge(lv_obj_t* parent, const lv_font_t* font) {
  return pill(parent, font, /*radius=*/6, /*padHor=*/7, /*padVer=*/2);
}

inline void pillText(lv_obj_t* p, const char* txt) {
  // Same unchanged-text guard as setText: an unconditional set invalidates the
  // label every call, and with partial updates disabled any dirty label costs a
  // whole-panel GC16 at the next allowed slot.
  setText((lv_obj_t*)lv_obj_get_user_data(p), txt);
}

// A page-header bar: title pill (left) + a small right-aligned status label
// (returned via *rightOut) + a 2 px rule under it.
inline lv_obj_t* header(lv_obj_t* parent, const lv_font_t* pillFont,
                        const char* title, lv_obj_t** rightOut) {
  lv_obj_t* p = pill(parent, pillFont);
  pillText(p, title);
  lv_obj_align(p, LV_ALIGN_TOP_LEFT, 8, 8);

  lv_obj_t* rule = lv_obj_create(parent);
  lv_obj_set_size(rule, kContentW, 3);
  lv_obj_set_pos(rule, kMargin, 48);
  lv_obj_set_style_bg_color(rule, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_set_style_radius(rule, 0, 0);

  if (rightOut) {
    *rightOut = label(parent, &lv_font_montserrat_18);
    lv_obj_align(*rightOut, LV_ALIGN_TOP_RIGHT, -10, 14);
  }
  return p;
}

}  // namespace theme
}  // namespace ui
