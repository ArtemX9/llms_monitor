# Screen Rotation & Orientation Support — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user change the display orientation from Settings across all four TFT_eSPI rotations (both landscapes, both portraits) with correct touch mapping, persisted across reboot.

**Architecture:** Two geometries only — landscape 320×240 (rotations 1,3) and portrait 240×320 (rotations 0,2). Every screen keeps its existing landscape code and gains a parallel portrait path selected by an inline `portrait()` check (no Layout-struct refactor). Rotation and per-rotation touch calibration persist in NVS. Touch calibration for all four rotations is derived in code from the known-good rotation-3 baseline, with a hidden long-press "recalibrate" gesture that runs TFT_eSPI's `calibrateTouch()` and saves the result.

**Tech Stack:** C++ (Arduino/ESP32), PlatformIO, TFT_eSPI (vendored in `lib/`), ESP32 `Preferences` (NVS).

## Global Constraints

- **No automated test harness exists.** Verification per task = `pio run` compiles with **zero errors** + a specific on-device observation. There is no pytest/unit-test cycle.
- **ESP32 Arduino core 2.x LEDC 3-call API** — do NOT use `ledcAttach()`. (Unchanged by this plan.)
- **No double buffering** — never "clear a big rect then redraw over it" as two steps; clear only the exact region that changes. Partial-update coords must match their full-draw counterpart for the same geometry.
- **`pushImage()` needs pre-swapped color bytes; `fillRect()` does not.** (Relevant only to AnimatedSprite.)
- **`USER_SETUP_LOADED=1`** stays in `platformio.ini`; touch/driver config comes only from build_flags + the vendored `lib/TFT_eSPI`.
- **Touch calData format** = `{x0, xSpan, y0, ySpan, flag}`, flag bits = `rotate | invert_x<<1 | invert_y<<2`. `convertRawXY` uses the *current* rotation's width/height.
- **Rotation values** are raw TFT_eSPI rotations `0..3`. Default rotation is `3`.
- Build command: `pio run`. Upload: `pio run -t upload`. Serial: `pio device monitor`.

---

### Task 1: NVS config helper for rotation + per-rotation calibration

**Files:**
- Create: `src/NvsConfig.h`
- Create: `src/NvsConfig.cpp`

**Interfaces:**
- Consumes: nothing (leaf module).
- Produces:
  - `uint8_t NvsConfig::loadRotation(uint8_t def);`
  - `void    NvsConfig::saveRotation(uint8_t rot);`
  - `bool    NvsConfig::loadCal(uint8_t rot, uint16_t out[5]);` — returns `true` and fills `out` if a saved blob exists for that rotation; `false` otherwise.
  - `void    NvsConfig::saveCal(uint8_t rot, const uint16_t data[5]);`

Uses the existing `netcfg` NVS namespace (already used for `proxyIp`), keys `rot` (uint8) and `cal0`…`cal3` (10-byte blobs). Standalone so `main.cpp` and `Renderer` can use it without depending on `DataFetcher`.

- [ ] **Step 1: Create `src/NvsConfig.h`**

```cpp
#pragma once
#include <Arduino.h>

// Thin wrapper over the "netcfg" NVS namespace for display orientation and
// per-rotation XPT2046 touch calibration overrides. Separate from DataFetcher
// so both main.cpp and Renderer can use it without cross-dependencies.
namespace NvsConfig {
  uint8_t loadRotation(uint8_t def);            // key "rot"; returns def if unset
  void    saveRotation(uint8_t rot);

  // Per-rotation calibration override. Key "cal0".."cal3", each a 10-byte blob
  // (5 x uint16_t). loadCal returns false if nothing is stored for that rotation.
  bool    loadCal(uint8_t rot, uint16_t out[5]);
  void    saveCal(uint8_t rot, const uint16_t data[5]);
}
```

- [ ] **Step 2: Create `src/NvsConfig.cpp`**

```cpp
#include "NvsConfig.h"
#include <Preferences.h>

namespace {
  void calKey(uint8_t rot, char* buf, size_t n) {
    snprintf(buf, n, "cal%u", (unsigned)(rot & 0x03));
  }
}

uint8_t NvsConfig::loadRotation(uint8_t def) {
  Preferences prefs;
  prefs.begin("netcfg", true);
  uint8_t r = prefs.getUChar("rot", def);
  prefs.end();
  if (r > 3) r = def;
  return r;
}

void NvsConfig::saveRotation(uint8_t rot) {
  Preferences prefs;
  prefs.begin("netcfg", false);
  prefs.putUChar("rot", rot & 0x03);
  prefs.end();
}

bool NvsConfig::loadCal(uint8_t rot, uint16_t out[5]) {
  char key[8];
  calKey(rot, key, sizeof(key));
  Preferences prefs;
  prefs.begin("netcfg", true);
  size_t got = prefs.getBytes(key, out, 5 * sizeof(uint16_t));
  prefs.end();
  return got == 5 * sizeof(uint16_t);
}

void NvsConfig::saveCal(uint8_t rot, const uint16_t data[5]) {
  char key[8];
  calKey(rot, key, sizeof(key));
  Preferences prefs;
  prefs.begin("netcfg", false);
  prefs.putBytes(key, data, 5 * sizeof(uint16_t));
  prefs.end();
}
```

- [ ] **Step 3: Build**

Run: `pio run`
Expected: compiles with zero errors. (The new file isn't referenced yet; this just confirms it compiles.)

- [ ] **Step 4: Commit**

```bash
git add src/NvsConfig.h src/NvsConfig.cpp
git commit -m "Add NvsConfig helper for rotation + per-rotation touch calibration"
```

---

### Task 2: Rotation state, derived calibration, and Renderer::setRotation

**Files:**
- Modify: `src/Types.h` (add `rotation` field to `AppState`)
- Modify: `src/Renderer.h` (add `_rotation`, `portrait()`, `setRotation()`, `applyTouchCalibration()`, change `init()` signature)
- Modify: `src/Renderer.cpp` (implement the above; init applies rotation instead of hardcoding 3)
- Modify: `src/main.cpp` (load rotation from NVS at boot, pass to `init`)

**Interfaces:**
- Consumes: `NvsConfig::loadRotation`, `NvsConfig::loadCal` (Task 1).
- Produces:
  - `AppState.rotation` (`uint8_t`, default 3).
  - `void Renderer::init(uint8_t rotation, uint8_t brightness);` (signature CHANGED — was `init(uint8_t brightness)`).
  - `void Renderer::setRotation(uint8_t rotation);` — sets `_tft` rotation and applies that rotation's touch calibration. Does NOT clear the screen (caller redraws).
  - `bool Renderer::portrait() const;` — `true` for rotations 0 and 2.

- [ ] **Step 1: Add `rotation` to `AppState` in `src/Types.h`**

Add the field to the `AppState` struct (after `ledEnabled`):

```cpp
  bool          ledEnabled      = true;
  uint8_t       rotation        = 3;   // raw TFT_eSPI rotation 0..3 (3 = landscape default)
  unsigned long rebootArmedAt   = 0; // 0 = disarmed; set to millis() on first reboot-icon tap
```

- [ ] **Step 2: Update `src/Renderer.h`**

Add the include and members. Add near the top of the class (private section, after `AnimatedSprite _sprite;`):

```cpp
  uint8_t _rotation = 3;
  bool portrait() const { return _rotation == 0 || _rotation == 2; }
  void applyTouchCalibration(uint8_t rotation);
```

Change the `init` declaration and add `setRotation` in the `public:` section:

```cpp
  void init(uint8_t rotation, uint8_t brightness);
  void setRotation(uint8_t rotation);
```

Add the include at the top of the file (after the existing includes):

```cpp
#include "NvsConfig.h"
```

- [ ] **Step 3: Rewrite `Renderer::init` and add `setRotation` / `applyTouchCalibration` in `src/Renderer.cpp`**

Replace the existing `init` (lines ~8-20) with:

```cpp
void Renderer::init(uint8_t rotation, uint8_t brightness) {
  _tft.init();
  setRotation(rotation);
  _tft.fillScreen(TFT_BLACK);
  ledcSetup(BL_CHANNEL, BL_FREQ, BL_RES);
  ledcAttachPin(TFT_BL_PIN, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, brightness);
}

void Renderer::setRotation(uint8_t rotation) {
  _rotation = rotation & 0x03;
  _tft.setRotation(_rotation);
  applyTouchCalibration(_rotation);
}

// Derive the 5-word touch calibration for each rotation from the known-good
// rotation-3 baseline {433, 3490, 314, 3448, 5}. The XPT2046 raw ADC bounds are
// physically fixed; only axis roles (rotate bit) and invert bits change.
//   flag bits: rotate | invert_x<<1 | invert_y<<2
//   rotation 3 (given): rotate=1, invert_x=0, invert_y=1  -> 0b101 = 5
// Landscape (1,3): rotate=1, calData uses {raw_y range, raw_x range}.
// Portrait  (0,2): rotate=0, axis roles swap -> {raw_x range, raw_y range}.
// The portrait invert bits (base rotation 0) are the uncertain part; if a
// rotation reads mirrored on hardware, the long-press recalibrate gesture
// (Task 7) overrides these permanently via NVS.
void Renderer::applyTouchCalibration(uint8_t rotation) {
  uint16_t cal[5];
  if (!NvsConfig::loadCal(rotation, cal)) {
    switch (rotation) {
      case 3: cal[0]=433; cal[1]=3490; cal[2]=314; cal[3]=3448; cal[4]=0b101; break;
      case 1: cal[0]=433; cal[1]=3490; cal[2]=314; cal[3]=3448; cal[4]=0b011; break;
      case 0: cal[0]=314; cal[1]=3448; cal[2]=433; cal[3]=3490; cal[4]=0b000; break;
      case 2: cal[0]=314; cal[1]=3448; cal[2]=433; cal[3]=3490; cal[4]=0b110; break;
      default: cal[0]=433; cal[1]=3490; cal[2]=314; cal[3]=3448; cal[4]=0b101; break;
    }
  }
  _tft.setTouch(cal);
}
```

- [ ] **Step 4: Update boot wiring in `src/main.cpp`**

Change the `renderer.init` call in `setup()` (currently `renderer.init(state.brightness);`) to load rotation from NVS first:

```cpp
  state.rotation = NvsConfig::loadRotation(3);
  renderer.init(state.rotation, state.brightness);
```

Add the include near the top of `src/main.cpp` (after `#include "Renderer.h"`):

```cpp
#include "NvsConfig.h"
```

- [ ] **Step 5: Build**

Run: `pio run`
Expected: compiles with zero errors.

- [ ] **Step 6: On-device check (landscape 180° flip)**

Temporarily force rotation 1 to prove touch calibration follows rotation: in `setup()`, right after the `loadRotation` line, add `state.rotation = 1;` (a temporary line). Upload (`pio run -t upload`), and confirm:
- All screens still render correctly (rotation 1 is landscape 320×240 — same layout, flipped 180°). The image is upside-down relative to rotation 3, which is expected.
- Touch still hits the correct on-screen controls (nav taps, brightness ±). If touch is mirrored, note it for the Task 7 recalibrate path but continue.
Then REMOVE the temporary `state.rotation = 1;` line and re-upload to confirm default (rotation 3) is unchanged from before this task.

- [ ] **Step 7: Commit**

```bash
git add src/Types.h src/Renderer.h src/Renderer.cpp src/main.cpp
git commit -m "Add rotation state, derived per-rotation touch calibration, Renderer::setRotation"
```

---

### Task 3: Portrait layout for the Claude screen

**Files:**
- Modify: `src/Renderer.h` (declare `drawClaudePortrait`, `updateClaudePortrait`)
- Modify: `src/Renderer.cpp` (dispatch + implement portrait Claude draw/update)

**Interfaces:**
- Consumes: `portrait()` (Task 2).
- Produces: portrait rendering for screen 0. No new public API.

Portrait canvas is 240 wide × 320 tall. Coordinates below are a working starting layout; nudge on device if anything clips.

- [ ] **Step 1: Declare the portrait methods in `src/Renderer.h`**

In the private section, next to `drawClaude`/`updateClaude`:

```cpp
  void drawClaudePortrait(const UsageData& d);
  void updateClaudePortrait(const UsageData& d);
```

- [ ] **Step 2: Dispatch to portrait at the top of `drawClaude` and `updateClaude`**

At the very start of `Renderer::drawClaude`, before `char buf[24];`:

```cpp
  if (portrait()) { drawClaudePortrait(d); return; }
```

At the very start of `Renderer::updateClaude`:

```cpp
  if (portrait()) { updateClaudePortrait(d); return; }
```

- [ ] **Step 3: Implement `drawClaudePortrait` in `src/Renderer.cpp`** (add after `updateClaude`)

```cpp
void Renderer::drawClaudePortrait(const UsageData& d) {
  char buf[24];
  _tft.fillScreen(TFT_BLACK);

  // Header: sprite + title
  _sprite.draw(_tft);
  _tft.setFreeFont(TITLE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("Usage", 120, 22);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);
  _tft.drawFastHLine(0, 44, 240, TFT_DARKGREY);

  // Session
  uint16_t cSession = progressColor(d.claudeSession);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(cSession);
  _tft.drawString(buf, 10, 56);
  _tft.setTextFont(0);
  drawPill(136, 56, 94, 28, "Session");
  drawProgressBar(10, 92, 220, 14, d.claudeSession, cSession);
  formatReset(buf, sizeof(buf), d.claudeReset);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString(buf, 10, 112, 2);
  _tft.drawFastHLine(0, 140, 240, TFT_DARKGREY);

  // Weekly
  uint16_t cWeekly = progressColor(d.claudeWeekly);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(cWeekly);
  _tft.drawString(buf, 10, 152);
  _tft.setTextFont(0);
  drawPill(136, 152, 94, 28, "Weekly");
  drawProgressBar(10, 188, 220, 14, d.claudeWeekly, cWeekly);

  // Nav buttons pinned to bottom
  _tft.drawFastHLine(0, 272, 240, TFT_DARKGREY);
  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(6,  282, 112, 32, "< Set",  f, b, TFT_WHITE);
    drawButton(122, 282, 112, 32, "Grok >", f, b, TFT_WHITE);
  }

  _prev.claudeSession = d.claudeSession;
  _prev.claudeWeekly  = d.claudeWeekly;
  _prev.claudeReset   = d.claudeReset;
}
```

- [ ] **Step 4: Implement `updateClaudePortrait` in `src/Renderer.cpp`** (add right after `drawClaudePortrait`)

```cpp
void Renderer::updateClaudePortrait(const UsageData& d) {
  char buf[24];
  if (d.claudeSession != _prev.claudeSession) {
    uint16_t c = progressColor(d.claudeSession);
    _tft.fillRect(10, 56, 120, 28, TFT_BLACK);
    _tft.setFreeFont(VALUE_FONT);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
    _tft.drawString(buf, 10, 56);
    _tft.setTextFont(0);
    drawProgressBar(10, 92, 220, 14, d.claudeSession, c);
    _prev.claudeSession = d.claudeSession;
  }
  if (d.claudeReset != _prev.claudeReset) {
    _tft.fillRect(10, 112, 220, 16, TFT_BLACK);
    _tft.setTextColor(TFT_DARKGREY);
    formatReset(buf, sizeof(buf), d.claudeReset);
    _tft.drawString(buf, 10, 112, 2);
    _prev.claudeReset = d.claudeReset;
  }
  if (d.claudeWeekly != _prev.claudeWeekly) {
    uint16_t c = progressColor(d.claudeWeekly);
    _tft.fillRect(10, 152, 120, 28, TFT_BLACK);
    _tft.setFreeFont(VALUE_FONT);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
    _tft.drawString(buf, 10, 152);
    _tft.setTextFont(0);
    drawProgressBar(10, 188, 220, 14, d.claudeWeekly, c);
    _prev.claudeWeekly = d.claudeWeekly;
  }
}
```

- [ ] **Step 5: Build**

Run: `pio run`
Expected: compiles with zero errors.

- [ ] **Step 6: On-device check (portrait Claude)**

Temporarily add `state.rotation = 0;` after the `loadRotation` line in `setup()`, upload, and confirm the Claude screen renders upright in portrait: header, both percentages, pills, progress bars, reset text, and nav buttons all visible and un-clipped within 240×320. Touch nav may be imperfect until Task 5/7; that's fine. Remove the temporary line after checking (or leave it to test Task 4 next, then remove).

- [ ] **Step 7: Commit**

```bash
git add src/Renderer.h src/Renderer.cpp
git commit -m "Add portrait layout for Claude screen"
```

---

### Task 4: Portrait layout for the Grok screen

**Files:**
- Modify: `src/Renderer.h` (declare `drawGrokPortrait`, `updateGrokPortrait`)
- Modify: `src/Renderer.cpp` (dispatch + implement)

**Interfaces:**
- Consumes: `portrait()` (Task 2).
- Produces: portrait rendering for screen 1. No new public API.

- [ ] **Step 1: Declare in `src/Renderer.h`** (next to `drawGrok`/`updateGrok`)

```cpp
  void drawGrokPortrait(const UsageData& d);
  void updateGrokPortrait(const UsageData& d);
```

- [ ] **Step 2: Dispatch at the top of `drawGrok` and `updateGrok`**

At the start of `Renderer::drawGrok` (before `char buf[8];`):

```cpp
  if (portrait()) { drawGrokPortrait(d); return; }
```

At the start of `Renderer::updateGrok`:

```cpp
  if (portrait()) { updateGrokPortrait(d); return; }
```

- [ ] **Step 3: Implement `drawGrokPortrait` in `src/Renderer.cpp`** (after `updateGrok`)

```cpp
void Renderer::drawGrokPortrait(const UsageData& d) {
  char buf[8];
  _tft.fillScreen(TFT_BLACK);
  _tft.setFreeFont(TITLE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("GROK BUILD", 10, 12);
  _tft.setTextFont(0);
  _tft.drawFastHLine(0, 44, 240, TFT_DARKGREY);

  uint16_t cTokens = progressColor(d.grokTokens);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Tokens", 10, 62, 2);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(cTokens);
  snprintf(buf, sizeof(buf), "%d%%", d.grokTokens);
  _tft.drawString(buf, 170, 54);
  _tft.setTextFont(0);
  drawProgressBar(10, 92, 220, 14, d.grokTokens, cTokens);

  _tft.drawFastHLine(0, 128, 240, TFT_DARKGREY);

  uint16_t cReqs = progressColor(d.grokRequests);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Requests", 10, 146, 2);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(cReqs);
  snprintf(buf, sizeof(buf), "%d%%", d.grokRequests);
  _tft.drawString(buf, 170, 138);
  _tft.setTextFont(0);
  drawProgressBar(10, 176, 220, 14, d.grokRequests, cReqs);

  _tft.drawFastHLine(0, 272, 240, TFT_DARKGREY);
  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(6,   282, 112, 32, "< Claude", f, b, TFT_WHITE);
    drawButton(122, 282, 112, 32, "Set >",    f, b, TFT_WHITE);
  }

  _prev.grokTokens   = d.grokTokens;
  _prev.grokRequests = d.grokRequests;
}
```

- [ ] **Step 4: Implement `updateGrokPortrait` in `src/Renderer.cpp`** (after `drawGrokPortrait`)

```cpp
void Renderer::updateGrokPortrait(const UsageData& d) {
  char buf[8];
  if (d.grokTokens != _prev.grokTokens) {
    uint16_t c = progressColor(d.grokTokens);
    _tft.fillRect(170, 54, 66, 28, TFT_BLACK);
    _tft.setFreeFont(VALUE_FONT);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.grokTokens);
    _tft.drawString(buf, 170, 54);
    _tft.setTextFont(0);
    drawProgressBar(10, 92, 220, 14, d.grokTokens, c);
    _prev.grokTokens = d.grokTokens;
  }
  if (d.grokRequests != _prev.grokRequests) {
    uint16_t c = progressColor(d.grokRequests);
    _tft.fillRect(170, 138, 66, 28, TFT_BLACK);
    _tft.setFreeFont(VALUE_FONT);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.grokRequests);
    _tft.drawString(buf, 170, 138);
    _tft.setTextFont(0);
    drawProgressBar(10, 176, 220, 14, d.grokRequests, c);
    _prev.grokRequests = d.grokRequests;
  }
}
```

- [ ] **Step 5: Build**

Run: `pio run`
Expected: compiles with zero errors.

- [ ] **Step 6: On-device check (portrait Grok)**

With a temporary `state.rotation = 0;` in `setup()`, upload and navigate to the Grok screen (or set `state.screen = 1` temporarily). Confirm Tokens/Requests labels, percentages, both progress bars, and nav buttons render un-clipped in portrait. Remove any temporary lines when done.

- [ ] **Step 7: Commit**

```bash
git add src/Renderer.h src/Renderer.cpp
git commit -m "Add portrait layout for Grok screen"
```

---

### Task 5: Portrait Settings + geometry-safe status screens, brightness bar, intervals, WiFi dot

**Files:**
- Modify: `src/Renderer.h` (declare `drawSettingsPortrait`, `drawIntervalButtonsPortrait`)
- Modify: `src/Renderer.cpp` (dispatch + implement portrait Settings; make status screens & wifi dot geometry-aware; portrait branches in `updateBrightnessBar`/`updateIntervalButtons`)

**Interfaces:**
- Consumes: `portrait()` (Task 2); header-icon draws `drawRebootIcon`, `drawLedToggle` (shared coords, unchanged).
- Produces: portrait Settings rendering + portrait partial-update paths. No new public API. NOTE: the rotate icon is added in Task 7, not here.

Header icons (`drawRebootIcon` at (6,6,32,32), `drawLedToggle` bulb around (60,19)) sit in the top-left and fit both geometries unchanged — do NOT modify them here.

- [ ] **Step 1: Declare in `src/Renderer.h`**

```cpp
  void drawSettingsPortrait(uint8_t brightness, unsigned long fetchInterval, bool ledEnabled);
  void drawIntervalButtonsPortrait(unsigned long fetchInterval);
```

- [ ] **Step 2: Make status screens geometry-safe in `src/Renderer.cpp`**

Replace the four status-screen functions so they center using `_tft.width()`/`_tft.height()` instead of hardcoded landscape offsets:

```cpp
void Renderer::showConnecting() {
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("Connecting...", _tft.width()/2, _tft.height()/2, 4);
  _tft.setTextDatum(TL_DATUM);
}

void Renderer::showWifiFailed() {
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextDatum(MC_DATUM);
  _tft.setTextColor(TFT_RED);
  _tft.drawString("WiFi failed", _tft.width()/2, _tft.height()/2 - 20, 4);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString("Retrying in 30s...", _tft.width()/2, _tft.height()/2 + 20, 2);
  _tft.setTextDatum(TL_DATUM);
}

void Renderer::showServerError() {
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextDatum(MC_DATUM);
  _tft.setTextColor(TFT_RED);
  _tft.drawString("Server error", _tft.width()/2, _tft.height()/2 - 20, 4);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString("Retrying...", _tft.width()/2, _tft.height()/2 + 20, 2);
  _tft.setTextDatum(TL_DATUM);
}

void Renderer::showRebooting() {
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("Rebooting...", _tft.width()/2, _tft.height()/2, 4);
  _tft.setTextDatum(TL_DATUM);
}
```

- [ ] **Step 3: Make the WiFi dot geometry-aware**

Replace `drawWifiIndicator`:

```cpp
void Renderer::drawWifiIndicator(bool on) {
  _tft.fillCircle(_tft.width() - 10, 8, 4, on ? TFT_GREEN : TFT_BLACK);
}
```

- [ ] **Step 4: Dispatch + implement portrait Settings**

At the start of `Renderer::drawSettings`, before `_tft.fillScreen(...)`:

```cpp
  if (portrait()) { drawSettingsPortrait(brightness, fetchInterval, ledEnabled); return; }
```

Add `drawSettingsPortrait` and `drawIntervalButtonsPortrait` after `drawSettings`:

```cpp
void Renderer::drawSettingsPortrait(uint8_t brightness, unsigned long fetchInterval, bool ledEnabled) {
  _tft.fillScreen(colorScreenBg());

  // Header: reboot icon, LED icon, title (rotate icon added in Task 7)
  drawRebootIcon(false);
  drawLedToggle(ledEnabled);
  _tft.setFreeFont(TITLE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("SETTINGS", 120, 8);
  _tft.setTextFont(0);

  // Brightness card
  drawCard(6, 44, 228, 56);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Brightness", 10, 48, 2);

  _tft.fillRoundRect(10, 62, 44, 32, 5, colorCardBg());
  _tft.drawRoundRect(10, 62, 44, 32, 5, colorCardBorder());
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("-", 32, 78);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.fillRoundRect(186, 62, 44, 32, 5, colorCardBg());
  _tft.drawRoundRect(186, 62, 44, 32, 5, colorCardBorder());
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("+", 208, 78);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.drawRoundRect(60, 62, 120, 32, 5, colorCardBorder());
  int bfill = 118 * brightness / 255;
  _tft.fillRect(61, 63, bfill,               30, colorAccent());
  _tft.fillRect(61 + bfill, 63, 118 - bfill, 30, colorCardBg());

  // Refresh card
  drawCard(6, 110, 228, 58);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Refresh", 10, 114, 2);
  drawIntervalButtonsPortrait(fetchInterval);

  // Nav buttons at bottom
  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(6,   282, 112, 30, "< Grok",   f, b, TFT_WHITE);
    drawButton(122, 282, 112, 30, "Claude >", f, b, TFT_WHITE);
  }
}

void Renderer::drawIntervalButtonsPortrait(unsigned long fetchInterval) {
  const unsigned long intervals[3] = {30000, 60000, 120000};
  const char*         labels[3]    = {"30s", "60s", "120s"};
  const int           btnX[3]      = {8, 84, 160};
  for (int i = 0; i < 3; i++) {
    bool sel = (fetchInterval == intervals[i]);
    uint16_t border = sel ? colorAccent() : colorCardBorder();
    uint16_t fg     = sel ? colorAccent() : TFT_WHITE;
    _tft.fillRoundRect(btnX[i], 130, 72, 32, 5, colorCardBg());
    _tft.drawRoundRect(btnX[i], 130, 72, 32, 5, border);
    _tft.setTextColor(fg);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString(labels[i], btnX[i] + 36, 146, 2);
    _tft.setTextDatum(TL_DATUM);
  }
}
```

- [ ] **Step 5: Portrait branch in `updateBrightnessBar` and `updateIntervalButtons`**

At the top of `Renderer::updateBrightnessBar`:

```cpp
  if (portrait()) {
    int bfill = 118 * brightness / 255;
    _tft.fillRect(61, 63, bfill,               30, colorAccent());
    _tft.fillRect(61 + bfill, 63, 118 - bfill, 30, colorCardBg());
    return;
  }
```

At the top of `Renderer::updateIntervalButtons`:

```cpp
  if (portrait()) { drawIntervalButtonsPortrait(fetchInterval); return; }
```

- [ ] **Step 6: Build**

Run: `pio run`
Expected: compiles with zero errors.

- [ ] **Step 7: On-device check (portrait Settings)**

With a temporary `state.rotation = 0;`, navigate to Settings (or temporarily `state.screen = 2`). Confirm the header icons, "SETTINGS" title, brightness card (− / bar / +), refresh card (three interval buttons), and nav buttons all render un-clipped in portrait. Confirm the WiFi dot (top-right) and any status screen (`showConnecting`) also center correctly in portrait. Remove temporary lines when done.

- [ ] **Step 8: Commit**

```bash
git add src/Renderer.h src/Renderer.cpp
git commit -m "Add portrait Settings layout and make status screens/wifi dot geometry-aware"
```

---

### Task 6: Geometry-aware AnimatedSprite patrol bounds

**Files:**
- Modify: `src/AnimatedSprite.h` (add a width setter / field)
- Modify: `src/AnimatedSprite.cpp` (use the width for patrol range + clear region)
- Modify: `src/Renderer.cpp` (tell the sprite the current header width in `setRotation`)

**Interfaces:**
- Consumes: `_rotation` / geometry (Task 2).
- Produces: `void AnimatedSprite::setHeaderWidth(int w);` — sets the right-hand patrol bound (title x). Default keeps landscape behavior (320-wide header) if never called.

The sprite patrols between a left position and the title. In landscape the title sits at x≈160 (header 320 wide); in portrait at x≈120 (header 240 wide). Read the current patrol/right bound from a field instead of a hardcoded constant.

- [ ] **Step 1: Inspect the current patrol math**

Run: `pio device monitor` is not needed. Read `src/AnimatedSprite.cpp` and locate the hardcoded rightmost/title x used in the `ToTitle`/`AtTitle` phases and any clear-region math.

Run: `grep -n "160\|320\|_x\|title\|width" src/AnimatedSprite.cpp`
Expected: shows the constants used for the patrol's right bound and clear region.

- [ ] **Step 2: Add a width field + setter to `src/AnimatedSprite.h`**

In the `public:` section:

```cpp
  void setHeaderWidth(int w) { _headerWidth = w; }
```

In the `private:` section (with the other members):

```cpp
  int _headerWidth = 320; // landscape header width; set per geometry
```

- [ ] **Step 3: Use `_headerWidth` in `src/AnimatedSprite.cpp`**

Replace the hardcoded right-bound / title-x constant(s) found in Step 1 with an expression based on `_headerWidth` (e.g. the title center is `_headerWidth / 2`, and the right patrol limit should keep the sprite fully left of the title — use the same relationship the landscape code used, but derived from `_headerWidth` instead of the literal `320`/`160`). Keep the clear-region logic clearing only the exact previous footprint (do not widen it).

Example (adapt to the exact variables in this file): if the landscape code patrolled toward an `x` of roughly `title_x - sprite_width`, compute `int titleX = _headerWidth / 2;` and use `titleX` where `160` appeared, and `_headerWidth` where `320` appeared.

- [ ] **Step 4: Set the header width from `Renderer::setRotation`**

At the end of `Renderer::setRotation` (after `applyTouchCalibration`):

```cpp
  _sprite.setHeaderWidth(portrait() ? 240 : 320);
```

- [ ] **Step 5: Build**

Run: `pio run`
Expected: compiles with zero errors.

- [ ] **Step 6: On-device check (sprite in both geometries)**

With a temporary `state.rotation = 0;` on the Claude screen, confirm the mascot patrols within the portrait header (240 wide) without walking under/over the title or off-screen, and without tearing/black flash (per the no-double-buffer quirk). Then set rotation 3 and confirm landscape patrol is unchanged. Remove temporary lines.

- [ ] **Step 7: Commit**

```bash
git add src/AnimatedSprite.h src/AnimatedSprite.cpp src/Renderer.cpp
git commit -m "Make AnimatedSprite patrol bounds geometry-aware"
```

---

### Task 7: Rotate icon, cycle + recalibrate events, touch wiring

**Files:**
- Modify: `src/Types.h` (add `CycleRotation`, `Recalibrate` events)
- Modify: `src/Renderer.h` (declare `drawRotateIcon`, `updateRotateIcon`, `recalibrate`)
- Modify: `src/Renderer.cpp` (implement rotate icon; draw it in both Settings layouts; implement `recalibrate`)
- Modify: `src/TouchRouter.h` (add press-tracking members)
- Modify: `src/TouchRouter.cpp` (rotate-icon short-tap vs long-press; keep existing zones)
- Modify: `src/main.cpp` (handle the two new events)

**Interfaces:**
- Consumes: `Renderer::setRotation`, `portrait()`, `NvsConfig::saveRotation`, `NvsConfig::saveCal`.
- Produces:
  - `Event::CycleRotation`, `Event::Recalibrate`.
  - `void Renderer::drawRotateIcon();` and `void Renderer::recalibrate(uint8_t rotation);`.
  - Rotate icon occupies header zone x∈[82,114], y∈[6,38] (both geometries; header icons share coords).

- [ ] **Step 1: Add events to `src/Types.h`**

In the `Event` enum, add to the last line:

```cpp
  ToggleLed,
  Reboot,
  CycleRotation,
  Recalibrate
```

- [ ] **Step 2: Declare rotate-icon + recalibrate in `src/Renderer.h`**

Private section:

```cpp
  void drawRotateIcon();
```

Public section:

```cpp
  void recalibrate(uint8_t rotation);
```

- [ ] **Step 3: Implement `drawRotateIcon` and `recalibrate` in `src/Renderer.cpp`**

Add after `drawLedToggle`:

```cpp
// Small circular-arrow "rotate" glyph in a header icon button at (82,6,32,32).
void Renderer::drawRotateIcon() {
  const int cx = 98, cy = 22; // origin (82,6) + half of 32x32
  _tft.fillRoundRect(82, 6, 32, 32, 6, colorCardBg());
  _tft.drawRoundRect(82, 6, 32, 32, 6, colorCardBorder());
  uint16_t fg = TFT_WHITE;
  // ~300-degree arc leaves a gap for the arrowhead.
  _tft.drawArc(cx, cy, 9, 7, 30, 330, fg, colorCardBg());
  // Arrowhead near the top-right gap.
  _tft.fillTriangle(cx + 9, cy - 6, cx + 3, cy - 8, cx + 10, cy + 1, fg);
}

// Runs TFT_eSPI's interactive corner-tap calibration for the current rotation
// and persists the result so it overrides the derived default from then on.
void Renderer::recalibrate(uint8_t rotation) {
  uint16_t cal[5];
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("Tap each arrow", _tft.width()/2, _tft.height()/2, 2);
  _tft.setTextDatum(TL_DATUM);
  _tft.calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 15);
  _tft.setTouch(cal);
  NvsConfig::saveCal(rotation, cal);
}
```

- [ ] **Step 4: Draw the rotate icon in both Settings layouts**

In `drawSettings` (landscape), after `drawLedToggle(ledEnabled);`:

```cpp
  drawRotateIcon();
```

In `drawSettingsPortrait`, after `drawLedToggle(ledEnabled);`:

```cpp
  drawRotateIcon();
```

Note: the landscape "SETTINGS" title is at x=86 which overlaps the new icon at x=82–114. In `drawSettings`, change the title draw from `_tft.drawString("SETTINGS", 86, 8);` to `_tft.drawString("SETTINGS", 130, 8);`. (Portrait already uses x=120, clear of the icons.)

- [ ] **Step 5: Add press-tracking to `src/TouchRouter.h`**

Add private members:

```cpp
  unsigned long _rotPressStart = 0;  // when a press in the rotate-icon zone began (0 = none)
  bool          _rotLongFired  = false; // long-press already emitted for this press
```

- [ ] **Step 6: Implement rotate-icon short/long press in `src/TouchRouter.cpp`**

The rotate icon needs continuous sampling (for hold detection), which the 300 ms debounce blocks. Handle it BEFORE the debounce. Replace the body of `TouchRouter::poll` with:

```cpp
Event TouchRouter::poll(int screen) {
  uint16_t x, y;
  bool touched = _tft.getTouch(&x, &y);

  // Rotate-icon press tracking (Settings only). Header icon zone x[82,114] y[6,38].
  if (screen == 2) {
    bool inRotZone = touched && x >= 82 && x <= 114 && y >= 6 && y <= 38;
    if (inRotZone) {
      if (_rotPressStart == 0) { _rotPressStart = millis(); _rotLongFired = false; }
      if (!_rotLongFired && millis() - _rotPressStart >= 800) {
        _rotLongFired = true;
        _lastTouch = millis();
        return Event::Recalibrate;   // fires once while still held
      }
      return Event::None;            // still deciding short vs long
    } else if (_rotPressStart != 0) {
      // Released. Short tap if long-press never fired.
      unsigned long held = millis() - _rotPressStart;
      _rotPressStart = 0;
      bool wasLong = _rotLongFired;
      _rotLongFired = false;
      if (!wasLong && held < 800) { _lastTouch = millis(); return Event::CycleRotation; }
      return Event::None;
    }
  } else {
    _rotPressStart = 0;
    _rotLongFired  = false;
  }

  if (!touched)                      return Event::None;
  if (millis() - _lastTouch <= 300)  return Event::None;
  _lastTouch = millis();

  if (screen != 2) {
    // Claude / Grok: full-screen tap navigates
    int mid = _tft.width() / 2;
    return (x < mid) ? Event::NavBack : Event::NavForward;
  }

  // Settings — landscape vs portrait zones
  if (_tft.width() >= 320) {
    // Landscape 320x240
    if (y >= 6 && y <= 38) {
      if (x >= 6  && x <= 38) return Event::Reboot;
      if (x >= 44 && x <= 76) return Event::ToggleLed;
      return Event::None; // rotate icon handled above
    }
    if (y > 204) return (x < 160) ? Event::NavBack : Event::NavForward;
    if (y >= 62 && y <= 94) {
      if (x <= 60)  return Event::BrightnessDown;
      if (x >= 260) return Event::BrightnessUp;
      return Event::None;
    }
    if (y >= 130 && y <= 162) {
      if (x >= 10  && x < 105) return Event::Interval30s;
      if (x >= 115 && x < 210) return Event::Interval60s;
      if (x >= 220 && x < 315) return Event::Interval120s;
      return Event::None;
    }
    return Event::None;
  } else {
    // Portrait 240x320
    if (y >= 6 && y <= 38) {
      if (x >= 6  && x <= 38) return Event::Reboot;
      if (x >= 44 && x <= 76) return Event::ToggleLed;
      return Event::None; // rotate icon handled above
    }
    if (y >= 282) return (x < 120) ? Event::NavBack : Event::NavForward;
    if (y >= 62 && y <= 94) {
      if (x <= 54)  return Event::BrightnessDown;  // − at (10,62,44,32)
      if (x >= 186) return Event::BrightnessUp;     // + at (186,62,44,32)
      return Event::None;
    }
    if (y >= 130 && y <= 162) {
      if (x >= 8   && x < 80)  return Event::Interval30s;   // (8,84,160) w72
      if (x >= 84  && x < 156) return Event::Interval60s;
      if (x >= 160 && x < 232) return Event::Interval120s;
      return Event::None;
    }
    return Event::None;
  }
}
```

- [ ] **Step 7: Handle the new events in `src/main.cpp`**

Add these cases to the `switch (touch.poll(state.screen))` in `loop()` (before `default:`):

```cpp
    case Event::CycleRotation: {
      static const uint8_t order[4] = {3, 0, 1, 2};
      int idx = 0;
      for (int i = 0; i < 4; i++) if (order[i] == state.rotation) { idx = i; break; }
      state.rotation = order[(idx + 1) % 4];
      renderer.setRotation(state.rotation);
      NvsConfig::saveRotation(state.rotation);
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
    }
    case Event::Recalibrate:
      renderer.recalibrate(state.rotation);
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
```

- [ ] **Step 8: Build**

Run: `pio run`
Expected: compiles with zero errors.

- [ ] **Step 9: On-device check (the full feature)**

Ensure no temporary `state.rotation = ...;` lines remain in `setup()`. Upload. Then:
- On Settings, short-tap the rotate icon repeatedly; confirm it cycles 3→0→1→2→3, each redrawing the current screen upright in the new orientation with no reboot.
- Power-cycle the device; confirm it boots back into the last-selected rotation.
- In each rotation, confirm touch hits the correct controls (nav, brightness ±, intervals, reboot, LED, rotate). If any rotation's touch is mirrored/off, long-press (~800 ms) the rotate icon, tap the four corner arrows, and confirm touch is corrected and survives a power-cycle.

- [ ] **Step 10: Commit**

```bash
git add src/Types.h src/Renderer.h src/Renderer.cpp src/TouchRouter.h src/TouchRouter.cpp src/main.cpp
git commit -m "Add rotate icon, rotation cycling, and long-press touch recalibration"
```

---

### Task 8: Update CLAUDE.md

**Files:**
- Modify: `CLAUDE.md`

**Interfaces:** none (docs only).

- [ ] **Step 1: Update the hardware/quirks docs**

Edit `CLAUDE.md` to reflect the new behavior:
- In the Hardware table, change the Display row from "rotation 3 (landscape)" to note all 4 rotations are supported and the choice is user-selectable in Settings and persisted in NVS.
- Update the "Touch requires real calibration" quirk: the rotation-3 baseline `{433, 3490, 314, 3448, 5}` is now the seed from which all four rotations' calData are derived in code (`Renderer::applyTouchCalibration`), with a per-rotation NVS override (`netcfg` keys `cal0`…`cal3`) written by the long-press recalibrate gesture on the Settings rotate icon.
- Update the "Touch x is not inverted" quirk to note that touch zones are now geometry-aware (landscape vs portrait) in `TouchRouter::poll`, keyed off `_tft.width()`.
- In the Data source / NVS section, note the `netcfg` namespace now also stores `rot` (uint8) and `cal0`…`cal3` (10-byte blobs).
- In Architecture, note `Renderer::setRotation()` applies rotation + calibration + sprite header width, and that each screen has a landscape and a portrait draw/update path selected by `Renderer::portrait()`.

- [ ] **Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "Document 4-rotation orientation support and per-rotation calibration"
```

---

## Self-Review

**Spec coverage:**
- §1 Orientation state & persistence → Task 1 (NVS), Task 2 (state + boot), Task 7 (cycle + save). ✓
- §2 Geometry-aware drawing (inline branches) → Tasks 3, 4, 5 (Claude/Grok/Settings portrait); status screens + wifi dot in Task 5. ✓
- §3 Touch calibration (derive + on-device recal) → Task 2 (derive), Task 1 (NVS blobs), Task 7 (recal gesture). ✓
- §4 Settings UI control (rotate icon, short cycle, long recal) → Task 7. ✓
- §5 AnimatedSprite geometry-aware → Task 6. ✓
- §6 Event handling → Task 7 (main.cpp cases). ✓
- Files-touched list + CLAUDE.md → all tasks + Task 8. ✓

**Placeholder scan:** No TBD/TODO. Task 6 Step 3 is intentionally descriptive because the sprite's exact patrol variables must be read from the file first (Step 1); it gives the concrete substitution rule (`160`→`_headerWidth/2`, `320`→`_headerWidth`) rather than a literal diff. All other code steps have complete code.

**Type consistency:** `init(uint8_t rotation, uint8_t brightness)` (Task 2) matches the call in Task 2 Step 4. `setRotation`, `portrait()`, `applyTouchCalibration`, `recalibrate(uint8_t)`, `drawRotateIcon()`, `NvsConfig::{loadRotation,saveRotation,loadCal,saveCal}`, `setHeaderWidth(int)`, and `Event::{CycleRotation,Recalibrate}` are used consistently across tasks. Rotate-icon zone x[82,114] y[6,38] matches between `drawRotateIcon` (origin 82,6,32,32) and `TouchRouter::poll`.
