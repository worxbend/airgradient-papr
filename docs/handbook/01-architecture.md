# 01 · Architecture

← [00 · Introduction](00-introduction.md) · Next: [02 · Power model →](02-power-model.md)

---

## Why bother with "architecture" on a tiny gadget?

Because embedded bugs are expensive to find (you can't easily `printf` inside a
display driver) and because we want to test the *logic* — "is 900 ppm CO₂
unhealthy?" — on a laptop in milliseconds, not by re-flashing a board. So we
draw a hard line between **pure decision-making** and **messy I/O**. That line
is the whole design. It's a lightweight take on **hexagonal architecture** (a.k.a.
"ports and adapters").

## The layers, from the inside out

```
        ┌───────────────────────────────────────────────────────┐
        │                         ui/                            │  turns data
        │         dashboard · weather · currency · theme         │  into pixels
        ├───────────────────────────────────────────────────────┤
        │                         app/                           │  policy & glue
        │      main · snapshot · buttons · power · health        │
        ├───────────────────────────────────────────────────────┤
        │                       adapters/                        │  the real world
        │  wifi_link · *_http · epd_guard · epd_lvgl_port · net  │
        ├───────────────────────────────────────────────────────┤
        │                        ports/                          │  interfaces
        │                    metrics_source                      │
        ├───────────────────────────────────────────────────────┤
        │                       domain/  ◀── PURE, TESTED        │  the rules
        │   measurement · aqi · metrics · weather · currency ·   │
        │                        trend                           │
        └───────────────────────────────────────────────────────┘

        Dependencies point INWARD only. domain/ depends on nothing.
```

### `domain/` — the rules (pure, no hardware)

These headers include only `<cmath>`, `<cstdint>`, `<cstring>` — **never**
`<Arduino.h>`, `<WiFi.h>`, or LVGL. That's the rule that lets them compile and
run on your laptop under the `native-test` environment.

- **`measurement.hpp`** — one AirGradient reading as plain data. Missing fields
  are `NaN`. Helper `tempC()` prefers the compensated value over the raw one.
- **`aqi.hpp`** — US EPA AQI math for PM2.5, plus health/comfort *bands*
  (`Good … Hazardous`) for CO₂, TVOC, NOx, temperature, humidity.
- **`metrics.hpp`** — a catalog that ties each metric to its label, unit,
  band function, and priority. Also `heroMetric()` (the worst metric right now).
- **`weather.hpp` / `currency.hpp`** — plain-data models for those pages.
- **`trend.hpp`** — a fixed-capacity ring buffer (`History<N>`) plus trend/Δ
  logic for sparklines. Templated so there's no heap allocation.

> **Why pure matters:** `test/test_domain/test_domain.cpp` exercises all of this
> with `pio test -e native-test` in ~0.2 s. If you change a threshold, a failing
> test tells you instantly — no board required.

### `ports/` — the interface (a contract)

- **`metrics_source.hpp`** declares *what* the app needs ("something with a
  `poll(Measurement&)`"), not *how*. The AirGradient HTTP adapter implements it.
  This is the seam where a mock or a different sensor could slot in.

### `adapters/` — the messy outside world

Each adapter wraps exactly one external dependency and hides its ugliness:

- **`wifi_link`** — connect + keep-alive with exponential backoff.
- **`airgradient_http`** — GET the sensor's local JSON, filter it, fill a
  `Measurement`. Tolerant of firmware key-spelling differences.
- **`weather_http`** — geolocate (ip-api.com) + fetch weather/UV (wttr.in).
  Plain HTTP on purpose (TLS is unreliable on this radio — see ch. 06).
- **`currency_http`** — HTTPS to NBU + CoinGecko, with retries.
- **`epd_guard`** — **the single choke point for every panel write** (ch. 03).
- **`epd_lvgl_port`** — bridges LVGL's "here's a rectangle of pixels" callback
  into the guard's framebuffer.
- **`net_services`** — the `/health` HTTP endpoint + ArduinoOTA.

### `app/` — policy and glue

- **`snapshot.hpp`** — the immutable bundle of everything we know (`Measurement`
  + `Weather` + `Currency` + connection state). Copied by value.
- **`buttons`** — debounced edge detection on GPIO 34/35/39.
- **`power`** — battery ADC + the low-voltage threshold.
- **`clock`** — the "is NTP synced yet?" helper (shared by 4 modules).
- **`health`** — cross-task counters for the `/health` endpoint.

### `ui/` — data becomes a picture

- **`ui.cpp`** — the page manager: splash screen, page switching, paging dots.
- **`dashboard.cpp`** — page 1 (air): the CO₂ arc gauge, PM2.5 dot ring, tiles.
- **`pages/weather.cpp`**, **`pages/currency.cpp`** — pages 2 and 3.
- **`theme.hpp`** — shared widget factories (`card`, `label`, `pill`) and the
  crucial `setText()` that only invalidates when text actually changed.
- **`fonts/`** — custom LVGL fonts (big numerals, unit glyphs like `µg/m³`).

## How one air reading travels (USB profile)

This is the single most important data-flow to understand. Follow the arrows:

```
   ┌─ CORE 0 (network task) ──────────────┐   ┌─ CORE 1 (Arduino loop) ─────────┐
   │                                      │   │                                 │
   │  wifi.ensureConnected()              │   │  xQueueReceive(mailbox) ───┐    │
   │  source.poll(measurement)            │   │                            ▼    │
   │  snap.m = measurement                │   │  ui::update(snapshot)           │
   │  xQueueOverwrite(mailbox, snap) ─────┼──▶│    aq::update / weather / cur    │
   │       (length-1 "mailbox")           │   │  lv_timer_handler()             │
   │  ...also weather + currency ...      │   │    → LVGL renders dirty areas    │
   │  vTaskDelay(50ms)                    │   │    → flushCb → EpdGuard          │
   │                                      │   │    → epd_poweron/draw/poweroff   │
   └──────────────────────────────────────┘   └─────────────────────────────────┘
```

Two rules make this safe and simple:

1. **One writer, one reader, one slot.** The network task *only writes* the
   mailbox; the UI task *only reads*. The mailbox (`xQueueCreate(1, …)` +
   `xQueueOverwrite`) always holds the newest snapshot — a slow renderer can
   never build a backlog; it just skips to "latest". No mutexes needed.
2. **All LVGL calls happen on the UI task (core 1).** LVGL is not thread-safe.
   The network task never touches the screen. This is a hard invariant — if you
   add a feature that draws, it must run on the loop, not the net task.

The `app::HealthStats g_health` struct is the one exception: the net task writes
counters that the `/health` web handler (also on the net task's core) reads. They
are `volatile` 32-bit scalars, whose reads/writes are atomic on ESP32, so a
status page never needs a lock.

## Why this shape pays off

- **You can change the "rules" fearlessly.** Tweak an AQI band → run host tests
  → done. No board, no flashing, no waiting.
- **You can swap the outside world.** The mock sensor (`tools/mock_airgradient.py`)
  stands in for real hardware because the adapter only depends on an HTTP URL.
- **Bugs have a home.** A rendering glitch is in `ui/` or `epd_guard`; a wrong
  number is in `domain/`; a flaky fetch is in an `*_http` adapter. You rarely
  have to guess which layer.

Next: [02 · Power model →](02-power-model.md)
