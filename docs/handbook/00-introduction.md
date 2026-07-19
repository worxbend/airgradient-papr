# 00 · Introduction & Mental Model

← [Back to index](README.md) · Next: [01 · Architecture →](01-architecture.md)

---

## What is airdeck, really?

Imagine a small picture frame that sits on your desk. It never needs a phone or
an app. Every so often it quietly asks three questions:

1. *"Hey air sensor on my wall — how's the air right now?"* (CO₂, dust, etc.)
2. *"Hey internet — what's the weather and UV outside?"*
3. *"Hey internet — what are the exchange rates today?"*

Then it draws the answers in big, crisp, ink-on-paper numbers you can read from
across the room. Three buttons flip between three pages. That's airdeck.

The screen is **e-paper** (also called e-ink) — the same technology as a Kindle.
Its magic trick: **once a picture is drawn, it stays there using zero power.**
You only spend energy *changing* the picture. That single fact shapes the entire
firmware, so keep it in mind.

## The hardware, in one picture

```
        ┌──────────────────────────────────────────────┐
        │   LILYGO T5-4.7"  (the board in your hand)    │
        │                                              │
        │   ┌────────────┐      ┌───────────────────┐  │
        │   │ ESP32      │      │  ED047TC1 panel   │  │
        │   │ WROVER-E   │─────▶│  960 × 540 px     │  │
        │   │ 2 CPU cores│ draws│  16 gray levels   │  │
        │   │ 8MB PSRAM  │      │  (e-paper)        │  │
        │   └─────┬──────┘      └───────────────────┘  │
        │         │  Wi-Fi (2.4GHz)                     │
        │   [btn][btn][btn]  ← 3 front buttons          │
        │   🔋 optional LiPo on JST-PH                   │
        └─────────┼────────────────────────────────────┘
                  │
       ┌──────────┴───────────┐        ┌──────────────┐
       ▼                      ▼        ▼              ▼
 AirGradient ONE        Open weather   NBU rates   CoinGecko
 (on your LAN)          (wttr.in)      (bank.gov.ua)  (crypto)
```

Key numbers you'll see everywhere:

- **960 × 540 pixels**, **16 gray levels** → 4 bits per pixel.
- **ESP32-WROVER-E**: two CPU cores (we use both on USB), plus **8 MB of PSRAM**
  (external RAM) and only ~320 KB of fast internal RAM. That RAM split matters a
  *lot* — see [chapter 03](03-epaper-deep-dive.md).

## The 60-second code tour

Everything lives under `src/`, split into five folders. Read them as a sentence:

> The **domain** knows the rules of air quality. The **ports** describe what the
> domain needs from the outside world. The **adapters** actually fetch data and
> push pixels. The **app** wires it together and manages power. The **ui** turns
> data into a picture.

```
src/
├── domain/     ← pure logic. No Wi-Fi, no display. Unit-tested on your laptop.
│                 (aqi, metrics, measurement, weather, currency, trend)
├── ports/      ← interfaces: "a metrics source must have a poll() method".
├── adapters/   ← the real world: wifi_link, *_http fetchers, epd_guard, lvgl port.
├── app/        ← glue + policy: snapshot, buttons, power, health, clock.
└── ui/         ← LVGL screens: dashboard (page 1), weather, currency, fonts, theme.
```

Two files are the "front doors":

- **`src/main.cpp`** — the *composition root*. It creates everything and runs
  the main loop(s). Start reading here.
- **`include/config.hpp`** — your private settings (Wi-Fi password, sensor IP,
  timezone). Copied from `config.example.hpp`; never committed to git.

## Two personalities: USB vs. battery

The same code base builds into two very different programs, chosen by a compiler
flag (`-DPROFILE_BATTERY`):

| | **USB profile** (default) | **Battery profile** |
|---|---|---|
| Power source | Always-on USB | LiPo cell |
| CPU usage | Both cores busy | Wake, work ~seconds, sleep |
| Networking | A background task polls forever | One poll per wake |
| Screen | Updates as data arrives | Redraws only when air changed |
| Sleep | Never | Deep sleep between polls |

Chapter [02](02-power-model.md) is entirely about this split.

## Glossary (no jargon left behind)

| Term | Plain meaning |
|------|---------------|
| **e-paper / e-ink** | A display that holds its image with no power; slow to change. |
| **GC16** | The high-quality "waveform" that repaints all 16 gray levels. Looks great, takes ~1 second and flashes. |
| **framebuffer** | A chunk of memory holding one full screen of pixels, which we then hand to the panel. |
| **PSRAM** | External RAM (8 MB). Big but slower than internal RAM. We put the framebuffer + LVGL here. |
| **internal heap** | The ~320 KB of fast on-chip RAM. Scarce. The panel driver needs some free, or it hangs (ch. 03). |
| **LVGL** | The graphics library that draws boxes, arcs, labels, fonts. Version 9.2.2. |
| **adapter** | Code that translates between our clean core and a messy external thing (HTTP, the panel). |
| **domain** | The pure rules of the problem (what counts as "unhealthy CO₂"), with no I/O. |
| **snapshot** | One immutable bundle of "everything we currently know", passed from the network to the UI. |
| **ghosting** | Faint leftover of a previous e-paper image. Cured by a full clear. |
| **deep sleep** | ESP32's lowest-power mode. RAM is wiped; the chip reboots from `setup()` on wake. |
| **RTC memory** | A tiny slice of RAM that *survives* deep sleep. We stash a few flags there. |
| **NTP** | The internet time service. Until it answers, the clock reads ~1970. |
| **mailbox** | A one-slot queue. The newest value overwrites the old; the reader always gets "latest". |

Now you have the map. Let's walk the architecture.

Next: [01 · Architecture →](01-architecture.md)
