# 03 · E-paper Deep Dive

← [02 · Power model](02-power-model.md) · Next: [04 · Module reference →](04-module-reference.md)

---

This is the trickiest hardware in the project. Get comfortable here and the rest
is easy.

## How the panel works (physically)

The ED047TC1 is a **grayscale e-paper** panel: 960×540 pixels, **16 gray
levels** (from black to white). Each pixel is a microcapsule of charged black &
white particles suspended in fluid. Apply a voltage pattern — a **waveform** —
and the particles migrate to show a shade. Cut the power and they stay put. That
"stay put" is why the picture survives with the board asleep or unplugged.

Two facts drive all our code:

- **You must drive high voltage to change a pixel.** The LilyGo board has a
  boost circuit that produces those rails; `epd_poweron()` / `epd_poweroff()`
  turn it on and off. We keep it on only during the ~1 s of an actual draw.
- **Full, high-quality repaint = GC16.** The library's `epd_draw_grayscale_image`
  runs a ~15-frame GC16-class waveform that repaints all 16 levels. It looks
  crisp and clears ghosting, but it flashes and takes time. There is *no* fast
  "just nudge a few pixels" mode in our pinned driver (see below).

## The driver we actually have (and what it lacks)

We pin **LilyGo-EPD47** at git `#5b1adc7`. On the classic ESP32 (WROVER-E) it
compiles the **I²S/RMT parallel driver** — the correct low-level path for this
chip. Important: this fork exposes only the **classic low-level API**, *not* the
newer epdiy high-level one:

| We have | We do **not** have |
|---|---|
| A self-managed 4 bpp framebuffer | `epd_hl_update_area` (high-level) |
| `epd_draw_grayscale_image(area, data)` (GC16) | `MODE_DU` / `MODE_GL16` fast modes |
| `epd_clear_area(area)` | A waveform-temperature setter |
| `epd_poweron/off`, `epd_poweroff_all` | Automatic partial-update management |

So we build our own framebuffer management and refresh discipline on top. That's
`EpdGuard`.

## The framebuffer: 4 bits per pixel, packed two-per-byte

16 gray levels need 4 bits. So one full screen is:

```
960 × 540 pixels × 4 bits = 960/2 × 540 = 259,200 bytes  (~253 KB)
```

That's why the framebuffer lives in **PSRAM**, not internal RAM. Two pixels
share one byte:

```
  byte = [ high nibble ][ low nibble ]
           odd x pixel    even x pixel
```

- **even x** → low nibble = `luminance >> 4`
- **odd x**  → high nibble = `luminance & 0xF0`

`0xF` nibbles mean white, so a fresh buffer is `memset(fb, 0xFF, …)`. This exact
packing is implemented in `epd_lvgl_port.cpp`'s `flushCb` and was verified
against the driver's own `epd_draw_pixel` (see DECISIONS "Pixel packing").

## `EpdGuard` — the one door every pixel goes through

`src/adapters/epd_guard.{hpp,cpp}` is deliberately the **only** place that talks
to the panel. Centralising it means power discipline, safety clamps, and refresh
policy live in exactly one auditable spot. It owns:

- **`fb_`** — the full 4 bpp framebuffer (PSRAM).
- **`scratch_`** — an extraction buffer for partial pushes.
- **Power discipline** — `present()` wraps every draw in `epd_poweron()` …
  `epd_poweroff()`.
- **A temperature clamp** — refuse to drive outside a panel-safe ambient range.
- **A heap gate** — refuse to draw when internal RAM is dangerously low.
- **Boot hygiene** — clear on an unclean shutdown; optionally preserve on a
  clean battery wake.
- **Lifetime counters** — `fullRefreshes()` / `partialRefreshes()` for `/health`.

### `present()` — the decision function

```cpp
void EpdGuard::present(bool forceFull) {
  if (!dirty_ && !forceFull) return;                 // nothing to do
  if (tempC out of [kMinSafeTempC, kMaxSafeTempC]) return;   // safety clamp
  if (ESP.getFreeHeap() < kMinHeapForRefresh) return;        // heap gate ← key!
  epd_poweron();                                     // rails ON
  fullRefresh();                                     // GC16 clear + draw
  epd_poweroff();                                    // rails OFF
  dirty_ = false;
}
```

Note: the project currently does a **full refresh for every update** (partial
updates are disabled by request — they proved fragile on this panel). The
partial-refresh machinery still exists in the guard and is documented below, but
`present()` calls `fullRefresh()`.

### The named safety constants (in `epd_guard.hpp`)

- **`kMinSafeTempC` / `kMaxSafeTempC` (0 / 50 °C)** — the GC16 waveform is tuned
  for this ambient range. Outside it we *hold the last image* (free on e-paper)
  and retry later, rather than risk a bad image or DC imbalance. Ambient temp
  comes from the AirGradient reading via `setAmbientTempC()`. (This fork can't
  feed temperature *into* the waveform, so "refuse-and-hold" is the safe play.)
- **`kMinHeapForRefresh` (55,000 bytes)** — read the next section; this one
  prevents a hard hang.

## ⚠️ The white-screen-of-death (the most important gotcha)

**Symptom we hit:** after adding gauges + fonts, the panel froze on a white
screen the moment real data arrived.

**Root cause:** `epd_draw_grayscale_image` spawns **two 8 KB FreeRTOS worker
tasks per frame**. If internal RAM is momentarily low — say a TLS handshake or a
weather fetch is holding a big buffer — that `xTaskCreate` **fails**, and the
draw then **blocks forever on a semaphore**, right after `epd_poweron()`. The
boot refresh worked only because heap was still high then.

**The fixes, layered (belt and suspenders):**

1. **Move LVGL's memory to PSRAM.** `lv_conf.h` sets a 640 KB TLSF pool carved
   from PSRAM (`LV_MEM_POOL_ALLOC = heap_caps_malloc(.., MALLOC_CAP_SPIRAM)`).
   All LVGL objects/layers/render buffers live in PSRAM, so internal heap
   `min_free` went from ~36 KB to ~112 KB. Now the panel driver's per-frame
   tasks always allocate.
2. **The heap gate.** `present()` returns early (keeping the region dirty, to
   retry) if internal free heap `< kMinHeapForRefresh` (55 KB). A refresh can
   never run during a low-heap window; the periodic interval refresh retries
   moments later.
3. **Splash + fetch-then-render.** A boot splash shows while the network task
   fetches all sources; the pages render only once data is ready (or a 45 s
   timeout). This keeps the heavy boot fetch burst off the render path.

**If you ever see a white-screen hang again:** check `min_free_heap` on
`/health`. If it's near or below 55 KB during your new feature, you've
reintroduced the pressure. Move buffers to PSRAM or free them before drawing.

## Partial refresh (present but disabled)

The guard *can* push only a dirty sub-rectangle (`partialRefresh()`): it snaps
the x-range to even boundaries (nibble alignment), copies the region into
`scratch_`, `epd_clear_area()`s it (wipes ghosting there), then redraws. It also
tracks a **partial budget** (`kPartialBudget = 6`) and a **15-minute full-refresh
backstop** (`kFullBackstopMs`) so ghosting never accumulates. This path is kept
for reference and possible future use; today `present()` always goes full.

Interestingly, the "everything redraws every poll" symptom was *not* a display
limitation — it was `lv_label_set_text` invalidating labels unconditionally. The
cure lives in the UI layer: `theme::setText()` compares against the current text
and only re-sets (and thus invalidates) when it actually changed. See
[chapter 04](04-module-reference.md) and DECISIONS "Render optimization".

## Ghosting & the repair routine

E-paper accumulates faint "ghosts" of prior images. Two defences:

- Every draw is a full GC16 clear+redraw, which inherently de-ghosts.
- `EpdGuard::repairRoutine(cycles)` alternates full black/white GC16 frames to
  shake loose stubborn stuck particles — a manual "deep clean".

## The LVGL bridge (`epd_lvgl_port.cpp`)

LVGL renders into small **partial buffers** (960×54 px, L8 = 1 byte/px, ~1/10
screen) allocated in PSRAM. Its `flushCb`:

1. Converts each L8 (luminance) pixel into our packed 4 bpp framebuffer.
2. Calls `guard->markDirty(...)` to grow the dirty rectangle.
3. On the **last** flush of a refresh (`lv_display_flush_is_last`), calls
   `guard->present(false)` to push to the panel.
4. Calls `lv_display_flush_ready(disp)`.

That "only push on the last flush" is what turns LVGL's many small band-renders
into a single panel update.

Next: [04 · Module reference →](04-module-reference.md)
