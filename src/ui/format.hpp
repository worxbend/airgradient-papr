// src/ui/format.hpp
// One place that turns a reading into the text a label shows.
//
// Deliberately LVGL-free and tiny so any page can include it cheaply.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace ui {
namespace fmt {

// Write `v` into `b` with `decimals` digits, or "--" when the reading is
// missing.
//
// Why the NAN check is not optional: the HTTP adapters parse each field on its
// own and store NAN for anything the upstream service left out of its reply.
// wttr.in and the currency feed both omit fields routinely, and their fetch()
// functions validate only one field before marking the whole record valid — so
// a record the page treats as good can still carry NAN in most of its numbers.
// Handing such a value to "%f" makes the C library print the literal text
// "nan", which then sits on the e-paper panel until the next good reading.
inline void value(char* b, size_t n, float v, int decimals) {
  if (std::isnan(v))
    snprintf(b, n, "--");
  else
    snprintf(b, n, "%.*f", decimals, v);
}

}  // namespace fmt
}  // namespace ui
