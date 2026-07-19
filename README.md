# airdeck-papr

Standalone Wi-Fi firmware that turns a **LILYGO T5-4.7" E-Paper (V1, ESP32-WROVER-E)**
into an offline dashboard for an **AirGradient ONE (I-9PSL)**. It polls the
monitor's local-server API over your LAN (no cloud) and renders an app-style
card grid on the 960×540 e-ink panel with LVGL 9, plus outdoor weather + UV.

### Pages & button navigation

Four pages, switched with the three front buttons:

| Button (GPIO)      | Action            |
|--------------------|-------------------|
| Leftmost (34)      | Previous page     |
| 2nd-from-left (35) | Jump to main (AQ) |
| Rightmost (39)     | Next page         |

1. **AirGradient** (default) — CO2 as an **arc gauge with a knob**, PM2.5 as a
   **dotted-ring gauge** (both have well-known thresholds), temperature /
   humidity / TVOC / NOx tiles, PM1.0 / PM10 / PM0.3 tiles, and a clock/date tile.
2. **Weather** — one compact outdoor page combining current conditions, an
   AIR panel (humidity/wind/precip/sun), a UV panel (index + band + today's max
   + high-UV window), an hourly row and a 3-day min–max range-bar forecast.
3. **Currency** — a 30-day USD/UAH line chart plus rate rows: USD→UAH, EUR→UAH
   (NBU), CNY→USD, and BTC→USD, ETH→USD (CoinGecko).

A small paging indicator (dots) at the top of each page shows the current page.

### Rendering

The panel only redraws what changed. LVGL runs in partial mode and the EPD
guard pushes just the dirty rectangle (a fast per-region update), inserting a
full GC16 clear every 6 partials or 15 min for panel health. Crucially, labels
are only re-set when their text actually changes (`ui::theme::setText`), so an
unchanged value never dirties the screen — a poll with no visible change does
**zero** panel refreshes.

Weather, UV and forecast come from **wttr.in** (plain HTTP, no key, 3-day
horizon); location is auto-detected via **ip-api.com** (override with
`kLatitude`/`kLongitude`).

See [`PLAN.md`](PLAN.md) for the full design and [`docs/DECISIONS.md`](docs/DECISIONS.md)
for the locked toolchain/versions and the gotchas hit during bring-up.

## Hardware

- **Board:** LILYGO T5-4.7" V1 — ESP32-WROVER-E (classic dual-core, 16 MB flash,
  8 MB QSPI PSRAM, ED047TC1 panel). USB-C via a **CP2104** UART bridge.
- **Sensor:** AirGradient ONE running firmware ≥ 3.0.x with the local API enabled.

## Quick start

1. **Toolchain:** PlatformIO Core (`pip install platformio`).
2. **Config:** copy the example and fill in your Wi-Fi + monitor address:
   ```bash
   cp include/config.example.hpp include/config.hpp
   $EDITOR include/config.hpp        # SSID, password, http://<monitor-ip>/measures/current
   ```
   `include/config.hpp` is gitignored — credentials never get committed.
   Give the AirGradient a **DHCP reservation** so its IP never drifts.
3. **Verify the API from your machine** first:
   ```bash
   curl http://<monitor-ip>/measures/current      # should return JSON
   ```
4. **Build + flash** (board on `/dev/ttyUSB0`):
   ```bash
   pio run -e t5-epd47-v1 -t upload
   ```
5. **Watch it boot:**
   ```bash
   pio device monitor -e t5-epd47-v1
   ```
   Expected:
   ```
   [airdeck] boot
   [boot] PSRAM: 4192123 bytes free
   [net] ok #1  CO2=385 PM2.5=29.3 T=28.8 RH=45
   ```

## Host unit tests (no hardware)

The `domain/` layer (AQI, banding, trend, hero selection) is pure C++ and
tested on the host:

```bash
pio test -e native-test
```

## Mock AirGradient (develop without the real sensor)

```bash
python3 tools/mock_airgradient.py            # serves /measures/current on :8080
# point kMonitorUrl at http://<your-dev-ip>:8080/measures/current
```

## Flashing / power notes (field-reported traps — see PLAN.md §5)

- Use a **USB-A→C data cable** or a powered hub. Some units don't enumerate on
  C-to-C (missing CC pulldowns).
- If upload fails / the port keeps re-enumerating: hold **BOOT**, tap **RST**,
  release **BOOT**, then flash.
- Never repurpose strapping pins **GPIO0/2/5/12/15**; in particular **GPIO12
  must not be pulled high at boot**.
- Check LiPo **polarity with a multimeter** before the first battery connect —
  AliExpress JST-PH pigtails are not standardized.

## Environments

| env               | target                                        |
|-------------------|-----------------------------------------------|
| `t5-epd47-v1`     | **default** — WROVER-E, USB always-on + OTA + /health |
| `t5-epd47-v1-batt`| same board, battery deep-sleep single-shot profile |
| `t5-epd47-s3`     | future ESP32-S3 unit (not this board)         |
| `native-test`     | host unit tests for `domain/`                 |
| `diag`            | minimal boot/PSRAM diagnostic sketch          |

## Homelab services (USB profile)

Once connected, the device runs:

- **`GET http://airdeck.local/health`** (or `http://<device-ip>/health`) — JSON
  with uptime, heap/PSRAM, RSSI, poll counters, last-poll age, and full/partial
  refresh counts. Scrapeable for monitoring.
- **ArduinoOTA** as host `airdeck` — reflash without USB:
  ```bash
  pio run -e t5-epd47-v1 -t upload --upload-port airdeck.local
  ```

## Battery profile

```bash
pio run -e t5-epd47-v1-batt -t upload
```
Single-shot per wake: connect → poll → render → `epd_poweroff_all()` →
deep-sleep for `kPollSeconds`. Includes a battery-ADC brownout gate (GPIO36):
below 3.4 V it renders a static "CHARGE ME" frame and refuses waveforms
(interrupting a waveform mid-drive damages the panel — PLAN §6.1.4).

## Fonts / icons

Custom fonts are committed under `src/ui/fonts/` and regenerated with
`tools/fontgen.sh` (needs `cd tools && npm install lv_font_conv`): a units font
with `µ/³/°` glyphs (so `µg/m³` and `°C` render) and an MDI watermark-icon font.

## Status / scope

Implemented and verified on-device: board + PSRAM bring-up; the e-paper display
port (LVGL 9 L8 → 4 bpp); the panel guard (power discipline, partial-refresh
budget + full-refresh backstop, temperature clamp, boot hygiene, repair
routine); Wi-Fi + HTTP + JSON data path with backoff reconnect; the two-task
mailbox architecture; the pure domain layer (host-tested); the single-screen
dashboard (hero with big clock/date + 8 cards, MDI watermark icons,
header/footer, stale/offline states); the `/health` endpoint + ArduinoOTA; and
the battery deep-sleep profile.

Deferred: the SDL desktop simulator (`sim/`). Everything else in PLAN.md's
implementation tracks is in place.
