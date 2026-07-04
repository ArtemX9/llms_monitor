# Settings Header Icons (Reboot/LED) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the Settings screen's Reboot and LED-toggle controls from full-width body cards into small icon-only buttons in the header, add a confirm-tap safety to Reboot, and resize the nav buttons into the freed vertical space.

**Architecture:** Pure rendering + touch-routing + event-handling change across `Renderer` (drawing), `TouchRouter` (hit-zones), `Types.h` (new transient state field), and `main.cpp` (arm/confirm/disarm timing logic, owned by `loop()` per the existing `AppState` convention).

**Tech Stack:** C++ / Arduino framework, TFT_eSPI (vendored copy in `lib/TFT_eSPI/`), PlatformIO (`esp32dev` env).

## Global Constraints

- No change to Claude or Grok screens, or to `drawButton`'s signature/body (only its call-site arguments in `drawSettings` change).
- No new `Event` enum value — `Event::Reboot` and `Event::ToggleLed` are reused; arm/confirm timing lives in `main.cpp`'s `loop()`, not `TouchRouter`.
- No persistence of reboot-armed state across screen switches or reboots — transient UI state only.
- Reboot icon button: `(6, 6, 32, 32)` rounded rect, radius 6. LED icon button: `(44, 6, 32, 32)`. "SETTINGS" title moves from `x=10` to `x=86` (same y=8). WiFi dot stays at `(310, 8)`.
- LED "on" color: new `colorLedOn()` helper = `color565(34, 197, 94)`. LED "off" color: reuse existing `colorDestructive()` (red).
- Nav buttons (`< Grok` / `Claude >`): width stays `142` (unchanged), height `22 → 29`, y `216 → 209`.
- TouchRouter nav-row threshold: `y > 213` → `y > 204`. Old y180-212 zone (LED/Reboot in the body) is deleted entirely. New header-icon zone: `y >= 6 && y <= 38`, with `x >= 6 && x <= 38` → `Event::Reboot`, `x >= 44 && x <= 76` → `Event::ToggleLed`.
- Reboot confirm-tap: first tap arms (`state.rebootArmedAt = millis()`, icon fills solid `colorDestructive()`); second tap within 2000ms reboots; no second tap within 2000ms auto-reverts the icon to its normal outline state; leaving the Settings screen resets `rebootArmedAt` to 0 silently.
- No automated test harness exists for `Renderer`/`TouchRouter`/timing behavior (hardware-drawing and physical-touch code); verification is `pio run` compiling cleanly, then hardware flash + manual verification — the latter is explicitly deferred to the user, not implementer subagents (they don't have physical device access).

---

## Reference: relevant current file contents (before this plan)

`src/Types.h` `AppState` struct (currently):

```cpp
struct AppState {
  int           screen          = 0;
  uint8_t       brightness      = 200;
  unsigned long fetchInterval   = 60000;
  bool          needsFullRedraw = true;
  bool          ledEnabled      = true;
};
```

`src/TouchRouter.cpp` (full file, currently):

```cpp
#include "TouchRouter.h"

TouchRouter::TouchRouter(TFT_eSPI& tft) : _tft(tft) {}

Event TouchRouter::poll(int screen) {
  uint16_t x, y;
  if (!_tft.getTouch(&x, &y))       return Event::None;
  if (millis() - _lastTouch <= 300)  return Event::None;
  _lastTouch = millis();

  if (screen != 2) {
    // Claude / Grok: full-screen tap navigates
    // x < 160 = visual left (<) = backward; x >= 160 = visual right (>) = forward
    return (x < 160) ? Event::NavBack : Event::NavForward;
  }

  // Settings
  if (y > 213) {
    return (x < 160) ? Event::NavBack : Event::NavForward;
  }
  if (y >= 62 && y <= 94) {
    if (x <= 60)  return Event::BrightnessDown;  // visual left:  − button
    if (x >= 260) return Event::BrightnessUp;    // visual right: + button
    return Event::None;
  }
  if (y >= 130 && y <= 162) {
    if (x >= 10  && x < 105) return Event::Interval30s;
    if (x >= 115 && x < 210) return Event::Interval60s;
    if (x >= 220 && x < 315) return Event::Interval120s;
    return Event::None;
  }
  if (y >= 180 && y <= 212) {
    if (x >= 10  && x < 155) return Event::ToggleLed;
    if (x >= 165 && x < 310) return Event::Reboot;
    return Event::None;
  }
  return Event::None;
}
```

`src/main.cpp`'s `loop()` (currently):

```cpp
void loop() {
  if (state.screen == 0) renderer.tickSprite();

  switch (touch.poll(state.screen)) {
    case Event::NavForward:
      state.screen = (state.screen + 1) % 3;
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
    case Event::NavBack:
      state.screen = (state.screen + 2) % 3;
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
    case Event::BrightnessUp:
      state.brightness = min(255, (int)state.brightness + 20);
      ledcWrite(BL_CHANNEL, state.brightness);
      renderer.updateBrightnessBar(state.brightness);
      break;
    case Event::BrightnessDown:
      if (state.brightness > 20) state.brightness -= 20;
      ledcWrite(BL_CHANNEL, state.brightness);
      renderer.updateBrightnessBar(state.brightness);
      break;
    case Event::Interval30s:
      state.fetchInterval = 30000;
      renderer.updateIntervalButtons(state.fetchInterval);
      break;
    case Event::Interval60s:
      state.fetchInterval = 60000;
      renderer.updateIntervalButtons(state.fetchInterval);
      break;
    case Event::Interval120s:
      state.fetchInterval = 120000;
      renderer.updateIntervalButtons(state.fetchInterval);
      break;
    case Event::ToggleLed:
      state.ledEnabled = !state.ledEnabled;
      fetcher.setLedEnabled(state.ledEnabled);
      renderer.updateLedToggle(state.ledEnabled);
      break;
    case Event::Reboot:
      renderer.showRebooting();
      delay(300);
      ESP.restart();
      break;
    default: break;
  }

  if (millis() - lastFetch > state.fetchInterval) {
    if (fetcher.fetch(data)) {
      renderer.update(state.screen, data, state.needsFullRedraw);
      state.needsFullRedraw = false;
    } else if (fetcher.consecutiveFailures() >= 5) {
      if (!fetcher.recoverProxy()) {
        ESP.restart();
      }
    }
    lastFetch = millis();
  }
}
```

`src/Renderer.h` (currently — see prior plan's final state; unchanged since):

```cpp
#pragma once
#include <TFT_eSPI.h>
#include "Types.h"
#include "AnimatedSprite.h"

class Renderer {
  TFT_eSPI      _tft;
  UsageData     _prev = { -1, -1, -1, -1, -1 };
  AnimatedSprite _sprite;

  uint16_t progressColor(int pct);
  void drawProgressBar(int x, int y, int w, int h, int pct, uint16_t color);
  void drawButton(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t border, uint16_t fg);
  void drawPill(int x, int y, int w, int h, const char* label);

  uint16_t colorScreenBg();
  uint16_t colorCardBg();
  uint16_t colorCardBorder();
  uint16_t colorAccent();
  uint16_t colorLabel();
  uint16_t colorDestructive();
  void drawCard(int x, int y, int w, int h);
  void drawOutlineCard(int x, int y, int w, int h, uint16_t border, uint16_t fg, const char* label);
  void drawIntervalButtons(unsigned long fetchInterval);

  void drawClaude(const UsageData& d);
  void updateClaude(const UsageData& d);
  void drawGrok(const UsageData& d);
  void updateGrok(const UsageData& d);
  void drawSettings(uint8_t brightness, unsigned long fetchInterval, bool ledEnabled);
  void drawLedToggle(bool ledEnabled);

public:
  TFT_eSPI& tft();
  void init(uint8_t brightness);

  void showConnecting();
  void showWifiFailed();
  void showServerError();
  void showRebooting();

  void switchTo(int screen, const UsageData& data,
                uint8_t brightness, unsigned long fetchInterval, bool ledEnabled);
  void update(int screen, const UsageData& data, bool fullRedraw);
  void updateBrightnessBar(uint8_t brightness);
  void updateIntervalButtons(unsigned long fetchInterval);
  void updateLedToggle(bool ledEnabled);
  void drawWifiIndicator(bool on);
  void tickSprite();
};
```

`src/Renderer.cpp`'s current `drawSettings`, `drawLedToggle`, and the palette block (relevant excerpt):

```cpp
uint16_t Renderer::colorScreenBg()    { return _tft.color565(8, 8, 10); }
uint16_t Renderer::colorCardBg()      { return _tft.color565(24, 24, 27); }
uint16_t Renderer::colorCardBorder()  { return _tft.color565(63, 63, 70); }
uint16_t Renderer::colorAccent()      { return _tft.color565(99, 102, 241); }
uint16_t Renderer::colorLabel()       { return _tft.color565(140, 140, 145); }
uint16_t Renderer::colorDestructive() { return _tft.color565(220, 38, 38); }

void Renderer::drawCard(int x, int y, int w, int h) {
  _tft.fillRoundRect(x, y, w, h, 6, colorCardBg());
  _tft.drawRoundRect(x, y, w, h, 6, colorCardBorder());
}

void Renderer::drawOutlineCard(int x, int y, int w, int h,
                                uint16_t border, uint16_t fg, const char* label) {
  _tft.fillRoundRect(x, y, w, h, 6, colorCardBg());
  _tft.drawRoundRect(x, y, w, h, 6, border);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(fg);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(label, x + w / 2, y + h / 2);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);
}

// ... (Claude/Grok functions unchanged, omitted here) ...

void Renderer::drawSettings(uint8_t brightness, unsigned long fetchInterval, bool ledEnabled) {
  _tft.fillScreen(colorScreenBg());
  _tft.setFreeFont(TITLE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("SETTINGS", 10, 8);
  _tft.setTextFont(0);

  // ── Brightness card ────────────────────────────────────────────────────────
  drawCard(6, 44, 308, 56);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Brightness", 10, 48, 2);

  _tft.fillRoundRect(10, 62, 50, 32, 5, colorCardBg());
  _tft.drawRoundRect(10, 62, 50, 32, 5, colorCardBorder());
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("-", 35, 78);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.fillRoundRect(260, 62, 50, 32, 5, colorCardBg());
  _tft.drawRoundRect(260, 62, 50, 32, 5, colorCardBorder());
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("+", 285, 78);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.drawRoundRect(68, 62, 182, 32, 5, colorCardBorder());
  int bfill = 180 * brightness / 255;
  _tft.fillRect(69, 63, bfill,               30, colorAccent());
  _tft.fillRect(69 + bfill, 63, 180 - bfill, 30, colorCardBg());

  // ── Refresh card ───────────────────────────────────────────────────────────
  drawCard(6, 110, 308, 58);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Refresh", 10, 114, 2);
  drawIntervalButtons(fetchInterval);

  // ── LED toggle + Reboot cards ──────────────────────────────────────────────
  drawLedToggle(ledEnabled);
  drawOutlineCard(165, 180, 145, 32, colorDestructive(), colorDestructive(), "REBOOT");

  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(8,   216, 142, 22, "< Grok",   f, b, TFT_WHITE);
    drawButton(170, 216, 142, 22, "Claude >", f, b, TFT_WHITE);
  }
}

void Renderer::drawIntervalButtons(unsigned long fetchInterval) {
  const unsigned long intervals[3] = {30000, 60000, 120000};
  const char*         labels[3]    = {"30s", "60s", "120s"};
  const int           btnX[3]      = {10, 115, 220};
  for (int i = 0; i < 3; i++) {
    bool sel = (fetchInterval == intervals[i]);
    uint16_t border = sel ? colorAccent() : colorCardBorder();
    uint16_t fg     = sel ? colorAccent() : TFT_WHITE;
    _tft.fillRoundRect(btnX[i], 130, 95, 32, 5, colorCardBg());
    _tft.drawRoundRect(btnX[i], 130, 95, 32, 5, border);
    _tft.setTextColor(fg);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString(labels[i], btnX[i] + 47, 146, 2);
    _tft.setTextDatum(TL_DATUM);
  }
}

void Renderer::drawLedToggle(bool ledEnabled) {
  uint16_t c = ledEnabled ? colorAccent() : colorCardBorder();
  drawOutlineCard(10, 180, 145, 32, c, c, ledEnabled ? "LED: ON" : "LED: OFF");
}
```

---

### Task 1: Color/icon-drawing foundation in Renderer

**Files:**
- Modify: `src/Renderer.h`
- Modify: `src/Renderer.cpp`

**Interfaces:**
- Produces (used by Task 2):
  - `uint16_t Renderer::colorLedOn()` — private, returns `color565(34, 197, 94)`
  - `void Renderer::drawRebootIcon(bool armed)` — private, draws the 32×32 reboot button at `(6,6)` in its normal (outline) or armed (solid fill) state
  - `void Renderer::updateRebootIcon(bool armed)` — public, thin wrapper calling `drawRebootIcon(armed)` (mirrors the existing `updateLedToggle` → `drawLedToggle` pattern)

Nothing calls `drawRebootIcon`/`updateRebootIcon` yet in this task — that's expected; Task 2 wires `drawRebootIcon` into `drawSettings`, and Task 4 wires `updateRebootIcon` into `main.cpp`.

- [ ] **Step 1: Add `colorLedOn()` to the palette block in `src/Renderer.cpp`**

Change:

```cpp
uint16_t Renderer::colorDestructive() { return _tft.color565(220, 38, 38); }
```

to:

```cpp
uint16_t Renderer::colorDestructive() { return _tft.color565(220, 38, 38); }
uint16_t Renderer::colorLedOn()       { return _tft.color565(34, 197, 94); }
```

- [ ] **Step 2: Add `drawRebootIcon` right after `drawOutlineCard` in `src/Renderer.cpp`**

Insert this new function immediately after `drawOutlineCard`'s closing brace (before the `// ── Status screens ──` comment):

```cpp
void Renderer::drawRebootIcon(bool armed) {
  const int cx = 22, cy = 22; // button center: origin (6,6) + half of 32x32
  if (armed) {
    _tft.fillRoundRect(6, 6, 32, 32, 6, colorDestructive());
    _tft.drawArc(cx, cy, 9, 6, 45, 315, TFT_WHITE, colorDestructive());
    _tft.fillTriangle(30, 24, 30, 31, 24, 28, TFT_WHITE);
  } else {
    _tft.fillRoundRect(6, 6, 32, 32, 6, colorCardBg());
    _tft.drawRoundRect(6, 6, 32, 32, 6, colorDestructive());
    _tft.drawArc(cx, cy, 9, 6, 45, 315, colorDestructive(), colorCardBg());
    _tft.fillTriangle(30, 24, 30, 31, 24, 28, colorDestructive());
  }
}
```

This draws a broken-ring "refresh" glyph (270° arc, 90° gap at the bottom) plus a small triangular accent near the arc's end, in the reboot button's outline color (normal) or in white against a solid red fill (armed). The exact glyph look is a first pass — hardware QA (Task 5 of this plan) may call for small tweaks to the triangle's 3 points, which is a one-line change.

- [ ] **Step 3: Add `updateRebootIcon` and the `drawRebootIcon`/`colorLedOn` declarations to `src/Renderer.h`**

Change:

```cpp
  void drawCard(int x, int y, int w, int h);
  void drawOutlineCard(int x, int y, int w, int h, uint16_t border, uint16_t fg, const char* label);
  void drawIntervalButtons(unsigned long fetchInterval);
```

to:

```cpp
  void drawCard(int x, int y, int w, int h);
  void drawOutlineCard(int x, int y, int w, int h, uint16_t border, uint16_t fg, const char* label);
  void drawIntervalButtons(unsigned long fetchInterval);
  void drawRebootIcon(bool armed);
  uint16_t colorLedOn();
```

Change:

```cpp
  void updateLedToggle(bool ledEnabled);
  void drawWifiIndicator(bool on);
```

to:

```cpp
  void updateLedToggle(bool ledEnabled);
  void updateRebootIcon(bool armed);
  void drawWifiIndicator(bool on);
```

- [ ] **Step 4: Add the `updateRebootIcon` implementation to `src/Renderer.cpp`**

Add this right after `updateLedToggle`'s implementation:

```cpp
void Renderer::updateLedToggle(bool ledEnabled) {
  drawLedToggle(ledEnabled);
}

void Renderer::updateRebootIcon(bool armed) {
  drawRebootIcon(armed);
}
```

(The first two lines already exist — this step adds the new `updateRebootIcon` function directly below the existing `updateLedToggle`.)

- [ ] **Step 5: Compile check**

Run: `pio run`
Expected: `SUCCESS` — the new methods are unused so far (no call sites yet), so this only verifies the new code is syntactically valid and links.

- [ ] **Step 6: Commit**

```bash
git add src/Renderer.h src/Renderer.cpp
git commit -m "$(cat <<'EOF'
Add reboot-icon drawing and LED-on color helper to Renderer

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Rewrite Settings screen layout — header icons, bulb LED, resized nav buttons

**Files:**
- Modify: `src/Renderer.cpp` — `drawLedToggle`, `drawSettings`

**Interfaces:**
- Consumes: `colorLedOn()`, `drawRebootIcon(bool)` (Task 1)
- No new interfaces produced — `drawLedToggle`'s existing signature is unchanged, only its body and the body of `drawSettings` change.

- [ ] **Step 1: Rewrite `drawLedToggle` to draw a filled bulb icon instead of a text card**

Change:

```cpp
void Renderer::drawLedToggle(bool ledEnabled) {
  uint16_t c = ledEnabled ? colorAccent() : colorCardBorder();
  drawOutlineCard(10, 180, 145, 32, c, c, ledEnabled ? "LED: ON" : "LED: OFF");
}
```

to:

```cpp
void Renderer::drawLedToggle(bool ledEnabled) {
  uint16_t c = ledEnabled ? colorLedOn() : colorDestructive();
  _tft.fillCircle(60, 22, 8, c);   // bulb glass — button origin (44,6) + half of 32x32
  _tft.fillRect(58, 30, 4, 3, c);  // bulb base
}
```

- [ ] **Step 2: Rewrite `drawSettings`**

Change the whole function:

```cpp
void Renderer::drawSettings(uint8_t brightness, unsigned long fetchInterval, bool ledEnabled) {
  _tft.fillScreen(colorScreenBg());
  _tft.setFreeFont(TITLE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("SETTINGS", 10, 8);
  _tft.setTextFont(0);

  // ── Brightness card ────────────────────────────────────────────────────────
  drawCard(6, 44, 308, 56);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Brightness", 10, 48, 2);

  _tft.fillRoundRect(10, 62, 50, 32, 5, colorCardBg());
  _tft.drawRoundRect(10, 62, 50, 32, 5, colorCardBorder());
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("-", 35, 78);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.fillRoundRect(260, 62, 50, 32, 5, colorCardBg());
  _tft.drawRoundRect(260, 62, 50, 32, 5, colorCardBorder());
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("+", 285, 78);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.drawRoundRect(68, 62, 182, 32, 5, colorCardBorder());
  int bfill = 180 * brightness / 255;
  _tft.fillRect(69, 63, bfill,               30, colorAccent());
  _tft.fillRect(69 + bfill, 63, 180 - bfill, 30, colorCardBg());

  // ── Refresh card ───────────────────────────────────────────────────────────
  drawCard(6, 110, 308, 58);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Refresh", 10, 114, 2);
  drawIntervalButtons(fetchInterval);

  // ── LED toggle + Reboot cards ──────────────────────────────────────────────
  drawLedToggle(ledEnabled);
  drawOutlineCard(165, 180, 145, 32, colorDestructive(), colorDestructive(), "REBOOT");

  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(8,   216, 142, 22, "< Grok",   f, b, TFT_WHITE);
    drawButton(170, 216, 142, 22, "Claude >", f, b, TFT_WHITE);
  }
}
```

to:

```cpp
void Renderer::drawSettings(uint8_t brightness, unsigned long fetchInterval, bool ledEnabled) {
  _tft.fillScreen(colorScreenBg());

  // ── Header: reboot icon, LED icon, title ──────────────────────────────────
  drawRebootIcon(false);
  drawLedToggle(ledEnabled);
  _tft.setFreeFont(TITLE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("SETTINGS", 86, 8);
  _tft.setTextFont(0);

  // ── Brightness card ────────────────────────────────────────────────────────
  drawCard(6, 44, 308, 56);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Brightness", 10, 48, 2);

  _tft.fillRoundRect(10, 62, 50, 32, 5, colorCardBg());
  _tft.drawRoundRect(10, 62, 50, 32, 5, colorCardBorder());
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("-", 35, 78);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.fillRoundRect(260, 62, 50, 32, 5, colorCardBg());
  _tft.drawRoundRect(260, 62, 50, 32, 5, colorCardBorder());
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("+", 285, 78);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.drawRoundRect(68, 62, 182, 32, 5, colorCardBorder());
  int bfill = 180 * brightness / 255;
  _tft.fillRect(69, 63, bfill,               30, colorAccent());
  _tft.fillRect(69 + bfill, 63, 180 - bfill, 30, colorCardBg());

  // ── Refresh card ───────────────────────────────────────────────────────────
  drawCard(6, 110, 308, 58);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Refresh", 10, 114, 2);
  drawIntervalButtons(fetchInterval);

  // ── Nav buttons (resized: height x1.3, moved up into freed space) ─────────
  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(8,   209, 142, 29, "< Grok",   f, b, TFT_WHITE);
    drawButton(170, 209, 142, 29, "Claude >", f, b, TFT_WHITE);
  }
}
```

Note the old body-row `drawOutlineCard(165, 180, 145, 32, ...)` REBOOT call is deleted entirely — reboot now only appears as the header icon drawn via `drawRebootIcon(false)` above.

- [ ] **Step 3: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 4: Flash and visually verify (layout only — touch zones aren't updated until Task 3)**

Run: `pio run -t upload` then `pio device monitor`.

On the physical device, navigate to Settings and confirm:
- Two small icon buttons appear top-left: a red-outlined broken-ring icon, then a bulb icon filled green (LED defaults to on) or red.
- "SETTINGS" title sits to the right of both icons with no overlap.
- The old body row where LED/Reboot used to be (around y180-212) is now empty — Brightness and Refresh cards look unchanged.
- Nav buttons (`< Grok` / `Claude >`) are visibly taller and sit higher than before, with a gap between them and the Refresh card above.
- **Expected/OK for this step:** tapping the icons won't do anything correct yet (old touch zones still point at the old y180-212 coordinates) — that's fixed in Task 3, not a regression to chase here.

- [ ] **Step 5: Commit**

```bash
git add src/Renderer.cpp
git commit -m "$(cat <<'EOF'
Move Reboot/LED to header icon buttons, resize nav buttons

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Update TouchRouter hit-zones to match the new layout

**Files:**
- Modify: `src/TouchRouter.cpp`

**Interfaces:**
- Consumes: nothing new — still returns the existing `Event` enum values.
- No new interfaces produced.

- [ ] **Step 1: Replace the Settings-screen zone checks in `poll()`**

Change:

```cpp
  // Settings
  if (y > 213) {
    return (x < 160) ? Event::NavBack : Event::NavForward;
  }
  if (y >= 62 && y <= 94) {
    if (x <= 60)  return Event::BrightnessDown;  // visual left:  − button
    if (x >= 260) return Event::BrightnessUp;    // visual right: + button
    return Event::None;
  }
  if (y >= 130 && y <= 162) {
    if (x >= 10  && x < 105) return Event::Interval30s;
    if (x >= 115 && x < 210) return Event::Interval60s;
    if (x >= 220 && x < 315) return Event::Interval120s;
    return Event::None;
  }
  if (y >= 180 && y <= 212) {
    if (x >= 10  && x < 155) return Event::ToggleLed;
    if (x >= 165 && x < 310) return Event::Reboot;
    return Event::None;
  }
  return Event::None;
}
```

to:

```cpp
  // Settings
  if (y >= 6 && y <= 38) {
    if (x >= 6  && x <= 38) return Event::Reboot;
    if (x >= 44 && x <= 76) return Event::ToggleLed;
    return Event::None;
  }
  if (y > 204) {
    return (x < 160) ? Event::NavBack : Event::NavForward;
  }
  if (y >= 62 && y <= 94) {
    if (x <= 60)  return Event::BrightnessDown;  // visual left:  − button
    if (x >= 260) return Event::BrightnessUp;    // visual right: + button
    return Event::None;
  }
  if (y >= 130 && y <= 162) {
    if (x >= 10  && x < 105) return Event::Interval30s;
    if (x >= 115 && x < 210) return Event::Interval60s;
    if (x >= 220 && x < 315) return Event::Interval120s;
    return Event::None;
  }
  return Event::None;
}
```

- [ ] **Step 2: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 3: Flash and verify touch zones**

Run: `pio run -t upload` then `pio device monitor`.

On the physical device, on the Settings screen:
- Tapping the reboot icon (top-left) toggles `updateRebootIcon` — **note:** at this point in the plan, `main.cpp` still has the *old* `Event::Reboot` handling (immediate reboot on any tap, no arm/confirm state yet — that's Task 4). So a single tap on the reboot icon **will still reboot the device immediately** right now. This is expected and temporary; don't be alarmed, just confirm the tap is correctly *routed* (device reboots when you tap the icon, proving the touch zone is right) — the confirm-tap safety lands in Task 4.
- Tapping the LED icon (next to reboot) still toggles the LED bulb color exactly as before.
- Tapping in the old y180-212 body area (now empty) does nothing.
- Brightness ±, interval buttons, and nav buttons (now at their new y=209 position) all still work.

- [ ] **Step 4: Commit**

```bash
git add src/TouchRouter.cpp
git commit -m "$(cat <<'EOF'
Move Settings touch zones to match header icon buttons

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Reboot confirm-tap arm/disarm logic

**Files:**
- Modify: `src/Types.h` — add `rebootArmedAt` to `AppState`
- Modify: `src/main.cpp` — `loop()`

**Interfaces:**
- Consumes: `Renderer::updateRebootIcon(bool armed)` (Task 1), `Event::Reboot`/`Event::NavForward`/`Event::NavBack` (existing, routed by Task 3's updated `TouchRouter`)
- Produces: `AppState::rebootArmedAt` (`unsigned long`, default `0`) — no other task consumes this field; it's `main.cpp`-internal.

- [ ] **Step 1: Add `rebootArmedAt` to `AppState` in `src/Types.h`**

Change:

```cpp
struct AppState {
  int           screen          = 0;
  uint8_t       brightness      = 200;
  unsigned long fetchInterval   = 60000;
  bool          needsFullRedraw = true;
  bool          ledEnabled      = true;
};
```

to:

```cpp
struct AppState {
  int           screen          = 0;
  uint8_t       brightness      = 200;
  unsigned long fetchInterval   = 60000;
  bool          needsFullRedraw = true;
  bool          ledEnabled      = true;
  unsigned long rebootArmedAt   = 0; // 0 = disarmed; set to millis() on first reboot-icon tap
};
```

- [ ] **Step 2: Reset `rebootArmedAt` when leaving the Settings screen**

Change:

```cpp
    case Event::NavForward:
      state.screen = (state.screen + 1) % 3;
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
    case Event::NavBack:
      state.screen = (state.screen + 2) % 3;
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
```

to:

```cpp
    case Event::NavForward:
      if (state.screen == 2) state.rebootArmedAt = 0;
      state.screen = (state.screen + 1) % 3;
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
    case Event::NavBack:
      if (state.screen == 2) state.rebootArmedAt = 0;
      state.screen = (state.screen + 2) % 3;
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
```

- [ ] **Step 3: Replace the `Event::Reboot` case with arm/confirm logic**

Change:

```cpp
    case Event::Reboot:
      renderer.showRebooting();
      delay(300);
      ESP.restart();
      break;
```

to:

```cpp
    case Event::Reboot:
      if (state.rebootArmedAt != 0 && millis() - state.rebootArmedAt < 2000) {
        renderer.showRebooting();
        delay(300);
        ESP.restart();
      } else {
        state.rebootArmedAt = millis();
        renderer.updateRebootIcon(true);
      }
      break;
```

- [ ] **Step 4: Add the auto-disarm check to `loop()`**

Change:

```cpp
  if (millis() - lastFetch > state.fetchInterval) {
```

to:

```cpp
  if (state.screen == 2 && state.rebootArmedAt != 0 && millis() - state.rebootArmedAt > 2000) {
    state.rebootArmedAt = 0;
    renderer.updateRebootIcon(false);
  }

  if (millis() - lastFetch > state.fetchInterval) {
```

- [ ] **Step 5: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 6: Flash and verify the full confirm-tap flow**

Run: `pio run -t upload` then `pio device monitor`.

On the physical device, on the Settings screen:
- Tap the reboot icon once: it should fill solid red (armed) and the device should **not** reboot yet.
- Wait 2+ seconds without tapping again: the icon should revert to its normal red-outline state, and the device is still running.
- Tap the reboot icon once (arms it), then tap it again within 2 seconds: the device should show "Rebooting..." and actually restart.
- Tap the reboot icon once (arms it), navigate to Grok or Claude, then back to Settings: the icon should show its normal disarmed state, not stuck armed.

- [ ] **Step 7: Commit**

```bash
git add src/Types.h src/main.cpp
git commit -m "$(cat <<'EOF'
Add confirm-tap safety to the header Reboot icon

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Full hardware QA pass

**Files:** none (verification only)

- [ ] **Step 1: Full walkthrough on hardware**

With the fully-updated firmware flashed, on the physical ESP32-32E CYD board, re-verify end-to-end (some of this repeats earlier per-task checks, but confirms nothing regressed once all 4 tasks are combined):

- Settings header: reboot icon, LED bulb icon, and "SETTINGS" title all render without overlapping or clipping; WiFi dot still visible top-right.
- LED icon toggles green/red correctly and persists visually across a full screen redraw (navigate away and back).
- Reboot confirm-tap flow works exactly as in Task 4's Step 6.
- Nav buttons at their new size/position work from Settings in both directions; Claude/Grok screens (which don't use the resized buttons) are unaffected.
- The empty gap where the old LED+Reboot row was doesn't respond to taps.
- Brightness ± and all 3 interval buttons still work correctly (unaffected by this plan, but worth a sanity check since `drawSettings` was rewritten).

- [ ] **Step 2: Report results**

If everything above passes, this plan is complete — no further commits needed (Task 5 is verification-only). If any issue is found, note the exact screen/control/symptom so a follow-up fix task can be scoped.
