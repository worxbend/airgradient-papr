# 02 · Power Model

← [01 · Architecture](01-architecture.md) · Next: [03 · E-paper deep dive →](03-epaper-deep-dive.md)

---

## The golden rule of e-paper power

> **Holding an image costs nothing. Changing it costs everything.**

An e-paper pixel is a tiny capsule of black and white particles. Once you push
them into place, they *stay* — even with the power completely off. You only burn
energy during a **refresh** (moving particles) and while the **high-voltage
rails** are on to drive that motion. So the entire power strategy is:

1. Keep the high-voltage rails **off** except during the ~1 second of a refresh.
2. **Refresh as rarely as the use-case allows.**
3. On battery, **sleep** the whole chip between refreshes.

`EpdGuard` enforces #1 for both profiles (`epd_poweron()` immediately before a
draw, `epd_poweroff()` immediately after). #2 and #3 are what separate the two
build profiles.

## Profile A — USB always-on (default)

Selected by: the default `env:t5-epd47-v1` (no special flag).

The board has unlimited power, so we optimise for *freshness and responsiveness*,
not for milliamps:

- **Both cores run continuously.** Core 0 polls the network; core 1 renders.
- `WiFi.setSleep(false)` — the radio never naps, so polls are snappy.
- The screen updates whenever new data arrives, and a periodic whole-page
  refresh (`kPageRefreshSeconds`, default 30 s) keeps the clock current and
  gives the panel a clean redraw.
- The chip **never** deep-sleeps.

Power here is dominated by the Wi-Fi radio (tens of mA) and is simply not a
concern — you're plugged in.

## Profile B — battery deep-sleep

Selected by: `env:t5-epd47-v1-batt`, which adds `-DPROFILE_BATTERY`.

Here every milliamp-hour counts, and the physics above becomes the design. The
firmware is a **single-shot loop**: `setup()` runs once per wake, does all the
work, then calls `esp_deep_sleep_start()`. Deep sleep wipes RAM and reboots from
`setup()` on the next timer wake — so there is no `loop()`.

```
   power on ──▶ setup() ──▶ deep sleep ──(timer)──▶ setup() ──▶ deep sleep ──▶ ...
                  │
                  ├─ bring up display (preserve image on a timer wake)
                  ├─ read battery; if too low → charge screen → long sleep
                  ├─ Wi-Fi connect + poll AirGradient
                  ├─ decide: does this wake earn a refresh?  (the key step)
                  ├─ if yes: draw the dashboard (full GC16)
                  └─ power everything off → deep sleep for kBatteryPollSeconds
```

### Where the battery actually goes

Per wake, roughly, the energy sinks are:

| Activity | Rough cost | Notes |
|---|---|---|
| Wi-Fi connect + poll | **the big one** — ~100–200 mA for a few seconds | Unavoidable; needed for fresh data. |
| Full GC16 panel refresh | ~1 s of rail-on drive | Avoided entirely when data is unchanged. |
| Deep sleep | microamps | The chip is essentially off. |

So the two levers that matter are **how often we wake** and **whether we refresh
when we do**. Both are tuned by config and by the skip logic below.

### Lever 1 — wake cadence (`kBatteryPollSeconds`)

This is the single biggest factor in runtime. The USB profile polls every
`kPollSeconds` (30 s). If the battery profile did the same, it would wake 2,880
times a day — each waking the radio — and flatten a typical LiPo in **about a
day**.

So the battery profile has its **own, much longer interval**,
`kBatteryPollSeconds` (default **900 s = 15 minutes**). That's ~96 wakes a day
instead of 2,880 — a **30× reduction** — turning "lasts a day" into "lasts
weeks". Tune it in `config.hpp` for your freshness-vs-runtime trade-off.

### Lever 2 — skip the refresh when nothing changed

Because e-paper holds its image for free, redrawing the *same* numbers is pure
waste. But there's a catch: deep sleep wipes RAM, so every wake rebuilds the
whole UI from scratch and *would* naturally redraw everything. To beat that we
use two tricks:

1. **Preserve the panel image on a timer wake.** `EpdGuard::init(clearOnBoot)`
   normally clears the panel to white at boot. On a battery timer wake with a
   clean shutdown flag, `main.cpp` passes `clearPanel=false`, so the existing
   image is kept intact (a cold boot or an unclean shutdown still clears — boot
   hygiene wins). The wake cause is read via `esp_sleep_get_wakeup_cause()`.

2. **Remember what's on the panel across sleep, in RTC memory.** RTC slow memory
   survives deep sleep. We stash a tiny **signature** of the last-rendered
   reading:

   ```cpp
   RTC_DATA_ATTR static uint32_t rtcLastSig;    // hash of the on-panel values
   RTC_DATA_ATTR static uint32_t rtcWakeCount;  // wakes since cold boot
   RTC_DATA_ATTR static bool     rtcHasImage;   // has the panel ever been drawn
   ```

   `measurementSignature()` is an FNV-1a hash of the values we actually render,
   **quantised to display precision** (CO₂ to 1 ppm, PM to 0.1 µg/m³, etc.), so
   a meaningless sub-digit sensor wiggle doesn't trigger a refresh.

The decision each wake:

```cpp
doRefresh = firstImageEver
         || (pollSucceeded && (signatureChanged || backstopDue));
```

- **First image ever** → always draw, so a fresh unit shows *something*.
- **Poll failed** (Wi-Fi/sensor down) → **do not refresh.** Reprinting identical
  numbers because the clock ticked would waste a waveform *and* battery. The last
  good frame stays on the glass.
- **Backstop** (`kBatteryFullRefreshEvery`, default 4 → ~hourly at 15-min wakes)
  → force a full redraw even if unchanged, so the clock stays roughly current
  and any e-paper ghosting is cleared.

The serial log tells you which path ran: `[epd] refresh (changed)`,
`[epd] refresh (backstop/first)`, or `[epd] skip refresh (unchanged)`.

### The brownout safety gate (§6.1.4) — this one protects hardware

An e-paper refresh drives the panel with a carefully balanced AC-like waveform.
If the supply **browns out in the middle** of a refresh, the panel gets a
DC-imbalanced drive that can, over time, **physically damage it**. So before
doing anything panel-related on battery, `setup()` reads the pack:

```cpp
if (vbat > 0.5f && vbat < app::kBatteryLowV) {   // kBatteryLowV = 3.4 V
    ui::showMessage("CHARGE ME", ...);
    ... sleep for kBatteryLowSleepSeconds (30 min) ...
}
```

- `vbat <= 0.5 V` means "no divider present / running on USB" → skip the gate.
- Below **3.4 V** we refuse to drive a fresh waveform, show a static charge
  screen once, and sleep long. We also reset `rtcLastSig = 0` so the dashboard
  is force-redrawn as soon as the pack recovers.

### Reading the battery accurately (`app/power.cpp`)

The pack is sensed on **GPIO36** through a 2:1 divider. GPIO36 is an **ADC1**
pin, which stays usable while Wi-Fi is on (ADC2 is claimed by the radio — a
classic ESP32 gotcha).

A single ADC read carries ±tens-of-mV of noise. Right at the 3.4 V gate, that
jitter could wrongly trip *or* wrongly clear the low-battery path, so
`batteryVoltage()` **averages 16 samples** (well under a millisecond) and uses
`analogReadMilliVolts()`, which applies the chip's factory eFuse VRef
calibration — no raw-count-to-volts guesswork.

`batteryPercent()` maps 3.30–4.20 V linearly to 0–100 %. Real LiPo discharge is
non-linear, but for a glanceable "roughly how full" indicator this is plenty and
never reports outside 0–100.

## Tuning cheat-sheet (`include/config.hpp`)

| Constant | Default | Effect |
|---|---|---|
| `kBatteryPollSeconds` | 900 (15 min) | **Biggest runtime lever.** Longer = more battery, staler data. |
| `kBatteryFullRefreshEvery` | 4 | Force a redraw every Nth wake (clock freshness / de-ghost). `0` disables skip-on-unchanged. |
| `kBatteryLowSleepSeconds` | 1800 (30 min) | How long to sleep when the pack is below `kBatteryLowV`. |
| `kBatteryLowV` (`app/power.hpp`) | 3.4 V | Brownout refuse-to-drive threshold. |
| `kPollSeconds` | 30 | USB poll cadence (battery ignores it). |

Next: [03 · E-paper deep dive →](03-epaper-deep-dive.md)
