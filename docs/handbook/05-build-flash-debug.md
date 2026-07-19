# 05 · Build, Flash & Debug

← [04 · Module reference](04-module-reference.md) · Next: [06 · Troubleshooting →](06-troubleshooting.md)

---

## Prerequisites

- **PlatformIO Core** (`pio`) — the build system. `pip install platformio` or
  the VS Code extension.
- A **LILYGO T5-4.7″ V1** board and a **USB-A → USB-C data cable**
  (⚠️ *not* C-to-C — the board has no CC pull-downs, so a C-to-C cable won't
  enumerate).
- (For fonts only) **Node.js**.

The toolchain, the pinned display driver, LVGL 9.2.2 and ArduinoJson are all
fetched automatically by PlatformIO on the first build. See `docs/DECISIONS.md`
for *why* each version is pinned.

## Step 1 — your private config

Wi-Fi credentials and the sensor address are **compile-time constants** (no
captive portal, by design). Create your private copy:

```bash
cp include/config.example.hpp include/config.hpp
$EDITOR include/config.hpp     # set SSID, password, monitor URL, timezone
```

`include/config.hpp` is **gitignored** — it holds secrets and must never be
committed. Keep it and `config.example.hpp` in sync when you add a knob (the two
share every `k*` name; a quick `diff` of the constant names should be empty).

Key fields: `kWifiSsid` / `kWifiPass`, `kMonitorUrl` (give the AirGradient a DHCP
reservation so its IP never drifts), `kGmtOffsetSec` (clock offset — auto-detect
is unreliable, so set it), and the timing/battery knobs from
[chapter 02](02-power-model.md).

## Step 2 — the PlatformIO environments

Defined in `platformio.ini`. Pick with `-e <env>`:

| Env | Purpose |
|---|---|
| **`t5-epd47-v1`** | **Default.** USB always-on profile for the received WROVER-E board. |
| **`t5-epd47-v1-batt`** | Same board, battery deep-sleep profile (`-DPROFILE_BATTERY`). |
| `t5-epd47-s3` | For a *future* T5-S3 unit — **not** the current board. |
| `native-test` | Host unit tests for `domain/` (no hardware). |
| `diag` | Bare boot diagnostic to isolate board/PSRAM issues. |

### Build & upload

```bash
pio run                              # build the default env
pio run -e t5-epd47-v1 -t upload     # build + flash over USB
pio run -e t5-epd47-v1-batt -t upload  # flash the battery profile
pio device monitor -b 115200         # serial monitor (exception decoder on)
```

> **Flash-size gotcha (already handled, don't undo it):** the env sets
> `board_upload.flash_size = 16MB` **and** `board_upload.maximum_size`. Without
> both, esptool leaves the bootloader header advertising 4 MB, the 16 MB
> partition table is rejected, and the board **reset-loops before any UART
> output**. See DECISIONS #1.

## Step 3 — the fast feedback loop (tests on your laptop)

The `domain/` layer runs on your host in ~0.2 s. Use this constantly:

```bash
pio test -e native-test
```

Any change to AQI math, banding, hero selection, or trend/ring-buffer logic
should be validated here **before** you flash anything. This is the single
biggest productivity lever in the project.

## Step 4 — developing without the real sensor

`tools/mock_airgradient.py` serves a fake `/measures/current` with slowly-varying
realistic values, so you can watch trends and hero-card switching on the panel
without the physical monitor:

```bash
python3 tools/mock_airgradient.py --port 8080        # optionally --scenario spike
# then set in config.hpp:
#   kMonitorUrl = "http://<your-dev-machine-ip>:8080/measures/current"
```

## Debugging on-device

### The `/health` endpoint (USB profile)

Once connected, the board serves JSON at `http://<board-ip>/health`. It's your
window into the running system:

```json
{ "uptime_s":…, "free_heap":…, "min_free_heap":…, "free_psram":…,
  "rssi":…, "poll_count":…, "consecutive_fail":…, "last_poll_ago_s":…,
  "full_refreshes":…, "partial_refreshes":…, "sensor_fw":"…", "ip":"…" }
```

What to watch:

- **`min_free_heap`** — the low-water mark of *internal* RAM. If it dips near or
  below the 55 KB heap gate, you risk the white-screen hang ([ch. 03](03-epaper-deep-dive.md)).
  Healthy is ~77 KB+.
- **`full_refreshes`** — should advance steadily; a poll whose values didn't
  change should add *zero* refreshes.
- **`consecutive_fail`** / **`last_poll_ago_s`** — sensor/Wi-Fi reachability.

### OTA updates

`net_services` also starts ArduinoOTA (hostname `airdeck`). After the first USB
flash you can push updates over Wi-Fi:

```bash
pio run -e t5-epd47-v1 -t upload --upload-port airdeck.local
```

### Serial logs

Every subsystem logs with a tag: `[boot]`, `[net]`, `[wx]`, `[cur]`, `[geo]`,
`[batt]`, `[epd]`, `[sleep]`, `[ota]`, `[svc]`. The monitor has the
`esp32_exception_decoder` filter on, so a crash prints a decoded backtrace.

On the **battery** profile, watch for `[epd] refresh (changed)` vs
`[epd] skip refresh (unchanged)` to confirm the power-saving skip logic is doing
its job, and `[batt] x.xx V (n%)` for the pack state.

### The `diag` env

If a board won't boot at all, flash `pio run -e diag -t upload`. It's a minimal
sketch with no libraries that confirms the chip, PSRAM, and flash independently
of the app — exactly how the 16 MB reset-loop was isolated.

## Fonts (only when changing glyphs)

The custom LVGL fonts are **committed C sources** under `src/ui/fonts/`, so a
normal build needs no font tooling. To regenerate (e.g. to add a glyph):

```bash
cd tools && npm install lv_font_conv && ./fontgen.sh
```

`fontgen.sh` drives `lv_font_conv` over the Montserrat TTFs to produce the
numeral/status/unit fonts. **Mind the character ranges** — they're limited to
save flash, which has bitten us before (a missing `:` broke the clock; a missing
`°` boxed the temperatures). See [chapter 06](06-troubleshooting.md).

## CI

`.github/workflows/ci.yml` builds the firmware and runs the host tests on every
push; `release.yml` handles tagged releases. Keep `native-test` green — it's the
cheapest guardrail you have.

Next: [06 · Troubleshooting →](06-troubleshooting.md)
