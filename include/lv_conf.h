/**
 * lv_conf.h — pinned LVGL 9.x config for airdeck-papr.
 *
 * Strategy (PLAN.md §D2): render in L8 (1 byte/px luminance), then the
 * flush_cb converts to epdiy's 4 bpp framebuffer. LV_COLOR_DEPTH 8 makes L8
 * the native render format, which is the community-proven path for e-paper.
 *
 * Anything not defined here falls back to the LVGL default in
 * lv_conf_internal.h, so this file only pins what matters for us.
 */

/* clang-format off */
#if 1 /*Set to "1" to enable content*/

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/* 8-bit grayscale render target -> LV_COLOR_FORMAT_L8. */
#define LV_COLOR_DEPTH 8

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/

/* LVGL manages its own TLSF pool carved out of PSRAM (§P13): this keeps the
 * scarce internal RAM free for Wi-Fi and the epdiy per-frame worker tasks —
 * otherwise the object tree + render layers starve internal heap and the
 * panel driver's xTaskCreate hangs on a semaphore. */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB
#define LV_MEM_SIZE            (640 * 1024U)   /* pool size, from PSRAM */
#define LV_MEM_POOL_INCLUDE    "esp_heap_caps.h"
#define LV_MEM_POOL_ALLOC(sz)  heap_caps_malloc(sz, MALLOC_CAP_SPIRAM)

/*====================
   HAL / TICK SETTINGS
 *====================*/

/* Tick is provided at runtime via lv_tick_set_cb(millis). */
#define LV_DEF_REFR_PERIOD 33   /* ms; e-paper is driven manually anyway */

/* Default DPI (px per inch). 960x540 on a 4.7" panel ~= 235 dpi. */
#define LV_DPI_DEF 130

/*========================
   RENDERING CONFIGURATION
 *========================*/

/* Keep flush row stride == area width so our L8->4bpp packing is trivial. */
#define LV_DRAW_BUF_ALIGN        4
#define LV_DRAW_BUF_STRIDE_ALIGN 1

/* Single software draw unit is plenty for a 30 s cadence. */
#define LV_DRAW_SW_DRAW_UNIT_CNT    1
#define LV_DRAW_SW_SUPPORT_RGB565   1
#define LV_DRAW_SW_SUPPORT_RGB888   1
#define LV_DRAW_SW_SUPPORT_L8       1
#define LV_DRAW_SW_SUPPORT_AL88     1
#define LV_DRAW_SW_SUPPORT_A8       1
#define LV_DRAW_SW_SUPPORT_I1       1

/*=================
   OPERATING SYSTEM
 *=================*/
#define LV_USE_OS   LV_OS_NONE

/*=======================
   FEATURE CONFIGURATION
 *=======================*/
#define LV_USE_LOG 0

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/*==================
 *   FONT USAGE
 *===================*/
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_48 1

#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* Render glyphs with anti-aliasing (4 bpp), which we quantize in flush. */
#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 0

/*=================
 *  TEXT SETTINGS
 *=================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8

/*==================
 *  WIDGET USAGE
 *================*/
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BUTTON     1
#define LV_USE_CANVAS     1
#define LV_USE_CHART      1
#define LV_USE_IMAGE      1
#define LV_USE_LABEL      1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_USE_LINE       1
#define LV_USE_SCALE      1
#define LV_USE_TABLE      0

/*==================
 * THEME USAGE
 *================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0
#define LV_THEME_DEFAULT_GROW 0
#define LV_USE_THEME_SIMPLE  1
#define LV_USE_THEME_MONO    1

/*==================
 * EXAMPLES / DEMOS
 *================*/
#define LV_BUILD_EXAMPLES 0

#endif /*LV_CONF_H*/
#endif /*End of "Content enable"*/
