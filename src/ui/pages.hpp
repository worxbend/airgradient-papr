// src/ui/pages.hpp
// Each page builds its own LVGL screen and updates from a Snapshot. The page
// manager (ui.cpp) owns the screen list, navigation and the boot message.
#pragma once
#include <lvgl.h>

#include "app/snapshot.hpp"

namespace ui {

namespace aq       { lv_obj_t* build(); void update(const app::Snapshot&); }
namespace weather  { lv_obj_t* build(); void update(const app::Snapshot&); }
namespace currency { lv_obj_t* build(); void update(const app::Snapshot&); }

}  // namespace ui
