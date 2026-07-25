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
- **Power discipline** — `present()` wraps every draw in `epd_poweron()` …
  `epd_poweroff()`.
- **A refresh rate limit** — the once-per-minute floor; see below.
- **A temperature clamp** — refuse to drive outside a panel-safe ambient range.
- **Heap gates** — refuse to draw when internal RAM is low *or* fragmented.
- **A panel-busy flag** — so the net task can't allocate mid-draw.
- **Boot hygiene** — clear on an unclean shutdown; optionally preserve on a
  clean battery wake.
- **Lifetime counters** — `fullRefreshes()` and the deferral counters, for
  `/health`.

### `present()` — the decision function

```cpp
void EpdGuard::present(bool forceFull) {
  if (forceFull) pendingForce_ = true;
  if (!dirty_ && !pendingForce_) return;             // nothing to do
  if (!rateLimitAllows()) return;                    // ← once-per-minute guard
  if (tempC out of [kMinSafeTempC, kMaxSafeTempC]) return;   // safety clamp
  if (freeHeap < kMinHeapForRefresh ||
      largestInternalBlock() < kMinContiguousForRefresh) return;  // heap gates
  g_panelBusy = true;                                // net task holds off
  epd_poweron();                                     // rails ON
  fullRefresh();                                     // GC16 clear + draw
  epd_poweroff();                                    // rails OFF
  g_panelBusy = false;
  lastPresentMs_ = millis(); dirty_ = false; pendingForce_ = false;
}
```

Every gate is **defer, never drop**: `dirty_` stays set and `EpdGuard::tick()`
(called every loop iteration) re-enters `present()`, so the *newest* framebuffer
lands at the next allowed slot. Deferring costs freshness, never content.

Note: the project does a **full refresh for every update** — partial updates
proved fragile on this panel and the machinery has been removed (see below).

## How often may we refresh? (the rate-limit guard)

**What one "refresh" actually costs.** `fullRefresh()` is two driver calls:

| Call | Panel frames | Why that many |
|---|---|---|
| `epd_clear()` | **32** | `epd_clear_area_cycles(full, 4, 50)` = 4 cycles × (4 dark + 4 white) pushes |
| `epd_draw_grayscale_image()` | **15** | one frame per GC16 contrast step (`contrast_cycles_4[15]`) |

So **~47 full-panel frames per update**. On top of that, `epd_draw_image()`
creates and destroys **two 8 KB FreeRTOS tasks per frame** (30 create/delete
pairs) and sits in `vTaskDelay(5)` between frames. Measured end-to-end this is
**~1 s of blocked UI task per refresh** — read the real number from `/health` →
`last_refresh_ms` / `max_refresh_ms`.

**What the panel says.** From the ED047TC1 datasheet (`LilyGo-EPD47/datasheet/`):

| Spec | Value |
|---|---|
| Operating temperature | **0 … +50 °C** (mirrored by `kMinSafeTempC/kMaxSafeTempC`) |
| Panel power, typical / max during update | **170 mW / 950 mW** |
| Standby (image held, rails off) | **0.1 mW** |
| VCOM inrush | up to **480 mA** |
| Max screen refresh rate | 85 Hz — that is the *frame* rate inside a waveform, **not** how often you may repaint |

The datasheet specifies no minimum interval between updates and no cycle-life
figure, and E Ink only guarantees the optics "under the controller & waveform
provided by E Ink" — which this LilyGo fork is not (it has no waveform
temperature compensation either, see DECISIONS #3). So the update budget is ours
to set, and the sane basis is cost, not a datasheet limit: at the old 30 s
interval the board drove **~2 880 full-screen waveforms a day** (~135 000 panel
frames, ~48 min/day of blocked UI, a 950 mW peak each time) to move a clock that
changes once a minute.

**The guard.** `EpdGuard` enforces a hard floor between panel updates:

- **`kMinRefreshIntervalSeconds` (60 s)** — ordinary data-driven redraws. This is
  the once-per-minute rule: the framebuffer keeps accumulating changes and the
  newest image is pushed at the next allowed slot.
- **`kForcedRefreshMinSeconds` (3 s)** — user-initiated pushes only. A button
  page switch calls `ui::takePageChanged()` → `requestForce()`, so paging stays
  instant, but a mashed button coalesces instead of burning one waveform per
  press.
- **`kUiTickSeconds` (15 s)** — how often the page *models* are recomputed. Pure
  CPU; it never touches the panel. Because `theme::setText()` only invalidates on
  a real change, a tick where nothing moved leaves the framebuffer clean and
  costs **zero** refreshes.
- **`kDeGhostRefreshSeconds` (3600 s)** — repaint the page hourly even if nothing
  changed, to clear particle memory.

The first frame after boot is never rate-limited (`havePresented_ == false`), so
the splash and the splash → dashboard hand-off are immediate. On the battery
profile every wake is a fresh boot, so the wake cadence *is* the rate limit and
the guard never fires.

Watch it work on `/health`: `full_refreshes` should advance at most once a
minute, and `refresh_deferred_rate` counts the pushes the limiter held back.

### The named safety constants (in `epd_guard.hpp`)

- **`kMinSafeTempC` / `kMaxSafeTempC` (0 / 50 °C)** — the GC16 waveform is tuned
  for this ambient range. Outside it we *hold the last image* (free on e-paper)
  and retry later, rather than risk a bad image or DC imbalance. Ambient temp
  comes from the AirGradient reading via `setAmbientTempC()`. (This fork can't
  feed temperature *into* the waveform, so "refuse-and-hold" is the safe play.)
- **`kTempStaleMs` (15 min)** — the clamp's proxy has a shelf life. The reading
  is the *AirGradient's indoor air* temperature standing in for panel
  temperature: there is no thermistor on this board, and this driver fork
  exposes no waveform temperature-set API (DECISIONS #3). That proxy only holds
  if sensor and panel share a room — and if the sensor goes offline it must not
  clamp forever, so a reading older than this window is treated as *no* reading
  and the clamp opens. The clamp also logs on each transition, so "screen won't
  update" shows up in the serial log instead of being silent.
- **`kMinHeapForRefresh` (55,000 bytes)** — total internal free heap.
- **`kMinContiguousForRefresh` (20,000 bytes)** — the *largest single* internal
  block. Both matter: read the next section.

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
   never run during a low-heap window; `tick()` retries moments later.
3. **The *contiguous* heap gate.** Free heap alone is not enough — a task stack
   must come out of **one contiguous internal block**. After days of `String`
   and mbedTLS churn the heap fragments, so `free_heap` can read 60 KB while the
   largest block is a few KB; `xTaskCreate` fails anyway and the draw deadlocks.
   `present()` therefore also requires
   `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT) >=
   kMinContiguousForRefresh` (20 KB). `/health` exposes it as
   `largest_free_block` — **this is the number that predicts the hang**, not
   `free_heap`.
4. **The panel-busy interlock.** The two gates above are checked on the UI task
   *before* `epd_poweron()`, but the net task runs on the other core and can
   start a 40 KB TLS handshake *during* the 15-frame draw — a textbook
   time-of-check/time-of-use race. `adapters::panelBusy()` is true for the whole
   duration of a push, and the net task calls `waitForPanelIdle()` before every
   fetch. A refresh takes ~1 s and now happens at most once a minute, so the
   wait costs nothing.
5. **The task watchdog.** If it deadlocks anyway, the driver blocks on
   `xSemaphoreTake(..., portMAX_DELAY)` — forever, **with the rails still
   energised**. The UI task is subscribed to the ESP32 task WDT
   (`kWatchdogSeconds`, panic → reset), so that turns a "unplug it and plug it
   back in" freeze into an automatic reboot. Check `/health` →
   `reset_reason` (`6` = `ESP_RST_TASK_WDT`) or the boot banner, which prints
   the reason in words.
6. **Splash + fetch-then-render.** A boot splash shows while the network task
   fetches all sources; the pages render only once data is ready (or a 45 s
   timeout). This keeps the heavy boot fetch burst off the render path.

**If you ever see a white-screen hang again:** check `largest_free_block` and
`min_free_heap` on `/health`. If either is near its gate during your new feature,
you've reintroduced the pressure. Move buffers to PSRAM or free them before
drawing.

## Partial refresh (removed)

Earlier revisions carried a partial-refresh path (dirty-rect extraction into a
scratch buffer, an N=6 budget and a 15-minute full backstop). Partial updates
proved fragile on this panel and `present()` was switched to full-refresh-only;
the machinery then sat unreachable — along with a permanently reserved 259 KB
PSRAM scratch buffer — so it has been deleted rather than left as a trap. Git
history has it if this panel ever earns a second attempt.

## Ghosting & the repair routine

E-paper accumulates faint "ghosts" of prior images. Two defences:

- Every draw is a full GC16 clear+redraw, which inherently de-ghosts, and
  `kDeGhostRefreshSeconds` forces one hourly even when nothing changed.
- `EpdGuard::repairRoutine(cycles)` alternates full black/white GC16 frames to
  shake loose stubborn stuck particles — a manual "deep clean". Trigger it with
  **`curl http://airdeck.local/repair`**; the endpoint only raises a flag and the
  UI task runs the routine, because it drives the panel directly and would
  otherwise race a refresh in flight. It takes ~10 s (the UI task's watchdog is
  fed per cycle) and allocates its solid-colour buffer only for the duration.

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
