# DECISIONS — airdeck-papr

Lockfile notes and the deviations/gotchas discovered during bring-up.

> **New here?** Read the [Developer Handbook](handbook/README.md) first — it's
> the book-style, from-first-principles guide to the whole firmware. This file
> is the terse forensic log of *why each version is pinned* and *how each gotcha
> was fixed*; the handbook explains *how the system works*.
>
> Source comments and this file cite a historical `PLAN.md` with section markers
> (e.g. `§6.1.4`). That original design spec is not in the repo; its still-useful
> content now lives in the handbook, which **supersedes it**. The `§` numbers are
> kept only as stable cross-references.

## Pinned toolchain / libraries

| Component | Pin | Notes |
|---|---|---|
| PlatformIO platform | `espressif32@6.7.0` | Arduino core 2.0.16 — LilyGo-EPD47 needs core **<3.0** (PLAN §P1) |
| LilyGo-EPD47 | git `#5b1adc7` | Pinned SHA. On the classic ESP32 target it compiles the **I2S/RMT parallel driver** (`i2s_data_bus.c`, `rmt_pulse.c`) — the correct path for the WROVER-E. |
| LVGL | `9.2.2` | `^9.2` resolved to 9.5.0 whose ARM **Helium `.S`** kernels break the Xtensa assembler; pinned to 9.2.2 (PLAN §7 "pin LVGL minor version"). |
| ArduinoJson | `^7.2.0` → 7.4.3 | v7 `JsonDocument` + `Filter`. |

## Bring-up gotchas (fixed)

### 1. 16 MB flash + a silent reset loop — `board_upload.flash_size`
Setting only `board_build.flash_size = 16MB` (with `default_16MB.csv`) left the
**flashed bootloader header advertising 4 MB**. The bootloader then rejected the
16 MB partition table (partitions extend past the declared flash size) and
reset-looped — *before any UART output*, so it looked like an app crash. ROM
banner → `entry 0x400805e4` → immediate `rst:0x3 (SW_RESET)`, repeating.

**Fix:** also set `board_upload.flash_size = 16MB` (and
`board_upload.maximum_size`) so esptool patches the bootloader header to match.
Isolated with the `diag` env: bare `esp32dev` booted; adding the 16 MB partition
table without the matching upload size reproduced the loop; adding
`board_upload.flash_size` fixed it. Chip confirmed via esptool:
`ESP32-D0WD-V3 rev3`, 16 MB flash (device `4018`), 40 MHz crystal, ~4 MB PSRAM.

### 2. LVGL 9.5 Helium assembly on Xtensa
`lv_blend_helium.S` is preprocessed through the LVGL/`stdint.h` include chain;
the Xtensa `as` chokes on the `typedef`s (`unknown opcode ... 'typedef'`). The
kernel bodies are guarded out on non-ARM anyway. Pinned LVGL to 9.2.2 **and**
added `tools/pio_strip_lvgl_asm.py` (a pre-build hook) that blanks any leftover
`.S` files — belt-and-suspenders so a future minor bump can't reintroduce it.

### 3. LilyGo-EPD47 has no `epd_hl_*` high-level API
PLAN §D1/§D3 assumed the epdiy high-level API (`epd_hl_update_area`, `MODE_DU`,
`MODE_GC16`). The pinned LilyGo fork only exposes the **classic low-level API**:
a self-managed 4 bpp framebuffer (`EPD_WIDTH/2 * EPD_HEIGHT` bytes, `0xFF` =
white) drawn via `epd_draw_grayscale_image(area, data)` (15-frame GC16-class
waveform) and `epd_clear_area(area)`. `DrawMode_t` is only
`BLACK_ON_WHITE / WHITE_ON_WHITE / WHITE_ON_BLACK` — there is no DU/GL16 enum.

**Adaptation (`adapters/epd_guard.cpp`):** the guard owns the full framebuffer;
partial refresh extracts the (even-x-aligned) dirty sub-rectangle into a scratch
buffer, `epd_clear_area()`s it (wipes ghosting), then redraws it. The
partial-budget (N=6) + 15-min backstop + boot-hygiene semantics from PLAN §6 are
preserved. The panel has no exposed temperature-set API in this fork, so the
0–50 °C **clamp** (refuse-and-hold) is enforced but the waveform temperature is
not fed to the driver.

### 4. Pixel packing (verified against `epd_draw_pixel`)
4 bpp, 2 px/byte: even x → low nibble = `color >> 4`; odd x → high nibble =
`color & 0xF0`. L8 luminance maps directly (`0` black … `255` white →
`v >> 4`).

## Deliberate MVP simplifications

- **Fonts:** the hero value is 40 px (not the 96 px in PLAN §3) — a direct user
  call after seeing it on the panel; the space it freed shows a large clock +
  date instead. Custom fonts (`tools/fontgen.sh` via `lv_font_conv`) provide the
  `µg/m³`/`°C` glyphs the built-in Montserrat lacks, plus the MDI watermark
  icons. Body text still uses built-in Montserrat.
- **Config injection:** via gitignored `include/config.hpp` (from
  `config.example.hpp`) rather than `secrets.ini` build-flag injection — avoids
  the missing-`extra_configs`-file failure mode. Either is fine per PLAN §2b.
- **Temperature to the panel:** this LilyGo fork exposes no waveform
  temperature-set API, so the guard enforces the 0–50 °C refuse-and-hold clamp
  but cannot feed ambient temp into the waveform.

## Implemented beyond the MVP core

`/health` endpoint + ArduinoOTA (T6, `adapters/net_services.cpp`), battery
deep-sleep profile (T5, `env:t5-epd47-v1-batt` + `app/power.cpp` + the
`PROFILE_BATTERY` path in `main.cpp`), and custom fonts + MDI watermark icons
(T3, `src/ui/fonts/` + `tools/fontgen.sh`). **Deferred:** the SDL simulator
(`sim/`).

## Multi-page navigation + weather/UV

- **Pages** are independent LVGL screens managed by `ui/ui.cpp`; each is built
  once and updated from the shared `Snapshot`. Only the active screen touches
  the panel; switching does a full refresh. Buttons GPIO34/35/39 are input-only
  (no internal pull-up — the board provides external ones); read active-low with
  a 35 ms debounce in `app/buttons.cpp`.
- **Weather source is wttr.in over plain HTTP**, not Open-Meteo. Open-Meteo is
  HTTPS-only and `WiFiClientSecure` consistently returned `-1` on-device (TLS
  handshake failing under the weak signal / heap). wttr.in `?format=j1` carries
  everything (temp, feels-like, humidity, wind, UV, precip, description, hi-lo,
  sunrise/sunset) with no key. Its response is **chunked** — parse via
  `http.getString()` (de-chunks) then an ArduinoJson **Filter**, never a
  streamed parse (chunk-size markers corrupt it). It renders on first hit and
  can be slow, so the fetch retries 3× with a 15 s timeout.
- **Geolocation** via ip-api.com (HTTP). Its `offset` field is unreliable
  (returned 0 for a UTC+3 IP), and wttr/ip-api give no dependable local-time
  field, so the **clock offset is a config value** (`kGmtOffsetSec`), not
  auto-detected.
- Net-task stack raised to 16 KB (was 8 KB) for the HTTP fetches.

## White-screen hang: LVGL memory moved to PSRAM (§P13)

Symptom: after adding the gauges page + extra fonts, the panel froze on a white
screen once real data arrived. Root cause (found by bracketing the flush/panel
path with logs): LVGL used `LV_STDLIB_CLIB` (internal RAM), so 6 pages of objects
+ render layers + the 39 KB weather buffer starved internal heap to ~36 KB. The
LilyGo driver's `epd_draw_grayscale_image` spawns two 8 KB FreeRTOS tasks **per
frame**; with heap that low, `xTaskCreate` fails and the draw blocks forever on a
semaphore — right after `epd_poweron`. The boot refresh worked only because heap
was still ~114 KB then.

Fix: `LV_USE_STDLIB_MALLOC = LV_STDLIB_BUILTIN` with a 640 KB TLSF pool carved
from PSRAM (`LV_MEM_POOL_ALLOC = heap_caps_malloc(.., MALLOC_CAP_SPIRAM)`). All
LVGL objects/layers now live in PSRAM; internal heap `min_free` went 36 KB → 112 KB,
so the panel driver's per-frame tasks always allocate. This is exactly PLAN §P13.

## UI always available (no data ≠ blank)

The page manager no longer gates navigation on data arrival. All pages are built
and shown at boot with their own "No data" / "--" fallbacks, and left/right/main
buttons always switch pages even with Wi-Fi down or a server unreachable. Loading
the main page at boot (while heap is high) also means the first full refresh never
races the low-heap window.

## Full-refresh only + heap gate + splash (the definitive render fix)

Partial updates on this panel proved fragile, and full refreshes colliding with
a low-heap fetch window re-triggered the epd task-creation hang (the panel
driver spawns two 8 KB tasks per frame; `xTaskCreate` fails under low heap and
blocks on a semaphore). Resolution:

- **Full refresh for every update** — `EpdGuard::present()` always does a full
  GC16 redraw; partial updates are disabled.
- **Heap gate** — `present()` returns early (keeping the region dirty, to retry)
  if internal free heap < 55 KB, so a refresh never runs during a TLS/HTTP fetch
  that momentarily holds a large buffer. The interval refresh retries shortly.
- **Splash + fetch-then-render** — a boot splash (logo + title + a per-source
  loading status) shows while the net task fetches AG + weather + currency; the
  pages are only rendered once all three are ready (or a 45 s timeout). This
  moves the heavy boot fetch burst off the rendered pages entirely.

Measured: `full_refreshes` advances steadily, `min_free_heap` ~77 KB, no hangs.

## Render optimization (partial updates)

The symptom "every refresh redraws the whole screen" was **not** a limitation of
the display port — the EPD guard already does per-dirty-region partial updates.
The cause was `lv_label_set_text` invalidating a label unconditionally, so
re-setting all labels each poll dirtied the whole screen and tripped the >60%
"mostly changed → full refresh" rule. Fixes:

- `ui::theme::setText()` compares against the current text and only re-sets (and
  thus invalidates) when it changed.
- The AQ status **dot** is gated on an actual band change (its `add_flag(HIDDEN)`
  invalidates unconditionally, and 9 dots spread across the grid would otherwise
  make the dirty union span the whole screen).

Result (measured via `/health` counters): a poll whose displayed values are
unchanged does **zero** refreshes; a poll that changes a couple of cards does a
small partial. Page switches still force one full refresh (intended).

## Gauges page

Temp / humidity / PM2.5 as `lv_scale` round gauges (270° sweep, ticks + labels)
with an `lv_line` needle via `lv_scale_set_line_needle_value`, strict B/W. Needle
moves only when the integer value changes.

## Currency page + on-device TLS

Once LVGL moved to PSRAM, internal heap stayed ~90 KB+ during a TLS handshake,
so `WiFiClientSecure` (which returned `-1` before, starved of heap) now works.
The currency page fetches over HTTPS:
- **NBU** `statdirectory/exchange?json` — USD/EUR/CNY in UAH (CNY→USD derived).
- **NBU** `NBU_Exchange/exchange_site?start=&end=&valcode=usd` — 30-day USD/UAH
  history for the chart (the `statdirectory` endpoint ignores the date range and
  returns only today; `exchange_site` honours it — 32 daily points).
- **CoinGecko** `simple/price?ids=bitcoin,ethereum` — BTC/ETH in USD.

All parsed with ArduinoJson filters, on a 30-minute cadence. The 30-day line is
an `lv_chart`; rates render as list rows.

## Font glyph coverage

The custom fonts are range-limited, which caused missing-glyph rectangles:
`font_num_*` lacked `:` (0x3A) → clock showed `HHMM`; `font_status_*` lacked `°`
(0xB0) → hourly temps showed a box. `tools/fontgen.sh` now includes 0x3A in the
numeral fonts and 0xB0 in the status fonts, and the few °-bearing text labels
were pointed at fonts that carry the glyph.

## Gauge styles

CO2 is an `lv_arc` (gray track + black indicator + white knob riding the rim);
PM2.5 is a ring of 28 dots that fill to the value with a check/warning badge in
the centre — matching the requested references, translated to strict B/W.

## UI feedback applied (from on-panel review)

- Hero value 48 → 40 px ("too big vertically"); reclaimed space now holds a
  48 px clock + date line.
- Units switched off broken built-in glyphs → custom `font_units_18` renders
  `µg/m³` and `°C` correctly; footer separators are ASCII.
- Hero status word ("Poor" etc.) repositioned so it is no longer clipped.
