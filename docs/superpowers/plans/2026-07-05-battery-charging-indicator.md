# Battery Charging Indicator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a small lightning-bolt overlay on the existing battery icon
when the board is likely charging, inferred purely from a software
voltage-trend heuristic (no hardware modification), per
[`docs/superpowers/specs/2026-07-05-battery-charging-indicator-design.md`](../specs/2026-07-05-battery-charging-indicator-design.md).

**Architecture:** `Battery` gains a three-bucket (rising/flat/falling)
streak-based state machine, driven by the same averaged mV reading
`readPercent()` already takes each call — no new sampling, no new timer.
`Renderer::drawBatteryIcon`/`setBattery` gain a `bool charging` parameter and
draw a 2-segment white glyph inside the existing battery-body footprint when
true. `main.cpp` threads the new `Battery::isCharging()` value through both
existing `setBattery` call sites, splitting each into two statements to
avoid an argument-evaluation-order hazard (detailed in Task 3).

**Tech Stack:** Arduino-ESP32 core 2.x, TFT_eSPI (vendored), PlatformIO. No
unit-test framework exists in this project — every task's verification is
`pio run` (compile) plus, for Task 4, manual hardware checks.

## Global Constraints

- `BAT_CHG_RISE_MV` = `15` — mV delta beyond which a reading counts as
  "rising"/"falling" (else "flat").
- `BAT_CHG_RISE_STREAK` = `3` — consecutive rising reads required to enter
  `_charging = true`.
- `BAT_CHG_FALL_STREAK` = `2` — consecutive falling reads required to exit
  `_charging = false`.
- A **flat** reading never changes `_charging` (stays true if already
  charging — this is what survives the CV-taper period) but resets both
  streak counters to 0.
- `_lastMv` sentinel is `-1`; the very first reading ever skips
  classification entirely (nothing to diff against).
- `readPercent()`'s public signature and return semantics are unchanged —
  it still returns a clamped `0..100` int. `isCharging()` is purely additive.
- `main.cpp`'s two `setBattery` call sites **must** call `battery.readPercent()`
  into a local variable *before* calling `battery.isCharging()` in a separate
  statement — C++ does not guarantee argument-evaluation order, so passing
  both as arguments to the same call risks reading stale charging state.
- Battery-body geometry is unchanged (`drawProgressBar(width()-42, 4, 16, 8, pct, color)`,
  border rows `y=4` and `y=11`, fill interior rows `y=5..10`) — the bolt glyph
  must stay within `y=5..10` (the interior band), not the border rows.
- No new files, no persistence (NVS), no change to the existing level-color
  logic (`TFT_GREEN`/`colorDestructive()` by `BAT_LOW_PCT`).

---

### Task 1: Battery charging detection

**Files:**
- Modify: `src/Config.h`
- Modify: `src/Battery.h`
- Modify: `src/Battery.cpp`

**Interfaces:**
- Produces: `bool Battery::isCharging() const;` — reflects state as of the
  last `readPercent()` call. This is the only new surface Task 3 (main.cpp
  integration) consumes. `readPercent()`'s existing signature is unchanged.

- [ ] **Step 1: Add charging-detection constants to `src/Config.h`**

Append after the existing `BAT_LOW_PCT` line (`src/Config.h:25`):

```cpp
#define BAT_CHG_RISE_MV     15  // mV delta beyond which a reading counts as "rising"/"falling" (else "flat")
#define BAT_CHG_RISE_STREAK 3   // consecutive rising reads required to enter "charging"
#define BAT_CHG_FALL_STREAK 2   // consecutive falling reads required to exit "charging"
```

- [ ] **Step 2: Update `src/Battery.h`**

Replace the entire file with:

```cpp
#pragma once
#include <Arduino.h>

// Owns ADC characterization and battery-percent conversion for the onboard
// GPIO34 voltage divider. Nothing else in the codebase touches this ADC.
class Battery {
public:
  void init();
  int  readPercent();      // averaged, clamped 0..100; also updates charging state as a side effect
  bool isCharging() const; // reflects state as of the last readPercent() call

private:
  int  readMilliVoltsAveraged(); // the averaging loop, used once per readPercent() call
  void classify(int mv);         // three-bucket rising/flat/falling streak logic

  int  _lastMv        = -1; // sentinel: no previous reading yet
  int  _risingStreak  = 0;
  int  _fallingStreak = 0;
  bool _charging      = false;
};
```

- [ ] **Step 3: Update `src/Battery.cpp`**

Replace the entire file with:

```cpp
#include "Battery.h"
#include "Config.h"
#include <esp_adc_cal.h>

static esp_adc_cal_characteristics_t s_adcChars;

void Battery::init() {
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12,
                            1100, &s_adcChars);
}

int Battery::readMilliVoltsAveraged() {
  uint32_t sum = 0;
  for (int i = 0; i < BAT_SAMPLE_COUNT; i++) {
    sum += analogRead(BAT_ADC_PIN);
    delayMicroseconds(100);
  }
  uint32_t raw = sum / BAT_SAMPLE_COUNT;
  return esp_adc_cal_raw_to_voltage(raw, &s_adcChars) * 2; // divider is 2:1
}

void Battery::classify(int mv) {
  if (_lastMv >= 0) {
    int delta = mv - _lastMv;
    if (delta > BAT_CHG_RISE_MV) {
      _risingStreak++;
      _fallingStreak = 0;
      if (_risingStreak >= BAT_CHG_RISE_STREAK) _charging = true;
    } else if (delta < -BAT_CHG_RISE_MV) {
      _fallingStreak++;
      _risingStreak = 0;
      if (_fallingStreak >= BAT_CHG_FALL_STREAK) _charging = false;
    } else {
      // Flat: sticky through CV taper — don't touch _charging, but a flat
      // sample isn't part of either streak, so it resets both.
      _risingStreak = 0;
      _fallingStreak = 0;
    }
  }
  _lastMv = mv;
}

int Battery::readPercent() {
  int mv = readMilliVoltsAveraged();
  classify(mv);

  if (mv <= BAT_MIN_MV) return 0;
  if (mv >= BAT_MAX_MV) return 100;
  return (mv - BAT_MIN_MV) * 100 / (BAT_MAX_MV - BAT_MIN_MV);
}

bool Battery::isCharging() const { return _charging; }
```

- [ ] **Step 4: Compile check**

Run: `pio run`
Expected: `SUCCESS` — nothing calls `isCharging()` yet (that's Task 3), but
`Battery.cpp` must still compile cleanly as its own translation unit.

- [ ] **Step 5: Commit**

```bash
git add src/Config.h src/Battery.h src/Battery.cpp
git commit -m "Add voltage-trend charging detection to Battery"
```

---

### Task 2: Renderer bolt overlay

**Files:**
- Modify: `src/Renderer.h`
- Modify: `src/Renderer.cpp`

**Interfaces:**
- Consumes: nothing new from Task 1 (Renderer never touches `Battery`
  directly, same SRP boundary as the level-indicator feature).
- Produces: `void Renderer::setBattery(int pct, bool charging);` (public,
  signature change) — the method Task 3 calls. `void Renderer::drawBatteryIcon(int pct, bool charging);`
  (private, signature change) — called internally by `setBattery` and by
  each of the six existing screen-draw functions.

- [ ] **Step 1: Update `src/Renderer.h`**

Change (`src/Renderer.h:10`, unchanged, just noting context):
```cpp
  int           _batteryPct = 100;
```
Add immediately after it:
```cpp
  bool          _batteryCharging = false;
```

Change (`src/Renderer.h:33`):
```cpp
  void drawBatteryIcon(int pct);
```
to:
```cpp
  void drawBatteryIcon(int pct, bool charging);
```

Change (`src/Renderer.h:68`):
```cpp
  void setBattery(int pct);
```
to:
```cpp
  void setBattery(int pct, bool charging);
```

- [ ] **Step 2: Update `drawBatteryIcon` in `src/Renderer.cpp` (currently `src/Renderer.cpp:731-751`)**

Replace the whole method:

```cpp
void Renderer::drawBatteryIcon(int pct, bool charging) {
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

  if (charging) {
    // drawProgressBar's border occupies rows y=4 and y=11 of this 16x8
    // body; the fill interior is rows y=5..10. Keep the bolt inside that
    // interior band so it doesn't touch the border stroke.
    int bx = _tft.width() - 42; // body's left edge, matches drawProgressBar's x above
    _tft.drawLine(bx + 9, 5,  bx + 6, 8,  TFT_WHITE);
    _tft.drawLine(bx + 6, 8,  bx + 10, 10, TFT_WHITE);
  }
}
```

- [ ] **Step 3: Update `setBattery` in `src/Renderer.cpp` (currently `src/Renderer.cpp:753-756`)**

Replace:

```cpp
void Renderer::setBattery(int pct, bool charging) {
  _batteryPct = pct;
  _batteryCharging = charging;
  drawBatteryIcon(_batteryPct, _batteryCharging);
}
```

- [ ] **Step 4: Update the six existing call sites**

Each of these currently reads `drawBatteryIcon(_batteryPct);` — change each
to `drawBatteryIcon(_batteryPct, _batteryCharging);`. Current line numbers
(find by function name if these have shifted — each call is the single line
immediately after that function's `fillScreen(...)`):

- `drawClaude` — `src/Renderer.cpp:207`
- `drawClaudePortrait` — `src/Renderer.cpp:299`
- `drawGrok` — `src/Renderer.cpp:387`
- `drawGrokPortrait` — `src/Renderer.cpp:457`
- `drawSettings` — `src/Renderer.cpp:528`
- `drawSettingsPortrait` — `src/Renderer.cpp:600`

- [ ] **Step 5: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 6: Commit**

```bash
git add src/Renderer.h src/Renderer.cpp
git commit -m "Add charging bolt overlay to battery icon rendering"
```

---

### Task 3: Wire `isCharging()` into `main.cpp`

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `bool Battery::isCharging() const` (Task 1),
  `void Renderer::setBattery(int pct, bool charging)` (Task 2).
- Produces: nothing new — this is the final integration point where the
  bolt becomes visible end-to-end.

- [ ] **Step 1: Update the `setup()` call site (currently `src/main.cpp:87-88`)**

Replace:
```cpp
  battery.init();
  renderer.setBattery(battery.readPercent());
```
with:
```cpp
  battery.init();
  int pct = battery.readPercent();
  renderer.setBattery(pct, battery.isCharging());
```

- [ ] **Step 2: Update the `loop()` call site (currently `src/main.cpp:192-193`)**

Replace:
```cpp
  if (millis() - lastFetch > state.fetchInterval) {
    renderer.setBattery(battery.readPercent());
```
with:
```cpp
  if (millis() - lastFetch > state.fetchInterval) {
    int pct = battery.readPercent();
    renderer.setBattery(pct, battery.isCharging());
```

Both call sites split the single-expression call into two statements
**because C++ does not guarantee left-to-right evaluation of function
arguments** — writing `renderer.setBattery(battery.readPercent(), battery.isCharging())`
directly risks `isCharging()` evaluating before `readPercent()` updates the
streak state, silently reading the previous cycle's charging flag. Storing
`pct` first removes the ambiguity.

- [ ] **Step 3: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 4: Flash and do a first visual check (if hardware is available)**

Run: `pio run -t upload` then `pio device monitor`
Expected: with the battery connected and USB-C plugged in, after a few
fetch-interval ticks the battery icon shows a white bolt overlay. If no
device is available to this implementer, skip this step and note it in the
report — it is not a blocker for this task (Task 4 is the dedicated
hardware-verification task).

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "Wire Battery::isCharging() into main.cpp's setBattery calls"
```

---

### Task 4: Hardware verification pass

**Files:** none (verification only)

**Interfaces:** none — this task only exercises what Tasks 1–3 built.

- [ ] **Step 0: Sanity-check the underlying battery percentage first**

The battery-*level* feature (a separate, earlier plan) has its own hardware
verification pass that is still pending as of this writing — meaning this
is also the first time the raw percentage reading has been checked against
real hardware at all. Before tuning any charging thresholds below, confirm
the displayed percentage roughly tracks a multimeter reading across the
battery terminals. If the level reading itself is off, charging-detection
tuning in Step 6 would be chasing a moving target — fix the level reading
first (see the battery-level-display plan's own Task 4) before proceeding.

- [ ] **Step 1: Plug in USB-C while running on battery and time the bolt's appearance**

Confirm the bolt appears after a handful of fetch-interval ticks (not
instantly — inherent to requiring `BAT_CHG_RISE_STREAK` (3) consecutive
rising reads). If it never appears despite clearly charging, or appears
immediately/flickers on and off, `BAT_CHG_RISE_MV`/`BAT_CHG_RISE_STREAK` in
`src/Config.h` likely need retuning against this board's actual ADC noise
(no on-device noise measurement exists yet — this is expected tuning work,
not a bug to report upstream).

- [ ] **Step 2: Confirm the bolt renders legibly against both fill colors**

Check the bolt is visible both when the battery body is green (charged,
above `BAT_LOW_PCT`) and red (`colorDestructive()`, at/below `BAT_LOW_PCT`)
— i.e. plug in a low battery and confirm both signals ("low" and
"charging") are visible simultaneously.

- [ ] **Step 3: Unplug USB-C while the bolt is showing**

Confirm the bolt disappears within roughly `BAT_CHG_FALL_STREAK` (2)
fetch-interval ticks of the battery visibly starting to discharge under
the display's own load.

- [ ] **Step 4: Leave it charging until the battery reaches ~100%**

Confirm the known, accepted limitation: the bolt is expected to keep
showing for a while into the CV-taper flat period (voltage nearly flat,
still slowly topping off), not disappear the instant charging current
tapers — this is inherent to voltage-only sensing, not a bug.

- [ ] **Step 5: Confirm no new visual collisions**

Check the bolt doesn't visually clash with anything on at least one
screen/orientation — it stays within the existing battery-icon footprint
so this should be a no-op, but confirm since it's a new visual element on
top of an icon whose surrounding clearances were already verified by the
level-indicator feature's own hardware pass.

- [ ] **Step 6: Retune constants if needed**

If Step 1 revealed flickering or non-triggering, adjust `BAT_CHG_RISE_MV`,
`BAT_CHG_RISE_STREAK`, or `BAT_CHG_FALL_STREAK` in `src/Config.h`, re-flash,
and repeat Steps 1–4. Commit any constant changes:

```bash
git add src/Config.h
git commit -m "Retune charging-detection thresholds based on hardware observation"
```
