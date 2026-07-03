# ESP32-32E CYD Board Support

## Goal

Add support for a second physical board — a "3.2 ESP32-32E 240x320 Resistance
Touch" all-in-one display module (Sunton/CYD-family clone; manufacturer
Shenzhen Hong Shu Yuan Technology; serial H06201; screen marked "HR4 8637S
B3/4") — on a dedicated branch, `esp32-32e-cyd`.

This board integrates the ESP32, TFT, and touch controller on one PCB with a
fixed pin layout, unlike the current hardware (a bare ILI9341 module
hand-wired to an ESP32 dev board on arbitrary pins).

## Hardware facts

Sourced from LCD Wiki's page for this exact board
(`3.2inch_ESP32-32E_Display`); to be verified empirically once flashed since
clone boards sometimes disagree with vendor docs.

| Function | Pin |
|---|---|
| TFT driver | **ST7789P3** (not ILI9341 — this is the main change) |
| TFT CS | IO15 |
| TFT DC | IO2 |
| TFT RST | EN (no dedicated GPIO — tied to chip reset) |
| TFT MOSI | IO13 |
| TFT SCLK | IO14 |
| TFT MISO | IO12 |
| TFT Backlight | IO27 (active high) |
| Touch (XPT2046) CS | IO33 |
| Touch IRQ | IO36 |
| Touch MOSI/SCLK/MISO | shared with TFT bus (13/14/12) |
| microSD CS/MOSI/SCLK/MISO | 5/23/18/19 (separate VSPI bus) — unused by this project, left unconfigured |

Two pins used by the current board's `Config.h` are unavailable here because
the display claims them:

- **GPIO2** — currently the status LED pin; on this board it's the TFT DC line.
- **GPIO14** — currently the backlight PWM pin; on this board it's the TFT SCLK line. The new board has its own dedicated backlight pin (IO27), so this isn't a "find a free pin" problem, just a different constant.

## Scope of changes (on the `esp32-32e-cyd` branch)

This branch is a direct hardware swap, not a multi-board codebase. No
`#ifdef`s, no second PlatformIO environment — `platformio.ini` and
`Config.h` are edited in place for this board, mirroring how the branch
itself already isolates the two hardware targets.

1. **`platformio.ini`**: replace `-DILI9341_DRIVER` and the old pin flags
   with `-DST7789_DRIVER` (or the exact TFT_eSPI macro name for ST7789P3)
   and the pin table above. `TFT_WIDTH`/`TFT_HEIGHT` stay 240×320. Rotation
   (currently `3` for landscape) and the touch-mirroring math documented in
   `CLAUDE.md` are specific to the old board's physical wiring and must be
   re-derived empirically on this board — not something to guess at in this
   design.

2. **`Config.h`**: drop `LED_PIN`. Change `TFT_BL_PIN` to 27.
   `BL_CHANNEL`/`BL_FREQ`/`BL_RES` unchanged.

3. **Replace the physical status LED with an on-screen indicator**, keeping
   the existing architecture's separation of concerns intact:
   - `DataFetcher::connect()` and `ensureWifi()` currently blink a GPIO
     directly inside their blocking wait loops. They will instead accept an
     optional tick callback (function pointer), invoked once per ~250ms
     blink tick and once more with the final connected/failed state.
     `DataFetcher` still does not touch the display or any drawing code —
     it only reports state through the callback.
   - `Renderer` gains a small method (e.g. `drawWifiIndicator(bool on)`)
     that draws or clears a small dot in a fixed screen corner without a
     full redraw.
   - `main.cpp` wires the two together: it passes a lambda to
     `DataFetcher` that calls `renderer.drawWifiIndicator(...)`, respecting
     the existing `ledEnabled` state.
   - The Settings screen's existing "LED" toggle (added in the most recent
     commit) is repointed to enable/disable this on-screen indicator instead
     of a physical GPIO. No UI relabeling needed — same toggle, same
     semantics from the user's point of view ("show a connection heartbeat
     indicator or not").

4. **`CLAUDE.md`**: once the board is flashed and the real rotation/touch
   quirks are known, add a short section documenting this board alongside
   the existing hardware-quirks section, so future work on this branch
   doesn't rediscover them.

## Out of scope

- No dual-board / shared-codebase support — this is a hardware-swap branch.
- No use of the onboard microSD slot, speaker, or I2C header — not needed by
  this project.
- Exact rotation value and touch-axis mirroring are left as "verify on
  hardware" — cannot be determined from docs alone, per this project's own
  documented experience with the current board's touch quirk.

## Testing / validation plan

After flashing to the physical board:

1. Confirm the display draws with correct colors and no inversion (ST7789
   panels frequently need a color-inversion flag that ILI9341 doesn't).
2. Confirm touch coordinates map correctly to visual positions on all
   screens; re-derive any mirroring math and update `TouchRouter.cpp` and
   `CLAUDE.md`.
3. Confirm backlight PWM works via GPIO27 across the brightness range.
4. Confirm the on-screen WiFi indicator behaves like the old LED did:
   blinks while connecting, solid when connected, off when disabled via the
   Settings toggle.
