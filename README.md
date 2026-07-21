<div align="center">

<img src="docs/assets/banner-v2.webp" width="900" alt="airdeck — your air at a glance on a local, paper-crisp e-ink dashboard">

<br>

[![CI](https://github.com/worxbend/airgradient-papr/actions/workflows/ci.yml/badge.svg)](https://github.com/worxbend/airgradient-papr/actions/workflows/ci.yml)
[![Release](https://github.com/worxbend/airgradient-papr/actions/workflows/release.yml/badge.svg)](https://github.com/worxbend/airgradient-papr/actions/workflows/release.yml)
![Platform](https://img.shields.io/badge/platform-ESP32--WROVER--E-informational?logo=espressif&logoColor=white)
![Framework](https://img.shields.io/badge/PlatformIO-Arduino_2.0.x-orange?logo=platformio&logoColor=white)
![UI](https://img.shields.io/badge/LVGL-9.2-black)
![E--paper](https://img.shields.io/badge/ED047TC1-960×540_16_gray-lightgrey)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**No cloud. No app. No subscription.** 🔒 Just your air — plus weather and money — rendered paper-crisp on a screen you can read from across the room. 📈

<img src="docs/assets/showcase-main.png" width="760" alt="airdeck main page: CO2 arc gauge + PM2.5 dotted ring + tiles">

<br><br>

<a href="https://youtu.be/1xlhLZY1_Wo" title="Watch airdeck in action">
  <img src="https://img.youtube.com/vi/1xlhLZY1_Wo/maxresdefault.jpg" width="560" alt="▶ Watch airdeck in action (YouTube)">
</a>

<br>
<sub>▶️ <a href="https://youtu.be/1xlhLZY1_Wo"><strong>Watch it run</strong></a> — 60-second demo</sub>

</div>

---

## <img src="docs/assets/icons/idea.svg" width="26" align="top"> &nbsp;Why I built this 💡

Okay, real talk. I've got my **AirGradient ONE** mounted on the wall right next to my desk. 🧑‍💻 Perfect spot… except it's basically *behind me* — out of my line of sight while I'm actually working. So to check the air I'd have to spin my chair around and squint at the sensor's tiny onboard screen. Annoying.

I already built myself little companion apps — one for my **phone** 📱 and one for **Linux** 🐧 — so I could peek at the readings without getting up. Nice. Problem solved, right?

Nope. Classic plot twist: I'm sitting in **another room, no phone on me**, and I suddenly want to know what the air in my office is doing. Can't. 🙃

So I did the reasonable-person thing and built a **portable e-ink terminal** that just… sits there and shows me. No phone, no app, no getting up, no glare. Grab it, glance at it, done. **That's airdeck.** ✨

---

## <img src="docs/assets/icons/air.svg" width="26" align="top"> &nbsp;What it actually is

**airdeck** is standalone firmware for a **LILYGO T5-4.7″ ESP32 e-paper** board that adopts your AirGradient ONE and gives it the panoramic, zero-glare, always-on dashboard it deserves — talking to the sensor **entirely over your LAN** via its local API. 🏠

Then it got greedy 😎 and picked up two more tricks: **outdoor weather + forecast + UV**, and **live FX & crypto rates**. Because a screen this nice shouldn't only do one thing.

<div align="center">
<img src="docs/assets/showcase-weather.png" width="380" alt="Weather page">
&nbsp;&nbsp;
<img src="docs/assets/showcase-currency.png" width="380" alt="Currency page">
</div>

---

## <img src="docs/assets/icons/air.svg" width="26" align="top"> &nbsp;The AirGradient bit (the whole point) 🟢

airdeck speaks the **AirGradient ONE local-server API** — no AirGradient cloud account, no internet round-trip:

```
GET http://<your-monitor-ip>/measures/current   →   JSON, every 30 s
```

Every field the I-9PSL exposes lands on the panel:

| 🫁 Air | 🌡️ Comfort | 🧪 Chemistry | ℹ️ Meta |
|---|---|---|---|
| CO₂ (ppm) | Temperature | TVOC index + raw | firmware / model |
| PM1.0 / PM2.5 / PM10 | Humidity | NOx index + raw | boot count · LED mode |
| PM0.3 particle count | (compensated too) | | RSSI |

…all classified with **US EPA AQI** (PM2.5) and per-metric health bands (Good / Moderate / Elevated / Unhealthy). CO₂ and PM2.5 — the two with the clearest thresholds — get their own **gauges**. 🎯

> **Prereqs:** AirGradient firmware **≥ 3.0.10** with the local API on, and a **DHCP reservation** so the monitor's IP never drifts. That's it. ✅

Big respect to **[AirGradient](https://github.com/airgradienthq)** for building open, hackable, honest hardware. 🙏

---

## <img src="docs/assets/icons/pages.svg" width="26" align="top"> &nbsp;The pages 🖥️

Three full-screen pages, flipped with the board's three buttons — **◀ prev · ⌂ main · next ▶** — with a little paging dot indicator up top.

- **1️⃣ AirGradient** — CO₂ as a sweeping **arc gauge with a knob**, PM2.5 as a **dotted ring**, a 2×2 tile block (temp / humidity / TVOC / NOx), a PM detail row, and a big clock. 🕗
- **2️⃣ Weather** — current conditions + **AIR** (humidity / wind / precip / sun) + **UV** (index, band, today's max, high-UV window) + an **8-slot hourly strip** + a **3-day forecast** with range bars. Auto-geolocated 📍, no API key.
- **3️⃣ Currency** — a **30-day USD→UAH chart** 📉 over live rows: **USD→UAH · EUR→UAH · CNY→USD** (NBU) and **BTC→USD · ETH→USD** (CoinGecko).

> On boot, a 🌀 **splash screen** fetches every data source first, *then* renders the pages fully-formed — no half-empty screens.

---

## <img src="docs/assets/icons/hardware.svg" width="26" align="top"> &nbsp;Hardware 🧰

| Part | Notes |
|---|---|
| 🧠 **LILYGO T5-4.7″ V1** | ESP32-**WROVER-E**, 16 MB flash, 8 MB PSRAM, ED047TC1 960×540 e-paper |
| 🟢 **AirGradient ONE (I-9PSL)** | your air sensor, on the same Wi-Fi |
| 🔌 USB-A→C data cable | ⚠️ *not* C-to-C — the board lacks CC pulldowns |
| 🔋 *(optional)* 3.7 V LiPo | JST-PH — **check polarity with a multimeter first!** |

---

## <img src="docs/assets/icons/rocket.svg" width="26" align="top"> &nbsp;Quick start 🚀

```bash
# 1. Grab the code + PlatformIO
git clone https://github.com/worxbend/airgradient-papr.git && cd airgradient-papr
pip install platformio

# 2. Tell it about your Wi-Fi + monitor
cp include/config.example.hpp include/config.hpp
$EDITOR include/config.hpp          # SSID, password, http://<monitor-ip>/measures/current, timezone

# 3. Sanity-check the sensor is reachable
curl http://<monitor-ip>/measures/current   # should spit JSON

# 4. Flash it 🔥  (board on /dev/ttyUSB0)
pio run -e t5-epd47-v1 -t upload

# 5. Watch it wake up
pio device monitor
```

```text
[airdeck] boot
[boot] PSRAM: 4192123 bytes free
[net] ok #1  CO2=480 PM2.5=10.2 T=30 RH=43
[wx]  Kyiv 30C UV 1 Patchy rain nearby
[cur] USD=44.67 EUR=51.06 BTC=64539 hist=32
```

Prefer a one-shot image? Every tagged release ships a **single-file factory `.bin`** — flash it at `0x0` with `esptool` and go. 📦

**🛰️ OTA:** once it's on Wi-Fi it advertises **ArduinoOTA** + a **`/health`** JSON endpoint:

```bash
pio run -e t5-epd47-v1 -t upload --upload-port airdeck.local   # no USB needed
curl http://airdeck.local/health                                # uptime, heap, poll stats
```

---

## <img src="docs/assets/icons/arch.svg" width="26" align="top"> &nbsp;Under the hood 🏗️

- **Hexagonal-ish** layout: a pure, host-tested `domain/` (AQI, banding, trend) with thin `adapters/` (HTTP, e-paper, buttons) and `ui/` pages.
- **Two FreeRTOS tasks + a mailbox** — networking on core 0, LVGL on core 1, no locks.
- **LVGL 9.2** rendering **L8 → 4 bpp**, its heap pool living in **PSRAM** so Wi-Fi + the e-paper driver keep their internal RAM.
- **Panel-care baked in**: power discipline, full-refresh cadence, temperature clamp, boot hygiene, and a heap-gated refresh so a fetch spike can never wedge the display.

📖 **New to the code?** The [**Developer Handbook**](docs/handbook/README.md) is a from-first-principles, book-style walkthrough of the whole firmware (architecture, power model, e-paper internals, module reference, build/flash/debug, troubleshooting). See [`docs/DECISIONS.md`](docs/DECISIONS.md) for the version-pin war stories 🪖.

| env | target |
|---|---|
| `t5-epd47-v1` | **default** — the WROVER-E board, USB always-on |
| `t5-epd47-v1-batt` | battery deep-sleep single-shot profile |
| `native-test` | host unit tests for `domain/` |

---

## <img src="docs/assets/icons/ci.svg" width="26" align="top"> &nbsp;CI/CD 🤖

Every push gets put through its paces by GitHub Actions:

- 🧪 **Unit tests** — the `domain/` layer on the host
- 🔧 **Build matrix** — firmware for every device env, artifacts uploaded
- 🎨 **clang-format** — style stays honest
- 🚀 **Release** — tag `v*` → builds + merges a factory image → publishes a GitHub Release
- 🤖 **Dependabot** — keeps the Actions fresh

---

## <img src="docs/assets/icons/box.svg" width="26" align="top"> &nbsp;Enclosure 📦

<div align="center">
<img src="docs/assets/final-1.jpg" width="380" alt="airdeck final build — angled desk-stand enclosure, front">
&nbsp;
<img src="docs/assets/final-2.jpg" width="380" alt="airdeck final build — dashboard live on the desk">
<br>
<sub><em>The finished build — printed, wired, and reading the room. 🖨️</em></sub>
</div>

<details>
<summary>CAD render</summary>
<div align="center">
<img src="docs/assets/showcase-case.png" width="620" alt="airdeck desk-stand enclosure (CAD render)">
</div>
</details>

A slick, angled desk-stand case. **FreeCAD source + print-ready `.3mf`** are coming soon™ — they'll live in `hardware/`. Print it, prop it on your desk, glance away. 🖨️

---

## <img src="docs/assets/icons/warning.svg" width="26" align="top"> &nbsp;Gotchas worth knowing 🛟

- 🔌 Use a **USB-A→C** cable (C-to-C won't enumerate).
- 🪫 On battery: **verify JST-PH polarity with a multimeter** before first connect.
- 📌 Give the AirGradient a **DHCP reservation**.
- ☀️ Keep the panel out of **direct sunlight** — e-ink hates UV.
- 🔩 Never repurpose strapping pins **GPIO0/2/5/12/15**.

---

## <img src="docs/assets/icons/heart.svg" width="26" align="top"> &nbsp;Credits & kin ❤️

- 🟢 **[AirGradient](https://www.airgradient.com/)** — the open sensor that started it all
- 🖼️ **[LVGL](https://lvgl.io/)** · **[LilyGo-EPD47](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47)** / **[epdiy](https://github.com/vroland/epdiy)** — pixels on paper
- 🌦️ **[wttr.in](https://github.com/chubin/wttr.in)** · **[Open-Meteo](https://open-meteo.com/)** · 🏦 **[NBU](https://bank.gov.ua/)** · 🪙 **[CoinGecko](https://www.coingecko.com/)** — the data
- 🛠️ More builds at **[github.com/worxbend](https://github.com/worxbend)**

<div align="center">
<br>
<img src="docs/assets/logo.svg" width="72" alt="airdeck">
<br><br>

**Built with 🫁, ☕, and a healthy distrust of the cloud.**

*If airdeck made your air visible, drop a ⭐.*

</div>
