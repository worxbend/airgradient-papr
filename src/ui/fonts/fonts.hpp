// src/ui/fonts/fonts.hpp
// Declarations for the committed, generated fonts (see tools/fontgen.sh).
#pragma once
#include <lvgl.h>

LV_FONT_DECLARE(font_units_18);   // ASCII + µ ³ ° ·  (restores "µg/m³", "°C")
LV_FONT_DECLARE(font_num_40);     // bold value numerals, 40 px
LV_FONT_DECLARE(font_num_64);     // bold numerals + °, 64 px (weather temp)
LV_FONT_DECLARE(font_num_110);    // bold numerals + °, 110 px (hero UV/temp)
LV_FONT_DECLARE(font_status_22);  // bold status word, mini cards
LV_FONT_DECLARE(font_status_30);  // bold status word, hero
