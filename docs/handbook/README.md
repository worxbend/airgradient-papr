# The airdeck Developer Handbook

> A build-it-in-your-head guide to the airdeck firmware — written so that a
> brand-new embedded engineer (or a very clever six-year-old) can read it
> front-to-back and then confidently change the code.

This handbook is the **authoritative design document** for airdeck. The source
comments occasionally reference a historical `PLAN.md` with section markers like
`§6.1.4`; that document was the original design spec. Those section numbers are
preserved here where they still matter, but **this handbook supersedes it** —
if the code and this book disagree, the code wins, and you should fix the book.

## How to read this book

Read it in order the first time. Each chapter builds on the last.

| # | Chapter | What you'll learn |
|---|---------|-------------------|
| [00](00-introduction.md) | **Introduction & mental model** | What airdeck *is*, the 60-second tour, and a glossary so no jargon trips you up. |
| [01](01-architecture.md) | **Architecture** | The four layers (domain / ports / adapters / app+ui), why the code is split this way, and how one air reading travels from the sensor to the glass. |
| [02](02-power-model.md) | **Power model** | USB always-on vs. battery deep-sleep, why we wake every 15 minutes, how we skip refreshes, and the brownout safety gate. |
| [03](03-epaper-deep-dive.md) | **E-paper deep dive** | How the panel physically works, the 4-bits-per-pixel framebuffer, the `EpdGuard` choke point, ghosting, and the infamous "white screen of death". |
| [04](04-module-reference.md) | **Module reference** | A file-by-file tour of `src/`, what each piece owns, and where to make common changes. |
| [05](05-build-flash-debug.md) | **Build, flash & debug** | Toolchain, the PlatformIO environments, `config.hpp`, flashing over USB and OTA, the `/health` endpoint, the mock sensor, and font generation. |
| [06](06-troubleshooting.md) | **Troubleshooting** | A symptom → cause → fix table for every sharp edge we've hit. |

## The one-paragraph summary

airdeck is firmware for a **LILYGO T5-4.7″ ESP32 e-paper board**. It polls an
**AirGradient ONE** air-quality monitor over your LAN, plus outdoor weather and
currency rates from the internet, and renders three full-screen pages on a
paper-crisp 960×540 grayscale display. The code is organised as a **hexagon**:
a pure, unit-tested core (`domain/`) that knows nothing about hardware, wrapped
by `adapters/` that talk to Wi-Fi, HTTP and the panel, coordinated by a thin
`app/` + `ui/` layer. Two build profiles exist: **USB** (a two-core always-on
renderer) and **battery** (wake → poll → draw → deep-sleep).

## Contributing to the docs

- Keep the beginner-first tone. Assume the reader is smart but new.
- When you change behavior, update the relevant chapter in the *same* commit.
- Prefer linking to a real file+symbol (e.g. `src/adapters/epd_guard.cpp` →
  `EpdGuard::present`) over paraphrasing — the code is the source of truth.
