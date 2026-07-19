// src/ui/ui.hpp
// Public UI surface + page navigation. All of these MUST be called from the UI
// task only (LVGL is not thread-safe — PLAN.md §D5).
#pragma once
#include "app/snapshot.hpp"

namespace ui {

enum Page { PAGE_AQ = 0, PAGE_WEATHER, PAGE_CURRENCY, PAGE_COUNT };

// Build all pages + the boot message screen (after the display exists).
void build();

// Full-screen status message (boot / connecting) shown until data arrives.
void showMessage(const char* title, const char* line1, const char* line2);

// Update every page from the latest snapshot; the active one is on the panel.
void update(const app::Snapshot& s);

// Button navigation (left = prev, mid = main/AQ, right = next).
void nextPage();
void prevPage();
void gotoMain();
int  currentPage();

// Force a clean full redraw of the currently shown page (interval refresh).
void refreshCurrent();

}  // namespace ui
