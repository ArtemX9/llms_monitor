# Battery Level Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Read the onboard battery-voltage divider (GPIO34) and show a small
battery-level icon + percentage in the top-right corner of every screen
(Claude, Grok, Settings; landscape and portrait), matching the design in
[`docs/superpowers/specs/2026-07-05-battery-level-display-design.md`](../specs/2026-07-05-battery-level-display-design.md).

**Architecture:** A new `Battery` class (mirrors `DataFetcher`'s
one-concern-per-file pattern) owns ADC characterization and converts a raw
reading to a 0–100 percent. `Renderer` gains one small private drawing method
(`drawBatteryIcon`) called identically from all six existing screen-draw
functions, positioned purely from `_tft.width()` so it needs no
per-orientation variant — same trick already used by `drawWifiIndicator`.
`main.cpp` feeds a fresh reading into `Renderer` on the same cadence as the
existing WiFi data fetch.

**Tech Stack:** Arduino-ESP32 core 2.x (`analogRead`, `esp_adc_cal`),
TFT_eSPI (vendored in `lib/TFT_eSPI/`), PlatformIO (`pio run` to build).
No unit-test framework exists in this project (embedded/hardware-only) — no
automated test framework is being introduced by this plan either; every
task's verification step is `pio run` (compile) plus, where noted, a manual
hardware check, matching how every existing feature in this codebase is
verified.

## Global Constraints

- `BAT_ADC_PIN` = `34` (GPIO34, onboard 100K/100K divider — read value must
  be doubled).
- `BAT_MIN_MV` = `3300`, `BAT_MAX_MV` = `4200` — linear map to 0–100%,
  clamped outside this range.
- `BAT_LOW_PCT` = `15` — battery icon renders in `colorDestructive()`
  (red) at or below this percent, `TFT_GREEN` above it.
- `BAT_SAMPLE_COUNT` = `8` — number of `analogRead()` samples averaged per
  reading.
- Battery icon geometry is derived entirely from `_tft.width()` (never
  hardcoded per-orientation): clear rect `x=width()-72, y=2, w=50, h=12`;
  percent text `MR_DATUM` at `(width()-45, 8)`, `setTextFont(1)`; body via
  `drawProgressBar(width()-42, 4, 16, 8, pct, color)`; nub
  `fillRect(width()-26, 6, 2, 4, color)`.
- Every temporary `setTextFont`/`setTextColor`/`setTextDatum` change inside
  `drawBatteryIcon` must be reset (`setTextFont(0)`, `setTextDatum(TL_DATUM)`)
  before the function returns, matching the existing convention everywhere
  else in `Renderer.cpp`.
- No new `Event`, no new `AppState` field, no persistence (NVS) — the
  battery level is transient, Renderer-owned display state only.

---

### Task 1: Battery sensing module

**Files:**
- Modify: `src/Config.h`
- Create: `src/Battery.h`
- Create: `src/Battery.cpp`

**Interfaces:**
- Produces: `class Battery { public: void init(); int readPercent(); };` —
  `readPercent()` returns a clamped `0..100` int. This is the only surface
  Task 3 (main.cpp integration) consumes.

- [ ] **Step 1: Add battery constants to `Config.h`**

Append to `src/Config.h` (after the existing `PROXY_PROBE_TIMEOUT_MS`/
`PROXY_VALIDATE_TIMEOUT_MS` lines):

```cpp
// Battery level sensing — onboard 100K/100K divider on GPIO34.
// See docs/superpowers/specs/2026-07-05-battery-level-display-design.md
#define BAT_ADC_PIN      34
#define BAT_SAMPLE_COUNT 8
#define BAT_MIN_MV       3300  // maps to 0%
#define BAT_MAX_MV       4200  // maps to 100%
#define BAT_LOW_PCT      15    // battery icon renders red at/below this
```

- [ ] **Step 2: Create `src/Battery.h`**

```cpp
#pragma once
#include <Arduino.h>

// Owns ADC characterization and battery-percent conversion for the onboard
// GPIO34 voltage divider. Nothing else in the codebase touches this ADC.
class Battery {
public:
  void init();
  int  readPercent(); // averaged, clamped 0..100
};
```

- [ ] **Step 3: Create `src/Battery.cpp`**

```cpp
#include "Battery.h"
#include "Config.h"
#include <esp_adc_cal.h>

static esp_adc_cal_characteristics_t s_adcChars;

void Battery::init() {
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12,
                            1100, &s_adcChars);
}

int Battery::readPercent() {
  uint32_t sum = 0;
  for (int i = 0; i < BAT_SAMPLE_COUNT; i++) {
    sum += analogRead(BAT_ADC_PIN);
    delayMicroseconds(100);
  }
  uint32_t raw = sum / BAT_SAMPLE_COUNT;
  int mv = esp_adc_cal_raw_to_voltage(raw, &s_adcChars) * 2; // divider is 2:1

  if (mv <= BAT_MIN_MV) return 0;
  if (mv >= BAT_MAX_MV) return 100;
  return (mv - BAT_MIN_MV) * 100 / (BAT_MAX_MV - BAT_MIN_MV);
}
```

- [ ] **Step 4: Compile check**

Run: `pio run`
Expected: `SUCCESS` — `Battery.cpp` builds as a new translation unit (nothing
calls it yet, that's Task 3; PlatformIO compiles every `.cpp` under `src/`
regardless).

- [ ] **Step 5: Commit**

```bash
git add src/Config.h src/Battery.h src/Battery.cpp
git commit -m "Add Battery module for onboard ADC-based charge sensing"
```

---

### Task 2: Renderer battery icon drawing

**Files:**
- Modify: `src/Renderer.h`
- Modify: `src/Renderer.cpp`

**Interfaces:**
- Consumes: nothing new from Task 1 (this task doesn't call `Battery` —
  `Renderer` stays hardware-agnostic per the spec's SRP reasoning).
- Produces: `void Renderer::setBattery(int pct);` (public) — this is the
  only method Task 3 calls to both cache and display a reading.

- [ ] **Step 1: Add the private member and method declarations to `src/Renderer.h`**

Add `_batteryPct` next to the existing `_prev` member (both are
Renderer-owned display-state caches, same reasoning):

```cpp
  UsageData     _prev = { -1, -1, -1, -1, -1 };
  int           _batteryPct = 100;
```

Add the private drawing method next to the other private draw helpers (near
`drawRotateIcon`):

```cpp
  void drawBatteryIcon(int pct);
```

Add the public setter next to `drawWifiIndicator` in the public section:

```cpp
  void setBattery(int pct);
```

- [ ] **Step 2: Implement `drawBatteryIcon` in `src/Renderer.cpp`**

Add this new method right after `drawWifiIndicator` (around line 723):

```cpp
void Renderer::drawBatteryIcon(int pct) {
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  uint16_t color = (pct > BAT_LOW_PCT) ? TFT_GREEN : colorDestructive();

  // Clear the previous frame's text+icon in one step so a shrinking
  // percentage (e.g. "100%" -> "9%") never leaves a stray digit.
  _tft.fillRect(_tft.width() - 72, 2, 50, 12, TFT_BLACK);

  char buf[6];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  _tft.setTextFont(1);
  _tft.setTextColor(color);
  _tft.setTextDatum(MR_DATUM);
  _tft.drawString(buf, _tft.width() - 45, 8);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  drawProgressBar(_tft.width() - 42, 4, 16, 8, pct, color);
  _tft.fillRect(_tft.width() - 26, 6, 2, 4, color); // nub
}
```

- [ ] **Step 3: Implement `setBattery` in `src/Renderer.cpp`**

Add right after `drawBatteryIcon`:

```cpp
void Renderer::setBattery(int pct) {
  _batteryPct = pct;
  drawBatteryIcon(_batteryPct);
}
```

- [ ] **Step 4: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add src/Renderer.h src/Renderer.cpp
git commit -m "Add Renderer battery icon drawing (not yet wired to any screen)"
```

---

### Task 3: Wire the icon into every screen and feed it real readings

**Files:**
- Modify: `src/Renderer.cpp` (six one-line insertions)
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `Battery::init()` / `Battery::readPercent()` (Task 1),
  `Renderer::setBattery(int)` (Task 2).
- Produces: nothing new — this is the integration task where the feature
  becomes visible end-to-end.

- [ ] **Step 1: Add `drawBatteryIcon(_batteryPct);` to all six draw functions**

In `src/Renderer.cpp`, add the call immediately after each function's
`fillScreen(...)` line:

`drawClaude` (after line 206):
```cpp
  _tft.fillScreen(TFT_BLACK);
  drawBatteryIcon(_batteryPct);
```

`drawClaudePortrait` (after line 297):
```cpp
  _tft.fillScreen(TFT_BLACK);
  drawBatteryIcon(_batteryPct);
```

`drawGrok` (after line 384):
```cpp
  _tft.fillScreen(TFT_BLACK);
  drawBatteryIcon(_batteryPct);
```

`drawGrokPortrait` (after line 453):
```cpp
  _tft.fillScreen(TFT_BLACK);
  drawBatteryIcon(_batteryPct);
```

`drawSettings` (after line 523):
```cpp
  _tft.fillScreen(colorScreenBg());
  drawBatteryIcon(_batteryPct);
```

`drawSettingsPortrait` (after line 594):
```cpp
  _tft.fillScreen(colorScreenBg());
  drawBatteryIcon(_batteryPct);
```

- [ ] **Step 2: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 3: Wire `Battery` into `src/main.cpp`**

Add the include near the top, with the other project headers:

```cpp
#include "Battery.h"
```

Add the global instance next to the other global objects (after
`Renderer renderer;`):

```cpp
Battery     battery;
```

In `setup()`, right after `renderer.showConnecting();`:

```cpp
  battery.init();
  renderer.setBattery(battery.readPercent());
```

In `loop()`, as the first line inside the existing fetch-interval block
(so the reading refreshes on the same cadence as the data fetch,
independent of whether that fetch itself succeeds):

```cpp
  if (millis() - lastFetch > state.fetchInterval) {
    renderer.setBattery(battery.readPercent());
    if (fetcher.fetch(data)) {
```

- [ ] **Step 4: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 5: Flash and do a first visual check**

Run: `pio run -t upload` then `pio device monitor`
Expected: with the battery physically connected (see the hardware-wiring
section of the design spec — check JST pitch and polarity against JP2's
silkscreen before plugging in), a battery icon with a plausible percentage
appears in the top-right corner of the Claude screen on boot.

- [ ] **Step 6: Commit**

```bash
git add src/Renderer.cpp src/main.cpp
git commit -m "Wire battery icon into all screens and feed live ADC readings"
```

---

### Task 4: Full hardware verification pass

**Files:** none (verification only; may produce a follow-up fix, see Step 2)

**Interfaces:** none — this task only exercises what Tasks 1–3 built.

- [ ] **Step 1: Verify icon placement on all six screen/orientation combinations**

With the device flashed from Task 3, cycle through Claude, Grok, and
Settings, and toggle rotation (long-press-free tap on the Settings rotate
icon) through all four rotations. Confirm the battery icon:
- Never overlaps the WiFi dot, screen titles, the Claude-screen sprite, or
  the Settings header icon buttons.
- Renders identically positioned (relative to the right edge) across all
  three screens in a given orientation.

- [ ] **Step 2: Specifically check portrait Settings for the flagged collision risk**

Per the design spec's "Unverified risk" note: `drawSettingsPortrait`
centers `"SETTINGS"` at `x=178`, sized assuming it may use close to the
full width up to `x=240`. Check whether the title visually overlaps the
battery icon zone (`x≈168` onward) in portrait Settings specifically.

If it overlaps: narrow the title's centering zone in
`src/Renderer.cpp`'s `drawSettingsPortrait` from the full `114..240` zone to
`114..168`, i.e. change:
```cpp
  _tft.drawString("SETTINGS", 178, 22);
```
to:
```cpp
  _tft.drawString("SETTINGS", 141, 22); // (114+168)/2, narrowed to clear the battery icon
```
then re-flash and re-check this step.

- [ ] **Step 3: Verify percentage plausibility**

Compare the displayed percentage against a multimeter reading across the
battery terminals (allowing for the linear-approximation curve described in
the design spec — it won't match a fuel-gauge IC's accuracy, just needs to
be in the right ballpark and move in the right direction as the battery
charges/discharges).

- [ ] **Step 4: Verify USB plug/unplug continuity**

Unplug USB (board running on battery only), confirm the percentage doesn't
jump discontinuously; replug USB and confirm the same.

- [ ] **Step 5: Verify low-battery color threshold**

Temporarily set `BAT_LOW_PCT` to `95` in `src/Config.h`, re-flash, confirm
the icon and percentage text render in red (`colorDestructive()`) instead of
green. Revert `BAT_LOW_PCT` back to `15` and re-flash.

- [ ] **Step 6: Verify digit-shrink erasure**

Watch the icon across a reading where the percentage drops to a shorter
string (e.g. "100%" → "9%", or trigger it by briefly disconnecting the
battery during a read if safe to do so) — confirm no stray leftover digit
remains from the previous frame.

- [ ] **Step 7: Commit any Step 2 follow-up fix**

Only if Step 2 required a change:

```bash
git add src/Renderer.cpp
git commit -m "Narrow portrait Settings title zone to clear the battery icon"
```
