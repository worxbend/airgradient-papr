# 04 · Module Reference

← [03 · E-paper deep dive](03-epaper-deep-dive.md) · Next: [05 · Build, flash & debug →](05-build-flash-debug.md)

---

A file-by-file tour. For each module: what it owns, and where you'd go to make a
common change. Files are grouped by layer (inner → outer).

## `domain/` — pure logic (host-tested, no hardware)

| File | Owns | Change here to… |
|---|---|---|
| `measurement.hpp` | One AirGradient reading as plain data; `tempC()`/`humidity()` prefer compensated values; `has(v)` = "not NaN". | Add a new sensor field. |
| `aqi.hpp` | US EPA AQI (PM2.5) + `Band` enum + per-metric band functions + `bandWord`/`comfortWord`. | Retune a health/comfort threshold. |
| `metrics.hpp` | The metric catalog (`MetricInfo`: label, unit, trend epsilon, priority), `value()`, `band()`, and `heroMetric()` (worst metric now). | Add a metric to the catalog or change hero ranking. |
| `weather.hpp` | Plain-data weather/forecast/UV model. | Add a weather field. |
| `currency.hpp` | Plain-data rates + `hist[kHistMax]` history array. | Change how much history is kept. |
| `trend.hpp` | `History<N>` ring buffer (min/max/at/latest) + `Trend`/Δ logic. Templated, no heap. | Change sparkline/trend behavior. |

> **Everything here is covered by `test/test_domain/test_domain.cpp`.** If you
> touch a threshold or the hero rule, add/adjust a test and run
> `pio test -e native-test`.

## `ports/` — interfaces

| File | Owns |
|---|---|
| `metrics_source.hpp` | The `MetricsSource` interface: `poll(Measurement&)` + `lastError()`. The AirGradient adapter implements it; a fake could too. |

## `adapters/` — the outside world

| File | Owns | Notes |
|---|---|---|
| `wifi_link.{hpp,cpp}` | Connect + keep-alive with **exponential backoff** (1 s → 60 s cap), an 8 s connect window, RSSI. | `WiFi.setSleep(false)` for snappy polls. |
| `airgradient_http.{hpp,cpp}` | GET the sensor's local JSON, **filter** to modeled fields, fill a `Measurement`. Tolerant of `pm003Count` vs `pm003_count`. | Validates ≥1 core metric present. |
| `weather_http.{hpp,cpp}` | Geolocate via ip-api.com, weather/UV via **wttr.in `?format=j1`** (plain HTTP), 3× retry. Builds the hourly strip + 3-day forecast + high-UV window. | HTTP not TLS on purpose (ch. 06). Uses `getString()` (de-chunks) then a Filter. |
| `currency_http.{hpp,cpp}` | **HTTPS** to NBU (USD/EUR/CNY + 30-day history) and CoinGecko (BTC/ETH), each 3× retry via `tlsGet()`. | Works because LVGL moved to PSRAM freed internal heap for TLS. |
| `epd_guard.{hpp,cpp}` | **The single panel choke point** — framebuffer, power discipline, safety clamps, refresh policy, boot hygiene, counters. | See [chapter 03](03-epaper-deep-dive.md). |
| `epd_lvgl_port.{hpp,cpp}` | Bridges LVGL's flush callback into the guard's 4 bpp framebuffer; pushes to panel only on the last flush of a refresh. | L8 → 4 bpp packing lives here. |
| `net_services.{hpp,cpp}` | The `/health` JSON endpoint + ArduinoOTA. Runs inside the net task. **Compiled out on battery.** | `netServicesBegin()` is idempotent. |

## `app/` — policy & glue

| File | Owns | Notes |
|---|---|---|
| `snapshot.hpp` | The immutable `Snapshot` bundle (Measurement + Weather + Currency + `NetState` + rssi/fail counters). Plain-old-data, copied by value. | The one thing passed net task → UI task. |
| `buttons.{hpp,cpp}` | Debounced edge detection on GPIO 34/35/39 (input-only, external pull-ups, active-low, 35 ms debounce). Returns one `Btn` press per push. | Left=prev, Mid=main, Right=next. |
| `power.{hpp,cpp}` | Battery ADC (16-sample average on GPIO36/ADC1) + `kBatteryLowV` brownout threshold. | Battery profile only. |
| `clock.hpp` | `timeIsSynced()` + `kClockSyncedAfter` — the single home for the "is NTP up?" test. | Used by main, dashboard, weather, currency. |
| `health.{hpp,cpp}` | `g_health` cross-task counters (`volatile` scalars, atomic on ESP32). | Read by `/health`. |

## `ui/` — data becomes a picture

| File | Owns | Notes |
|---|---|---|
| `ui.{hpp,cpp}` | Page manager: builds all pages, the boot **splash**, navigation, paging dots, the "all sources ready" gate, and `showMainNow()` (battery's bypass). | LVGL calls must stay on the UI task. |
| `dashboard.cpp` | Page 1 (air): CO₂ **arc gauge** (with knob), PM2.5 **dot ring** (28 dots + badge), 2×2 tiles, PM detail row, big clock. Gauges only move when the integer value changes. | The "hero" page. |
| `pages/weather.cpp` | Page 2: current conditions + AIR + UV block + 8-slot hourly strip + 3-day forecast with range bars. | |
| `pages/currency.cpp` | Page 3: 30-day USD/UAH `lv_chart` + rate rows. | |
| `theme.hpp` | Shared widget factories (`screen`, `label`, `card`, `pill`) + the crucial **`setText()`** that only invalidates on real change. | Strict black/white styling. |
| `fonts/` | Committed LVGL C fonts: big numerals (`font_num_*`), status (`font_status_*`), unit glyphs (`font_units_18`, gives `µg/m³`/`°C`). Regenerate with `tools/fontgen.sh`. | Range-limited — mind glyph coverage (ch. 06). |

## The two front doors

- **`src/main.cpp`** — composition root. Contains **both** profiles behind
  `#if !defined(PROFILE_BATTERY)`. The USB half sets up the net task + mailbox +
  render loop; the battery half is the single-shot wake→poll→draw→sleep with the
  RTC signature/skip logic. **Read this first when onboarding.**
- **`src/diag.cpp`** — a bare-minimum boot diagnostic (its own `env:diag`) that
  isolates board/PSRAM problems with no libraries.

## Where do I change…? (quick index)

| I want to… | Go to |
|---|---|
| Retune an AQI/comfort threshold | `domain/aqi.hpp` (+ a test) |
| Add a sensor field | `domain/measurement.hpp` + `adapters/airgradient_http.cpp` + a tile in `ui/dashboard.cpp` |
| Change poll/refresh timing | `include/config.hpp` |
| Change battery runtime behavior | `include/config.hpp` + battery half of `src/main.cpp` |
| Fix a rendering glitch | `ui/*` or `adapters/epd_guard.cpp` |
| Add/adjust a page | `ui/pages/*` + register it in `ui/ui.cpp` + `ui/pages.hpp` |
| Change what `/health` reports | `app/health.hpp` + `adapters/net_services.cpp` |
| Add glyphs to a font | `tools/fontgen.sh`, then rebuild |

Next: [05 · Build, flash & debug →](05-build-flash-debug.md)
