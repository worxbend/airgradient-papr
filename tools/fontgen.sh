#!/usr/bin/env bash
# Reproducible font generation for airdeck-papr (PLAN.md §3 fonts/icons).
# Requires: node + `npm install lv_font_conv` in this tools/ dir.
#
#   cd tools && npm install lv_font_conv && ./fontgen.sh
#
# Generates committed C sources under src/ui/fonts/.
set -euo pipefail
cd "$(dirname "$0")"

CONV=node_modules/.bin/lv_font_conv
OUT=../src/ui/fonts
BOLD=Montserrat-Bold-static.ttf  # wght=700 instance (see README)

mkdir -p "$OUT"

# --- Bold value numerals: digits + space , - . / (+ ° for the big ones) —
#     reads far better than the thin built-in Montserrat on e-ink.
"$CONV" --font "$BOLD" --size 40 --bpp 4 --format lvgl --no-compress \
  --force-fast-kern-format \
  -r 0x20 -r 0x2C-0x3A -r 0xB0 \
  --lv-font-name font_num_40 -o "$OUT/font_num_40.c"
"$CONV" --font "$BOLD" --size 64 --bpp 4 --format lvgl --no-compress \
  --force-fast-kern-format \
  -r 0x20 -r 0x2C-0x3A -r 0xB0 \
  --lv-font-name font_num_64 -o "$OUT/font_num_64.c"
"$CONV" --font "$BOLD" --size 110 --bpp 4 --format lvgl --no-compress \
  --force-fast-kern-format \
  -r 0x20 -r 0x2C-0x3A -r 0xB0 \
  --lv-font-name font_num_110 -o "$OUT/font_num_110.c"

# --- Bold status words. Two sizes: 22 px (mini cards), 30 px (hero).
"$CONV" --font "$BOLD" --size 22 --bpp 4 --format lvgl --no-compress \
  --force-fast-kern-format -r 0x20-0x7F -r 0xB0 \
  --lv-font-name font_status_22 -o "$OUT/font_status_22.c"
"$CONV" --font "$BOLD" --size 30 --bpp 4 --format lvgl --no-compress \
  --force-fast-kern-format -r 0x20-0x7F -r 0xB0 \
  --lv-font-name font_status_30 -o "$OUT/font_status_30.c"

# --- Bold units/text font: ASCII + micro (µ), superscript-3 (³), degree (°),
#     middle-dot (·). Restores "µg/m³" and "°C" that built-in Montserrat lacks.
"$CONV" --font "$BOLD" --size 18 --bpp 4 --format lvgl --no-compress \
  --force-fast-kern-format \
  -r 0x20-0x7F -r 0xB0 -r 0xB3 -r 0xB5 -r 0xB7 \
  --lv-font-name font_units_18 -o "$OUT/font_units_18.c"

echo "generated bold fonts under $OUT/"
