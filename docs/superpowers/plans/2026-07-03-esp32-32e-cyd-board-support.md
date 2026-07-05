# ESP32-32E CYD Board Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get this firmware building and running on the "3.2 ESP32-32E 240x320 Resistance Touch" all-in-one board (ST7789P3 driver, fixed pinout) on a dedicated `esp32-32e-cyd` branch, replacing the physical status LED (which the new board's pinout can't support) with an on-screen indicator.

**Architecture:** Direct hardware swap — `platformio.ini` and `Config.h` are edited in place for the new board's driver and pins (no dual-env, no `#ifdef`s). `DataFetcher` gains an optional tick callback so it can report WiFi connect/blink state without depending on `Renderer` or touching a GPIO directly; `main.cpp` wires that callback to a new `Renderer::drawWifiIndicator()` method. This preserves the existing three-way separation between `DataFetcher` (WiFi/HTTP only), `Renderer` (pure drawing), and `TouchRouter` (input only) documented in `CLAUDE.md`.

**Tech Stack:** PlatformIO, Arduino framework for ESP32, TFT_eSPI (ST7789 driver), ArduinoJson.

## Global Constraints

- Board: ESP32-32E, ST7789P3 driver, 240×320, TFT pins CS=15 DC=2 RST=EN(no GPIO) MOSI=13 SCLK=14 MISO=12 BL=27; touch (XPT2046) CS=33 IRQ=36 sharing the TFT SPI bus. (From `docs/superpowers/specs/2026-07-03-esp32-32e-cyd-board-support-design.md`.)
- GPIO2 and GPIO14 are unavailable (claimed by TFT DC and SCLK) — do not reuse them for LED/backlight.
- No `#ifdef`/dual-PlatformIO-env support — this branch targets only the new board.
- `DataFetcher` must not `#include` or reference `Renderer`/`TFT_eSPI` — it communicates status only via the callback.
- No physical LED code on this branch — status is shown on-screen via `Renderer::drawWifiIndicator()`.
- Build verification command for every task: `~/.platformio/penv/bin/pio run -e esp32dev` run from the repo root (PlatformIO CLI is not on `PATH` in this environment but is installed there).
- Hardware flashing/on-device verification cannot be performed by the agent (no USB/serial access in this environment) — those steps are explicitly for the user to run.

---

### Task 1: Create branch and swap `platformio.ini` for the new board

**Files:**
- Modify: `platformio.ini`

**Interfaces:**
- Produces: build flags consumed by TFT_eSPI at compile time (`ST7789_DRIVER`, `TFT_CS`, `TFT_DC`, `TFT_RST`, `TFT_MOSI`, `TFT_SCLK`, `TFT_MISO`, `TFT_BL`, `TOUCH_CS`) — later tasks (`Config.h`, `Renderer.cpp`) assume these are set correctly for a 240×320 ST7789P3 panel.

- [ ] **Step 1: Create and switch to the branch**

```bash
git checkout -b esp32-32e-cyd
```

- [ ] **Step 2: Replace the TFT_eSPI build flags in `platformio.ini`**

Replace the entire `build_flags` block (currently lines 19-36) with:

```ini
build_flags =
    -DST7789_DRIVER
    -DTFT_RGB_ORDER=TFT_BGR
    -DTFT_INVERSION_ON
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=320
    -DTFT_CS=15
    -DTFT_DC=2
    -DTFT_RST=-1
    -DTFT_MOSI=13
    -DTFT_SCLK=14
    -DTFT_MISO=12
    -DTFT_BL=27
    -DTFT_BACKLIGHT_ON=HIGH
    -DTOUCH_CS=33
    -DSPI_FREQUENCY=40000000
    -DSPI_TOUCH_FREQUENCY=2500000
    -DLOAD_GLCD
    -DLOAD_FONT2
    -DLOAD_FONT4
    -DSMOOTH_FONT
    -DLOAD_GFXFF
```

`TFT_RST=-1` means "no dedicated reset pin" (this board ties TFT reset to
the ESP32's EN/reset line). `TFT_RGB_ORDER=TFT_BGR` and `TFT_INVERSION_ON`
are the near-universal settings needed for ST7789 panels in TFT_eSPI to
show correct (non color-swapped, non-inverted) colors — Task 6 has the
on-device check to confirm or flip these.

- [ ] **Step 3: Verify it builds**

```bash
~/.platformio/penv/bin/pio run -e esp32dev
```

Expected: `SUCCESS` — this only proves the new flags compile with TFT_eSPI;
it does not prove the display works, since that requires the physical
board (Task 6).

- [ ] **Step 4: Commit**

```bash
git add platformio.ini
git commit -m "Switch build to ST7789P3 driver and ESP32-32E CYD board pinout"
```

---

### Task 2: Update `Config.h` for the new board's pin constraints

**Files:**
- Modify: `src/Config.h`

**Interfaces:**
- Consumes: nothing new.
- Produces: `TFT_BL_PIN` is now 27, matching Task 1's `-DTFT_BL=27`. `LED_PIN` no longer exists — Task 4/5 must not reference it.

- [ ] **Step 1: Edit `Config.h`**

Replace the full contents of `src/Config.h` with:

```cpp
#pragma once

#define TFT_BL_PIN 27
#define BL_CHANNEL 0
#define BL_FREQ    5000
#define BL_RES     8
```

(`LED_PIN` is removed — GPIO2 is the TFT DC line on this board. The status
LED is replaced by an on-screen indicator in Task 3-5.)

- [ ] **Step 2: Verify it builds**

```bash
~/.platformio/penv/bin/pio run -e esp32dev
```

Expected: This will currently **FAIL** with `'LED_PIN' was not declared in
this scope`, because `main.cpp` and `DataFetcher.cpp` still reference it.
That's expected at this point in the plan — confirm the error mentions
`LED_PIN` specifically (not an unrelated failure) before moving on.

- [ ] **Step 3: Commit**

```bash
git add src/Config.h
git commit -m "Drop LED_PIN, move backlight to GPIO27 for ESP32-32E CYD board"
```

---

### Task 3: Add `Renderer::drawWifiIndicator()`

**Files:**
- Modify: `src/Renderer.h`
- Modify: `src/Renderer.cpp`

**Interfaces:**
- Consumes: `TFT_eSPI& _tft` (existing member).
- Produces: `void drawWifiIndicator(bool on)` — public method. Task 5 wires this into a callback passed to `DataFetcher`.

- [ ] **Step 1: Declare the method in `Renderer.h`**

In `src/Renderer.h`, add `void drawWifiIndicator(bool on);` to the public
section, after `updateLedToggle`:

```cpp
  void updateLedToggle(bool ledEnabled);
  void drawWifiIndicator(bool on);
};
```

- [ ] **Step 2: Implement it in `Renderer.cpp`**

Add this at the end of `src/Renderer.cpp`, after `updateIntervalButtons`:

```cpp
void Renderer::drawWifiIndicator(bool on) {
  _tft.fillCircle(310, 8, 4, on ? TFT_GREEN : TFT_BLACK);
}
```

Top-right corner (310, 8) is clear of content on every screen (`Claude`'s
sprite header ends at x≈35, `Grok`'s and `Settings`' titles start at
x=10 and are far short of x=306), and it's overwritten naturally by the
next full-screen redraw (`fillScreen` in `drawClaude`/`drawGrok`/
`drawSettings`), so it never needs explicit clearing elsewhere.

- [ ] **Step 3: Verify it builds**

```bash
~/.platformio/penv/bin/pio run -e esp32dev
```

Expected: Still **FAILS** with the same `LED_PIN` error as Task 2 — this
task only adds a new method, it doesn't yet fix the callers. Confirm no
*new* errors were introduced (e.g. no `Renderer.h`/`.cpp` syntax errors).

- [ ] **Step 4: Commit**

```bash
git add src/Renderer.h src/Renderer.cpp
git commit -m "Add Renderer::drawWifiIndicator for on-screen WiFi status dot"
```

---

### Task 4: Replace direct GPIO blink in `DataFetcher` with a callback

**Files:**
- Modify: `src/DataFetcher.h`
- Modify: `src/DataFetcher.cpp`

**Interfaces:**
- Consumes: nothing new (still only `WiFi`, `HTTPClient`, `ArduinoJson`).
- Produces: `void setIndicatorCallback(void (*callback)(bool))` — public
  method. Task 5's `main.cpp` calls this once at startup with a function
  that forwards to `Renderer::drawWifiIndicator`. The callback receives
  `true` when the indicator should show "on" (blinking-on or
  connected-and-enabled) and `false` otherwise; `DataFetcher` handles the
  `ledEnabled` gating internally before invoking it, so the callback itself
  doesn't need to know about the enabled/disabled setting.

- [ ] **Step 1: Update `DataFetcher.h`**

Replace the full contents of `src/DataFetcher.h` with:

```cpp
#pragma once
#include <Arduino.h>
#include "Types.h"

class DataFetcher {
  const char* _ssid;
  const char* _password;
  const char* _url;
  int _failures = 0;
  bool _ledEnabled = true;
  void (*_indicatorCallback)(bool) = nullptr;

  void ensureWifi();
  void setIndicator(bool on);

public:
  DataFetcher(const char* ssid, const char* password, const char* url);
  bool connect(unsigned long timeoutMs = 15000);
  bool fetch(UsageData& out);
  int  consecutiveFailures() const;
  void setLedEnabled(bool enabled);
  void setIndicatorCallback(void (*callback)(bool));
};
```

- [ ] **Step 2: Update `DataFetcher.cpp`**

Replace `src/DataFetcher.cpp` lines 1-36 (everything from the `#include`s
through the end of `setLedEnabled`) with:

```cpp
#include "DataFetcher.h"
#include "Config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

DataFetcher::DataFetcher(const char* ssid, const char* password, const char* url)
  : _ssid(ssid), _password(password), _url(url) {}

void DataFetcher::setIndicator(bool on) {
  if (_indicatorCallback) _indicatorCallback(_ledEnabled && on);
}

void DataFetcher::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.disconnect(false);
  WiFi.begin(_ssid, _password);
  unsigned long t = millis();
  bool blinkOn = false;
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) {
    blinkOn = !blinkOn;
    setIndicator(blinkOn);
    delay(250);
  }
  setIndicator(WiFi.status() == WL_CONNECTED);
}

bool DataFetcher::connect(unsigned long timeoutMs) {
  WiFi.begin(_ssid, _password);
  unsigned long t = millis();
  bool blinkOn = false;
  while (WiFi.status() != WL_CONNECTED && millis() - t < timeoutMs) {
    blinkOn = !blinkOn;
    setIndicator(blinkOn);
    delay(250);
  }
  setIndicator(WiFi.status() == WL_CONNECTED);
  return WiFi.status() == WL_CONNECTED;
}

void DataFetcher::setLedEnabled(bool enabled) {
  _ledEnabled = enabled;
  setIndicator(WiFi.status() == WL_CONNECTED);
}

void DataFetcher::setIndicatorCallback(void (*callback)(bool)) {
  _indicatorCallback = callback;
}
```

Leave `fetch()` and `consecutiveFailures()` (the rest of the file)
unchanged.

- [ ] **Step 3: Verify it builds**

```bash
~/.platformio/penv/bin/pio run -e esp32dev
```

Expected: Still **FAILS**, but now only on `main.cpp`'s `pinMode(LED_PIN,
OUTPUT);` — confirm `DataFetcher.cpp` no longer appears in the error
output.

- [ ] **Step 4: Commit**

```bash
git add src/DataFetcher.h src/DataFetcher.cpp
git commit -m "Replace DataFetcher's direct LED GPIO blink with an indicator callback"
```

---

### Task 5: Wire the callback in `main.cpp` and drop the LED pin setup

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `Renderer::drawWifiIndicator(bool)` (Task 3),
  `DataFetcher::setIndicatorCallback(void (*)(bool))` (Task 4).
- Produces: nothing further downstream — this is the last task that touches
  compiled code.

- [ ] **Step 1: Remove the LED pin setup and add the indicator wiring**

In `src/main.cpp`, replace:

```cpp
unsigned long lastFetch = 0;

void setup() {
  Serial.begin(115200);
  esp_log_level_set("*", ESP_LOG_NONE);
  pinMode(LED_PIN, OUTPUT);

  renderer.init(state.brightness);
```

with:

```cpp
unsigned long lastFetch = 0;

void wifiIndicator(bool on) {
  renderer.drawWifiIndicator(on);
}

void setup() {
  Serial.begin(115200);
  esp_log_level_set("*", ESP_LOG_NONE);
  fetcher.setIndicatorCallback(wifiIndicator);

  renderer.init(state.brightness);
```

(`wifiIndicator` is a free function, not a lambda, because
`DataFetcher::setIndicatorCallback` takes a plain function pointer with no
captures — it references the global `renderer` the same way the rest of
`main.cpp` already does.)

- [ ] **Step 2: Verify it builds**

```bash
~/.platformio/penv/bin/pio run -e esp32dev
```

Expected: `SUCCESS`. This is the first fully-green build since Task 1 —
confirms every `LED_PIN` reference is gone and the new wiring compiles.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "Wire WiFi indicator callback to on-screen dot in main.cpp"
```

---

### Task 6: Flash, verify on hardware, and record findings

This task is manual — it requires the physical board over USB, which this
agent cannot access. Run each check yourself and report back what you see;
findings get folded into `CLAUDE.md` afterward.

**Files:**
- Modify: `CLAUDE.md` (after verification, to document this board's quirks
  the same way the ILI9341 board's quirks are documented today)

- [ ] **Step 1: Flash the board**

```bash
~/.platformio/penv/bin/pio run -e esp32dev -t upload
~/.platformio/penv/bin/pio device monitor
```

- [ ] **Step 2: Check display colors**

Confirm the Claude/Grok/Settings screens show correct, non-inverted,
non-color-swapped colors (green progress bars actually look green, not
orange/blue-shifted). If colors are wrong, that's the
`-DTFT_RGB_ORDER=TFT_BGR` / `-DTFT_INVERSION_ON` flags from Task 1 — report
which one(s) need to be flipped or removed and it'll be a one-line follow-up
to `platformio.ini`.

- [ ] **Step 3: Check orientation and touch mapping**

Confirm the display shows landscape 320×240 (not portrait or upside-down)
with `_tft.setRotation(3)` (unchanged from `Renderer::init`). Tap the
navigation zones, brightness +/-, interval buttons, LED toggle, and reboot
button on the Settings screen (all previously covered by the touch-mirroring
quirk documented in `CLAUDE.md`) and confirm each does what it visually
says. If tapping feels mirrored or offset, that confirms this board's touch
x-axis behaves differently from the old board's and `TouchRouter.cpp`'s
`319 - x` mirroring math needs re-deriving for this hardware.

- [ ] **Step 4: Check backlight**

Confirm the brightness +/- buttons on the Settings screen visibly dim/
brighten the display via GPIO27.

- [ ] **Step 5: Check the on-screen WiFi indicator**

Power-cycle the board (or temporarily use a wrong WiFi password to force a
retry loop) and confirm the small dot in the top-right corner blinks
while connecting and disappears/goes solid appropriately once connected.
Confirm toggling "LED: ON/OFF" on the Settings screen suppresses it.

- [ ] **Step 6: Record findings in `CLAUDE.md`**

Once the above is verified (with any needed one-line fixes to
`platformio.ini` or `TouchRouter.cpp` made and committed separately), add a
short section to `CLAUDE.md` alongside the existing "Critical hardware
quirks" section, documenting this board's actual (not assumed) rotation,
color-flag, and touch-mirroring behavior, so a future session doesn't have
to rediscover it.

- [ ] **Step 7: Commit**

```bash
git add CLAUDE.md
git commit -m "Document ESP32-32E CYD board quirks after hardware verification"
```
