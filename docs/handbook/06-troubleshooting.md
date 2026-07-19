# 06 · Troubleshooting

← [05 · Build, flash & debug](05-build-flash-debug.md) · [Back to index](README.md)

---

Every entry here is a real sharp edge someone hit. Symptom → cause → fix. Many
are cross-referenced to `docs/DECISIONS.md`, which has the full forensic detail.

## Boot & flashing

### Board reset-loops before any serial output
**Cause:** the bootloader header advertises 4 MB flash while the partition table
is 16 MB, so the ROM rejects it and resets (`rst:0x3 (SW_RESET)`) *before* the
app runs. **Fix:** ensure the env sets **both** `board_upload.flash_size = 16MB`
and `board_upload.maximum_size = 16777216` (already done in `platformio.ini`).
Isolate with `pio run -e diag`. (DECISIONS #1.)

### `[fatal] PSRAM not found`
**Cause:** the build is missing `-DBOARD_HAS_PSRAM` / the WROVER-E cache-errata
flag, or you flashed the wrong env. **Fix:** build `t5-epd47-v1` (or `-batt`),
which include `-DBOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue`. Confirm free
PSRAM in the `[boot] PSRAM:` log line.

### Board won't enumerate over USB
**Cause:** a **C-to-C** cable — the board lacks CC pull-downs. **Fix:** use a
USB-**A**-to-C data cable (and make sure it's a *data* cable, not charge-only).

### The upload port keeps changing (`ttyUSB0`↔`ttyUSB1`)
**Fix:** the env uses a stable `/dev/serial/by-id/...` path so re-enumeration
doesn't break uploads. Update it to match your board's CP2104 id if needed.

## Display

### White screen, frozen, right when data arrives ⚠️
**The big one.** **Cause:** `epd_draw_grayscale_image` spawns two 8 KB tasks per
frame; under low internal heap that `xTaskCreate` fails and the draw blocks
forever on a semaphore. **Fix (already in place):** LVGL memory lives in a 640 KB
PSRAM pool (`lv_conf.h`), and `EpdGuard::present()` refuses to draw below
`kMinHeapForRefresh` (55 KB), keeping the region dirty to retry. **If it
returns:** check `/health` → `min_free_heap`; if it's near 55 KB during your new
feature, move buffers to PSRAM or free them before drawing. (DECISIONS
"White-screen hang" / "Full-refresh only".)

### Every poll redraws the whole screen (slow, flashy)
**Cause:** `lv_label_set_text` invalidates unconditionally, so re-setting every
label each poll dirties the whole screen. **Fix:** use `ui::theme::setText()`,
which compares first and only re-sets on real change. Gate visibility toggles
(e.g. status dots) on an actual state change too. (DECISIONS "Render
optimization".)

### Faint ghost of the previous image
**Cause:** normal e-paper particle memory. **Fix:** full GC16 refreshes de-ghost
inherently; for stubborn cases call `EpdGuard::repairRoutine()` (alternating
black/white frames).

### Screen won't update in extreme heat/cold
**Cause:** the temperature clamp — `present()` refuses to drive the waveform
outside `kMinSafeTempC..kMaxSafeTempC` (0–50 °C) to protect the panel, and holds
the last image. **Fix:** none needed; it resumes automatically when ambient
(from the AirGradient temp) returns to range. (DECISIONS #3, §6.1.5.)

### Missing-glyph rectangles (□) in numbers or units
**Cause:** the custom fonts are range-limited to save flash; a needed codepoint
wasn't generated (historically `:` U+003A broke the clock, `°` U+00B0 boxed
temps). **Fix:** add the codepoint's range in `tools/fontgen.sh`, regenerate,
and point the label at a font that carries the glyph. (DECISIONS "Font glyph
coverage".)

## Networking

### Weather never loads / times out
**Cause:** wttr.in renders on first hit and can be slow/flaky, and it serves JSON
only to curl-like user agents; its response is **chunked**. **Fix (in place):**
`weather_http` sends a `curl/8.5.0` UA, retries 3× with a 15 s timeout, and
parses via `getString()` (which de-chunks) + a Filter — never a streamed parse,
which chokes on chunk markers. (DECISIONS "Weather source".)

### Weather/geolocation over HTTPS returns −1
**Cause:** TLS handshakes are unreliable on this radio under weak signal / heap
pressure. **Fix (by design):** weather + geolocation use **plain HTTP**
(wttr.in, ip-api.com). Only currency uses HTTPS, and only because moving LVGL to
PSRAM freed enough internal heap for `WiFiClientSecure` to handshake. (DECISIONS
"Weather source" / "Currency page".)

### Clock is wrong / off by hours
**Cause:** wttr.in and ip-api.com don't give a dependable local-time offset. **Fix:**
the clock offset is a **config value**, `kGmtOffsetSec` — set it for your locale.

### Clock shows `--:--` for a while after boot
**Cause:** NTP sync is asynchronous; until it lands, `time()` reads ~1970 and
`app::timeIsSynced()` returns false, so time-dependent UI (clock, currency
history, weather "now" slot) waits. **Fix:** none — it fills in within seconds of
a good connection.

### Currency history/chart is empty
**Cause:** the 30-day NBU history needs a real date range, so it's gated on
`app::timeIsSynced()`. Also note the plain `statdirectory` endpoint ignores dates
(returns only today); the chart uses `NBU_Exchange/exchange_site`, which honors
them. **Fix:** wait for NTP; verify the board can reach `bank.gov.ua` over HTTPS.

## Battery profile

### LiPo drains in about a day
**Cause:** waking too often. **Fix:** the battery profile uses
`kBatteryPollSeconds` (default 900 s), **not** the 30 s USB `kPollSeconds`.
Increase it for longer runtime. (See [ch. 02](02-power-model.md).)

### Panel goes blank/white on the battery profile
**Cause:** a cold boot or unclean shutdown clears the panel by design (boot
hygiene). If it happens on *every* timer wake, the clean-shutdown flag isn't
being written — check that `sleepFor()` calls `markCleanShutdown()` before
sleeping and that NVS is writable. **Fix:** clean wakes pass `clearPanel=false`
so the image is preserved; only cold/unclean boots clear.

### Battery panel shows the splash, never the dashboard
**Cause (fixed):** the "all three sources ready" gate can never be satisfied on
battery (it only fetches AirGradient). **Fix:** the battery path calls
`ui::showMainNow()`, which bypasses the gate and loads the main page. If you fork
the battery flow, keep that call.

### `CHARGE ME` screen appears
**Cause:** pack below `kBatteryLowV` (3.4 V). Refusing to drive a waveform on a
sagging supply protects the panel from DC-imbalance damage. **Fix:** charge it.
It re-checks every `kBatteryLowSleepSeconds` and force-redraws the dashboard once
recovered.

### Battery reads 0 V / 0 %
**Cause:** no divider populated, or you're on USB (the code treats `vbat ≤ 0.5 V`
as "no battery" and skips the brownout gate). **Fix:** expected on USB; on
battery, verify the divider and that sensing is on **GPIO36 (ADC1)** — ADC2 pins
don't work while Wi-Fi is on.

## General method

When something's wrong, in order:

1. **`pio device monitor`** — read the tagged logs and any decoded backtrace.
2. **`/health`** (USB) — `min_free_heap`, refresh counters, poll failures.
3. **`pio test -e native-test`** — rule the pure logic in or out in 0.2 s.
4. **`pio run -e diag`** — rule the board/PSRAM in or out.
5. **`docs/DECISIONS.md`** — the forensic history of every pinned version and
   fixed gotcha.

[Back to index](README.md)
