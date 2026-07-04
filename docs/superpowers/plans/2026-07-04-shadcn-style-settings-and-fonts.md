# Shadcn-style Settings restyle + consistent fonts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restyle the Settings screen to look like a Shadcn UI component set (neutral dark cards, one muted accent color, rounded corners) and make font usage consistent (title/value/label roles) across the Claude, Grok, and Settings screens.

**Architecture:** Pure rendering change confined to `src/Renderer.h` and `src/Renderer.cpp` — no touch-zone, data, or state-machine changes. New private color/font helper methods centralize the palette and font choices; existing draw/update functions are edited in place to call them instead of the old GLCD font numbers and `TFT_*` color constants.

**Tech Stack:** C++ / Arduino framework, TFT_eSPI (vendored copy in `lib/TFT_eSPI/`), PlatformIO (`esp32dev` env).

## Global Constraints

- No changes to touch hit-box coordinates in `src/TouchRouter.cpp` — every restyled control keeps the exact outer pixel footprint it has today.
- No changes to the bottom nav buttons' existing neutral-grey style logic beyond the font swap in Task 3 — their fill/border colors (`color565(32,32,32)` / `color565(90,90,90)`) are unchanged.
- No changes to `AnimatedSprite`/mascot rendering, data/fetch logic, WiFi, or any file other than `src/Renderer.h` / `src/Renderer.cpp`.
- No changes to `showConnecting()`, `showWifiFailed()`, `showServerError()`, `showRebooting()` — out of scope per the approved spec (transient boot/error screens, not one of the three main screens).
- `progressColor()` (green/orange/red status thresholds on Claude/Grok progress bars) is unchanged — out of scope.
- Card/background palette changes apply to the **Settings screen only**. Claude and Grok keep `TFT_BLACK` as their screen background — only their fonts change.
- Font-reset convention: after any `setFreeFont(...)` + `drawString(...)`, call `_tft.setTextFont(0)` before the next draw call, matching the existing pattern already used for `TITLE_FONT` at `Renderer.cpp:107` (do not use `setTextFont(1)` — `0` is this codebase's established reset value, proven on hardware).
- There is no automated test harness for `Renderer` (it's hardware-drawing code with no unit tests in this repo). Verification for every task is: (1) `pio run` compiles cleanly, (2) `pio run -t upload` + visual/touch inspection on the actual ESP32-32E CYD hardware.

---

## Reference: full current file contents

`src/Renderer.h` (current, before this plan):

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

(`src/Renderer.cpp`'s current contents are shown inline in each task's "before" context below — see the diffs.)

---

### Task 1: Palette + font constants and shared drawing helpers

**Files:**
- Modify: `src/Renderer.h`
- Modify: `src/Renderer.cpp:1-3` (top of file, before `TFT_eSPI& Renderer::tft()`)

**Interfaces:**
- Produces (used by Tasks 2-4):
  - `#define VALUE_FONT &FreeSansBold18pt7b` and `#define LABEL_FONT &FreeSans9pt7b` (alongside the existing `TITLE_FONT`)
  - `uint16_t Renderer::colorScreenBg()` — Settings screen background
  - `uint16_t Renderer::colorCardBg()` — card fill
  - `uint16_t Renderer::colorCardBorder()` — default card/control border
  - `uint16_t Renderer::colorAccent()` — selected/active/focus color
  - `uint16_t Renderer::colorLabel()` — muted caption text color
  - `uint16_t Renderer::colorDestructive()` — Reboot outline/text color
  - `void Renderer::drawCard(int x, int y, int w, int h)` — draws a rounded-rect card background + border at the given footprint, no text
  - `void Renderer::drawOutlineCard(int x, int y, int w, int h, uint16_t border, uint16_t fg, const char* label)` — rounded-rect card with centered `VALUE_FONT` text, border and text using the given colors

- [ ] **Step 1: Add font constants to `src/Renderer.cpp`**

Change the top of the file from:

```cpp
#include "Renderer.h"
#include "Config.h"
#define TITLE_FONT &FreeSansBold12pt7b
```

to:

```cpp
#include "Renderer.h"
#include "Config.h"
#define TITLE_FONT &FreeSansBold12pt7b
#define VALUE_FONT &FreeSansBold18pt7b
#define LABEL_FONT &FreeSans9pt7b
```

- [ ] **Step 2: Declare the new private helper methods in `src/Renderer.h`**

Change:

```cpp
  uint16_t progressColor(int pct);
  void drawProgressBar(int x, int y, int w, int h, int pct, uint16_t color);
  void drawButton(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t border, uint16_t fg);
  void drawPill(int x, int y, int w, int h, const char* label);
```

to:

```cpp
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
```

(`drawIntervalButtons` is declared here because Task 2 needs it, but it's easiest to add the declaration now alongside its sibling helpers.)

- [ ] **Step 3: Implement the color/card helpers in `src/Renderer.cpp`**

Add this block immediately after the `drawPill` function (after the line `}` that closes it, i.e. right before the `// ── Status screens ─────` comment):

```cpp
// ── Shadcn-style palette (Settings screen) ────────────────────────────────────

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
```

`drawIntervalButtons` is implemented in Task 2 (it needs `colorAccent`/`colorCardBg`/`colorCardBorder` from this task and is used by both `drawSettings` and `updateIntervalButtons`, which are also edited in Task 2).

- [ ] **Step 4: Compile check**

Run: `pio run`
Expected: `SUCCESS` — the new methods are unused so far (no call sites yet), so this only verifies the new code is syntactically valid C++ and links.

- [ ] **Step 5: Commit**

```bash
git add src/Renderer.h src/Renderer.cpp
git commit -m "$(cat <<'EOF'
Add Shadcn-style color palette and card-drawing helpers to Renderer

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Restyle the Settings screen with cards, accent color, and fonts

**Files:**
- Modify: `src/Renderer.cpp` — `drawSettings`, `drawLedToggle`, `updateBrightnessBar`, `updateIntervalButtons`, plus the new `drawIntervalButtons` helper (declared in Task 1)

**Interfaces:**
- Consumes: `colorScreenBg()`, `colorCardBg()`, `colorCardBorder()`, `colorAccent()`, `colorLabel()`, `colorDestructive()`, `drawCard()`, `drawOutlineCard()` (Task 1); `TITLE_FONT`, `VALUE_FONT`, `LABEL_FONT` (Task 1)
- Produces: `void Renderer::drawIntervalButtons(unsigned long fetchInterval)` — draws the 3 interval buttons at their fixed footprint, used by both `drawSettings` and `updateIntervalButtons`

- [ ] **Step 1: Replace `drawSettings`**

Change the whole function (currently `src/Renderer.cpp:240-286`):

```cpp
void Renderer::drawSettings(uint8_t brightness, unsigned long fetchInterval, bool ledEnabled) {
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextSize(1);
  _tft.drawString("SETTINGS", 10, 8, 4);
  _tft.drawFastHLine(0, 40, 320, TFT_DARKGREY);

  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Brightness", 10, 48, 2);
  _tft.drawRect(10, 62, 50, 32, TFT_DARKGREY);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("-", 22, 67, 4);
  _tft.drawRect(260, 62, 50, 32, TFT_DARKGREY);
  _tft.drawString("+", 272, 67, 4);
  _tft.drawRect(68, 62, 182, 32, TFT_DARKGREY);
  int bfill = 180 * brightness / 255;
  _tft.fillRect(69, 63, bfill,               30, TFT_YELLOW);
  _tft.fillRect(69 + bfill, 63, 180 - bfill, 30, TFT_BLACK);

  _tft.drawFastHLine(0, 106, 320, TFT_DARKGREY);

  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Refresh", 10, 114, 2);
  const unsigned long intervals[3] = {30000, 60000, 120000};
  const char*         labels[3]    = {"30s", "60s", "120s"};
  const int           btnX[3]      = {10, 115, 220};
  for (int i = 0; i < 3; i++) {
    bool sel = (fetchInterval == intervals[i]);
    _tft.drawRect(btnX[i], 130, 95, 32, sel ? TFT_GREEN : TFT_DARKGREY);
    _tft.setTextColor(sel ? TFT_GREEN : TFT_WHITE);
    _tft.drawString(labels[i], btnX[i] + 22, 138, 2);
  }

  _tft.drawFastHLine(0, 172, 320, TFT_DARKGREY);
  drawLedToggle(ledEnabled);
  _tft.drawRect(165, 180, 145, 32, TFT_RED);
  _tft.setTextColor(TFT_RED);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("REBOOT", 165 + 72, 180 + 16, 4);
  _tft.setTextDatum(TL_DATUM);

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
  _tft.setFreeFont(TITLE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("SETTINGS", 10, 8);
  _tft.setTextFont(0);

  // ── Brightness card ────────────────────────────────────────────────────────
  drawCard(6, 44, 308, 56);
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Brightness", 10, 48);
  _tft.setTextFont(0);

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
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Refresh", 10, 114);
  _tft.setTextFont(0);
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

- [ ] **Step 2: Add the `drawIntervalButtons` helper and replace `drawLedToggle`**

Change (currently `src/Renderer.cpp:288-296`):

```cpp
void Renderer::drawLedToggle(bool ledEnabled) {
  uint16_t c = ledEnabled ? TFT_GREEN : TFT_DARKGREY;
  _tft.fillRect(11, 181, 143, 30, TFT_BLACK);
  _tft.drawRect(10, 180, 145, 32, c);
  _tft.setTextColor(c);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(ledEnabled ? "LED: ON" : "LED: OFF", 10 + 72, 180 + 16, 4);
  _tft.setTextDatum(TL_DATUM);
}
```

to:

```cpp
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
    _tft.setFreeFont(LABEL_FONT);
    _tft.setTextColor(fg);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString(labels[i], btnX[i] + 47, 146);
    _tft.setTextFont(0);
    _tft.setTextDatum(TL_DATUM);
  }
}

void Renderer::drawLedToggle(bool ledEnabled) {
  uint16_t c = ledEnabled ? colorAccent() : colorCardBorder();
  drawOutlineCard(10, 180, 145, 32, c, c, ledEnabled ? "LED: ON" : "LED: OFF");
}
```

- [ ] **Step 3: Replace `updateBrightnessBar`**

Change (currently `src/Renderer.cpp:316-320`):

```cpp
void Renderer::updateBrightnessBar(uint8_t brightness) {
  int bfill = 180 * brightness / 255;
  _tft.fillRect(69, 63, bfill,               30, TFT_YELLOW);
  _tft.fillRect(69 + bfill, 63, 180 - bfill, 30, TFT_BLACK);
}
```

to:

```cpp
void Renderer::updateBrightnessBar(uint8_t brightness) {
  int bfill = 180 * brightness / 255;
  _tft.fillRect(69, 63, bfill,               30, colorAccent());
  _tft.fillRect(69 + bfill, 63, 180 - bfill, 30, colorCardBg());
}
```

- [ ] **Step 4: Replace `updateIntervalButtons` to call the new shared helper**

Change (currently `src/Renderer.cpp:326-337`):

```cpp
void Renderer::updateIntervalButtons(unsigned long fetchInterval) {
  const unsigned long intervals[3] = {30000, 60000, 120000};
  const char*         labels[3]    = {"30s", "60s", "120s"};
  const int           btnX[3]      = {10, 115, 220};
  for (int i = 0; i < 3; i++) {
    bool sel = (fetchInterval == intervals[i]);
    _tft.fillRect(btnX[i] + 1, 131, 93, 30, TFT_BLACK);
    _tft.drawRect(btnX[i], 130, 95, 32, sel ? TFT_GREEN : TFT_DARKGREY);
    _tft.setTextColor(sel ? TFT_GREEN : TFT_WHITE);
    _tft.drawString(labels[i], btnX[i] + 22, 138, 2);
  }
}
```

to:

```cpp
void Renderer::updateIntervalButtons(unsigned long fetchInterval) {
  drawIntervalButtons(fetchInterval);
}
```

- [ ] **Step 5: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 6: Flash and visually verify the Settings screen**

Run: `pio run -t upload` then `pio device monitor` (Ctrl+C to exit the monitor when done).

On the physical device, navigate to the Settings screen and confirm:
- Screen background is a very dark grey (not pure black), with 3-4 visibly separate rounded cards instead of divider lines.
- Brightness bar fill is muted indigo (not yellow); `−`/`+` buttons are rounded with grey borders.
- Tapping each of the 3 refresh-interval buttons (30s/60s/120s) selects it with an indigo border/text; the other two stay grey/white.
- Tapping the LED toggle switches between indigo border/text (ON) and grey border/text (OFF).
- REBOOT button shows a muted red outline/text (not the old bright `TFT_RED`).
- All touch zones still work exactly as before (brightness ±, 3 interval buttons, LED toggle, Reboot, `< Grok` / `Claude >` nav) — since no coordinates changed, this should require no code changes, only confirmation.

- [ ] **Step 7: Commit**

```bash
git add src/Renderer.cpp
git commit -m "$(cat <<'EOF'
Restyle Settings screen with rounded cards and accent color

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Consistent fonts on shared helpers and the Claude screen

**Files:**
- Modify: `src/Renderer.cpp` — `drawButton`, `drawPill`, `drawClaude`, `updateClaude`

**Interfaces:**
- Consumes: `TITLE_FONT`, `VALUE_FONT`, `LABEL_FONT` (Task 1)
- No new interfaces produced — `drawButton`/`drawPill` keep their existing signatures; only their internal font calls change. Since every screen's nav buttons route through `drawButton` and Claude's stat pills route through `drawPill`, this task's effects are visible on all three screens even though only `drawClaude`/`updateClaude` are edited beyond the shared helpers.

- [ ] **Step 1: Update `drawButton`'s font**

Change (currently `src/Renderer.cpp:43-51`):

```cpp
void Renderer::drawButton(int x, int y, int w, int h,
                           const char* label, uint16_t fill, uint16_t border, uint16_t fg) {
  _tft.fillRoundRect(x, y, w, h, 5, fill);
  _tft.drawRoundRect(x, y, w, h, 5, border);
  _tft.setTextColor(fg);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(label, x + w / 2, y + h / 2, 2);
  _tft.setTextDatum(TL_DATUM);
}
```

to:

```cpp
void Renderer::drawButton(int x, int y, int w, int h,
                           const char* label, uint16_t fill, uint16_t border, uint16_t fg) {
  _tft.fillRoundRect(x, y, w, h, 5, fill);
  _tft.drawRoundRect(x, y, w, h, 5, border);
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(fg);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(label, x + w / 2, y + h / 2);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);
}
```

- [ ] **Step 2: Update `drawPill`'s font**

Change (currently `src/Renderer.cpp:53-62`):

```cpp
void Renderer::drawPill(int x, int y, int w, int h, const char* label) {
  uint16_t fill   = _tft.color565(48, 48, 48);
  uint16_t border = _tft.color565(105, 105, 105);
  _tft.fillRoundRect(x, y, w, h, h / 2, fill);
  _tft.drawRoundRect(x, y, w, h, h / 2, border);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(label, x + w / 2, y + h / 2, 2);
  _tft.setTextDatum(TL_DATUM);
}
```

to:

```cpp
void Renderer::drawPill(int x, int y, int w, int h, const char* label) {
  uint16_t fill   = _tft.color565(48, 48, 48);
  uint16_t border = _tft.color565(105, 105, 105);
  _tft.fillRoundRect(x, y, w, h, h / 2, fill);
  _tft.drawRoundRect(x, y, w, h, h / 2, border);
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(label, x + w / 2, y + h / 2);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);
}
```

- [ ] **Step 3: Update `drawClaude`'s percentage/reset fonts**

Change (currently `src/Renderer.cpp:112-136`, the Session/Weekly blocks — title at the top of `drawClaude` already uses `TITLE_FONT`, unchanged):

```cpp
  // ── Session ────────────────────────────────────────────────────────────────
  uint16_t cSession = progressColor(d.claudeSession);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
  _tft.setTextColor(cSession);
  _tft.drawString(buf, 10, 52, 4);

  drawPill(206, 52, 104, 28, "Session");

  drawProgressBar(10, 86, 300, 14, d.claudeSession, cSession);
  formatReset(buf, sizeof(buf), d.claudeReset);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString(buf, 10, 104, 2);

  _tft.drawFastHLine(0, 128, 320, TFT_DARKGREY);

  // ── Weekly ─────────────────────────────────────────────────────────────────
  uint16_t cWeekly = progressColor(d.claudeWeekly);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
  _tft.setTextColor(cWeekly);
  _tft.drawString(buf, 10, 136, 4);

  drawPill(206, 136, 104, 28, "Weekly");

  drawProgressBar(10, 170, 300, 14, d.claudeWeekly, cWeekly);
```

to:

```cpp
  // ── Session ────────────────────────────────────────────────────────────────
  uint16_t cSession = progressColor(d.claudeSession);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(cSession);
  _tft.drawString(buf, 10, 52);
  _tft.setTextFont(0);

  drawPill(206, 52, 104, 28, "Session");

  drawProgressBar(10, 86, 300, 14, d.claudeSession, cSession);
  formatReset(buf, sizeof(buf), d.claudeReset);
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString(buf, 10, 104);
  _tft.setTextFont(0);

  _tft.drawFastHLine(0, 128, 320, TFT_DARKGREY);

  // ── Weekly ─────────────────────────────────────────────────────────────────
  uint16_t cWeekly = progressColor(d.claudeWeekly);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(cWeekly);
  _tft.drawString(buf, 10, 136);
  _tft.setTextFont(0);

  drawPill(206, 136, 104, 28, "Weekly");

  drawProgressBar(10, 170, 300, 14, d.claudeWeekly, cWeekly);
```

- [ ] **Step 4: Update `updateClaude`'s fonts to match**

Change (currently `src/Renderer.cpp:149-176`, the whole function):

```cpp
void Renderer::updateClaude(const UsageData& d) {
  char buf[24];
  if (d.claudeSession != _prev.claudeSession) {
    uint16_t c = progressColor(d.claudeSession);
    _tft.fillRect(10, 52, 190, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
    _tft.drawString(buf, 10, 52, 4);
    drawProgressBar(10, 86, 300, 14, d.claudeSession, c);
    _prev.claudeSession = d.claudeSession;
  }
  if (d.claudeReset != _prev.claudeReset) {
    _tft.fillRect(10, 104, 260, 16, TFT_BLACK);
    _tft.setTextColor(TFT_DARKGREY);
    formatReset(buf, sizeof(buf), d.claudeReset);
    _tft.drawString(buf, 10, 104, 2);
    _prev.claudeReset = d.claudeReset;
  }
  if (d.claudeWeekly != _prev.claudeWeekly) {
    uint16_t c = progressColor(d.claudeWeekly);
    _tft.fillRect(10, 136, 190, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
    _tft.drawString(buf, 10, 136, 4);
    drawProgressBar(10, 170, 300, 14, d.claudeWeekly, c);
    _prev.claudeWeekly = d.claudeWeekly;
  }
}
```

to:

```cpp
void Renderer::updateClaude(const UsageData& d) {
  char buf[24];
  if (d.claudeSession != _prev.claudeSession) {
    uint16_t c = progressColor(d.claudeSession);
    _tft.fillRect(10, 52, 190, 28, TFT_BLACK);
    _tft.setFreeFont(VALUE_FONT);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
    _tft.drawString(buf, 10, 52);
    _tft.setTextFont(0);
    drawProgressBar(10, 86, 300, 14, d.claudeSession, c);
    _prev.claudeSession = d.claudeSession;
  }
  if (d.claudeReset != _prev.claudeReset) {
    _tft.fillRect(10, 104, 260, 16, TFT_BLACK);
    _tft.setFreeFont(LABEL_FONT);
    _tft.setTextColor(TFT_DARKGREY);
    formatReset(buf, sizeof(buf), d.claudeReset);
    _tft.drawString(buf, 10, 104);
    _tft.setTextFont(0);
    _prev.claudeReset = d.claudeReset;
  }
  if (d.claudeWeekly != _prev.claudeWeekly) {
    uint16_t c = progressColor(d.claudeWeekly);
    _tft.fillRect(10, 136, 190, 28, TFT_BLACK);
    _tft.setFreeFont(VALUE_FONT);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
    _tft.drawString(buf, 10, 136);
    _tft.setTextFont(0);
    drawProgressBar(10, 170, 300, 14, d.claudeWeekly, c);
    _prev.claudeWeekly = d.claudeWeekly;
  }
}
```

- [ ] **Step 5: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 6: Flash and visually verify the Claude screen and nav buttons everywhere**

Run: `pio run -t upload` then `pio device monitor`.

On the physical device:
- Claude screen: "Usage" title, big Session/Weekly percentages, "Session"/"Weekly" pill text, and the reset-time caption all render in the new sans-serif fonts with no clipped or overlapping characters.
- Trigger a data refresh (or wait for the next fetch) and confirm the partial-update path (`updateClaude`) redraws the changed percentage/reset text cleanly, no leftover pixels from the old font.
- Check nav buttons on **all three screens** (`< Settings`/`Grok >` on Claude, `< Claude`/`Settings >` on Grok, `< Grok`/`Claude >` on Settings) — all should show the new label font since they all route through `drawButton`.

- [ ] **Step 7: Commit**

```bash
git add src/Renderer.cpp
git commit -m "$(cat <<'EOF'
Apply consistent fonts to shared button/pill helpers and Claude screen

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Consistent fonts on the Grok screen

**Files:**
- Modify: `src/Renderer.cpp` — `drawGrok`, `updateGrok`

**Interfaces:**
- Consumes: `TITLE_FONT`, `VALUE_FONT`, `LABEL_FONT`, `colorLabel()` (Task 1)

- [ ] **Step 1: Replace `drawGrok`**

Change (currently `src/Renderer.cpp:180-214`, the whole function):

```cpp
void Renderer::drawGrok(const UsageData& d) {
  char buf[8];
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("GROK BUILD", 10, 10, 4);
  _tft.drawFastHLine(0, 44, 320, TFT_DARKGREY);

  uint16_t cTokens = progressColor(d.grokTokens);
  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Tokens", 10, 58, 2);
  _tft.setTextColor(cTokens);
  snprintf(buf, sizeof(buf), "%d%%", d.grokTokens);
  _tft.drawString(buf, 240, 50, 4);
  drawProgressBar(10, 86, 300, 14, d.grokTokens, cTokens);

  _tft.drawFastHLine(0, 112, 320, TFT_DARKGREY);

  uint16_t cReqs = progressColor(d.grokRequests);
  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Requests", 10, 126, 2);
  _tft.setTextColor(cReqs);
  snprintf(buf, sizeof(buf), "%d%%", d.grokRequests);
  _tft.drawString(buf, 240, 118, 4);
  drawProgressBar(10, 154, 300, 14, d.grokRequests, cReqs);

  _tft.drawFastHLine(0, 180, 320, TFT_DARKGREY);
  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(8,   190, 142, 34, "< Claude",   f, b, TFT_WHITE);
    drawButton(170, 190, 142, 34, "Settings >", f, b, TFT_WHITE);
  }

  _prev.grokTokens   = d.grokTokens;
  _prev.grokRequests = d.grokRequests;
}
```

to:

```cpp
void Renderer::drawGrok(const UsageData& d) {
  char buf[8];
  _tft.fillScreen(TFT_BLACK);
  _tft.setFreeFont(TITLE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("GROK BUILD", 10, 10);
  _tft.setTextFont(0);
  _tft.drawFastHLine(0, 44, 320, TFT_DARKGREY);

  uint16_t cTokens = progressColor(d.grokTokens);
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Tokens", 10, 58);
  _tft.setTextFont(0);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(cTokens);
  snprintf(buf, sizeof(buf), "%d%%", d.grokTokens);
  _tft.drawString(buf, 240, 50);
  _tft.setTextFont(0);
  drawProgressBar(10, 86, 300, 14, d.grokTokens, cTokens);

  _tft.drawFastHLine(0, 112, 320, TFT_DARKGREY);

  uint16_t cReqs = progressColor(d.grokRequests);
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Requests", 10, 126);
  _tft.setTextFont(0);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(cReqs);
  snprintf(buf, sizeof(buf), "%d%%", d.grokRequests);
  _tft.drawString(buf, 240, 118);
  _tft.setTextFont(0);
  drawProgressBar(10, 154, 300, 14, d.grokRequests, cReqs);

  _tft.drawFastHLine(0, 180, 320, TFT_DARKGREY);
  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(8,   190, 142, 34, "< Claude",   f, b, TFT_WHITE);
    drawButton(170, 190, 142, 34, "Settings >", f, b, TFT_WHITE);
  }

  _prev.grokTokens   = d.grokTokens;
  _prev.grokRequests = d.grokRequests;
}
```

- [ ] **Step 2: Replace `updateGrok`**

Change (currently `src/Renderer.cpp:216-236`, the whole function):

```cpp
void Renderer::updateGrok(const UsageData& d) {
  char buf[8];
  if (d.grokTokens != _prev.grokTokens) {
    uint16_t c = progressColor(d.grokTokens);
    _tft.fillRect(240, 50, 72, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.grokTokens);
    _tft.drawString(buf, 240, 50, 4);
    drawProgressBar(10, 86, 300, 14, d.grokTokens, c);
    _prev.grokTokens = d.grokTokens;
  }
  if (d.grokRequests != _prev.grokRequests) {
    uint16_t c = progressColor(d.grokRequests);
    _tft.fillRect(240, 118, 72, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.grokRequests);
    _tft.drawString(buf, 240, 118, 4);
    drawProgressBar(10, 154, 300, 14, d.grokRequests, c);
    _prev.grokRequests = d.grokRequests;
  }
}
```

to:

```cpp
void Renderer::updateGrok(const UsageData& d) {
  char buf[8];
  if (d.grokTokens != _prev.grokTokens) {
    uint16_t c = progressColor(d.grokTokens);
    _tft.fillRect(240, 50, 72, 28, TFT_BLACK);
    _tft.setFreeFont(VALUE_FONT);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.grokTokens);
    _tft.drawString(buf, 240, 50);
    _tft.setTextFont(0);
    drawProgressBar(10, 86, 300, 14, d.grokTokens, c);
    _prev.grokTokens = d.grokTokens;
  }
  if (d.grokRequests != _prev.grokRequests) {
    uint16_t c = progressColor(d.grokRequests);
    _tft.fillRect(240, 118, 72, 28, TFT_BLACK);
    _tft.setFreeFont(VALUE_FONT);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.grokRequests);
    _tft.drawString(buf, 240, 118);
    _tft.setTextFont(0);
    drawProgressBar(10, 154, 300, 14, d.grokRequests, c);
    _prev.grokRequests = d.grokRequests;
  }
}
```

- [ ] **Step 3: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 4: Flash and visually verify the Grok screen**

Run: `pio run -t upload` then `pio device monitor`.

On the physical device: confirm "GROK BUILD" title, "Tokens"/"Requests" labels (now muted grey, not cyan), and both big percentages render in the new fonts with no clipping, and that a live data refresh (`updateGrok`'s partial-update path) redraws cleanly.

- [ ] **Step 5: Commit**

```bash
git add src/Renderer.cpp
git commit -m "$(cat <<'EOF'
Apply consistent fonts to Grok screen

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Full hardware QA pass

**Files:** none (verification only)

- [ ] **Step 1: Full walkthrough on hardware**

With the fully-updated firmware flashed, on the physical ESP32-32E CYD board:
- Cycle through Claude → Grok → Settings → Claude using both tap-navigation and the bottom nav buttons; confirm no visual glitches, no leftover pixels from prior screens, and consistent fonts/colors per the design spec (`docs/superpowers/specs/2026-07-04-shadcn-style-settings-and-fonts-design.md`).
- On Settings, exercise every touch control (brightness −/+, all 3 interval buttons, LED toggle, Reboot, both nav buttons) and confirm each still registers in the same physical location as before this plan.
- Let the device sit on Settings and switch back to Claude to confirm the mascot/sprite header still animates correctly (untouched by this plan, but worth a sanity check since `drawClaude` was edited).
- Leave the device running for a few minutes on Claude/Grok to confirm periodic data-refresh partial updates (`updateClaude`/`updateGrok`) redraw without artifacts.

- [ ] **Step 2: Report results**

If everything above passes, this plan is complete — no further commits needed (Task 5 is verification-only). If any issue is found, note the exact screen/control/symptom so a follow-up fix task can be scoped.
