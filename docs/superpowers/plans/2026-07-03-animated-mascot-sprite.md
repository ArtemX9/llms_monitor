# Animated Mascot Sprite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the static header sprite on the Claude screen with a library of 4 pixel-art characters, one randomly selected and animated at a time, re-rolled every 5 minutes — two of which (Ghost, Robot) patrol back and forth across the header.

**Architecture:** A new `AnimatedSprite` class owns all animation state (character choice, frame index, patrol phase, position) and is driven by a `tick(millis())` call from `loop()`; it redraws only a small fixed "sprite lane" rect in the header, never the full screen. Character pixel data lives in a separate pure-data header (`SpriteData.h`). `Renderer` owns one `AnimatedSprite` instance and exposes `tickSprite()`.

**Tech Stack:** C++/Arduino framework, ESP32 (esp32dev), TFT_eSPI (vendored in `lib/`), PlatformIO. No test framework exists in this repo — verification is `pio run` compilation checks plus a final manual on-device check (per the design spec's Testing section).

## Global Constraints

- Board: ESP32-WROOM-32E, ST7789P3 240×320 rotation 3 — see `CLAUDE.md` for hardware quirks (`USER_SETUP_LOADED=1` must stay set; vendored `lib/TFT_eSPI` must keep being used, not the registry copy).
- Scope: Claude screen only. Grok screen is untouched.
- Shared body/accent color pair for all characters (no per-character colors).
- `esp_random()` for character selection — no seeding needed on ESP32.
- Full spec: `docs/superpowers/specs/2026-07-03-animated-mascot-sprite-design.md`.

---

### Task 1: Sprite data + `AnimatedSprite` class (standalone, not yet wired in)

**Files:**
- Create: `src/SpriteData.h`
- Create: `src/AnimatedSprite.h`
- Create: `src/AnimatedSprite.cpp`

**Interfaces:**
- Produces (consumed by Task 2):
  - `enum class SpriteMotion { STATIC, PATROL_GLIDE, PATROL_HOP };`
  - `struct Character { const SpriteFrame* frames[4]; SpriteMotion motion; };`
  - `static const int NUM_CHARACTERS = 4;`
  - `static const Character CHARACTERS[NUM_CHARACTERS];`
  - `class AnimatedSprite { public: void tick(unsigned long nowMs); bool needsRedraw() const; void draw(TFT_eSPI& tft); ... };`

- [ ] **Step 1: Create `src/SpriteData.h` with the 4 characters' pixel grids**

```cpp
#pragma once
#include <Arduino.h>

// Pixel-art mascot data: 12 rows x 10 cols per frame, 4 frames per character.
// 0=transparent, 1=body, 2=accent — the actual colors are chosen by the
// caller (AnimatedSprite::draw), not stored here.

typedef uint8_t SpriteFrame[12][10];

enum class SpriteMotion { STATIC, PATROL_GLIDE, PATROL_HOP };

struct Character {
  const SpriteFrame* frames[4];
  SpriteMotion        motion;
};

// ── Character 0: Bot — blink + smile-widen idle loop, stays in place ───────
static const SpriteFrame BOT_F0 PROGMEM = {
  {0,1,0,0,0,0,0,0,1,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,2,1,1,1,1,2,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,2,1,1,1,1,2,1,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {0,0,1,1,0,0,1,1,0,0},
  {0,0,1,1,0,0,1,1,0,0},
};
static const SpriteFrame BOT_F1 PROGMEM = { // blink
  {0,1,0,0,0,0,0,0,1,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,2,1,1,1,1,2,1,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {0,0,1,1,0,0,1,1,0,0},
  {0,0,1,1,0,0,1,1,0,0},
};
static const SpriteFrame BOT_F2 PROGMEM = { // wide smile
  {0,1,0,0,0,0,0,0,1,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,2,1,1,1,1,2,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,2,2,1,1,2,2,1,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {0,0,1,1,0,0,1,1,0,0},
  {0,0,1,1,0,0,1,1,0,0},
};

// ── Character 1: Ghost — floaty, wavy tail, patrols with a smooth glide ────
static const SpriteFrame GHOST_F0 PROGMEM = { // tail wave A
  {0,0,0,1,1,1,1,0,0,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,2,1,1,1,1,2,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,0,1,1,0,1,1,0,1,1},
  {0,0,0,0,0,0,0,0,0,0},
};
static const SpriteFrame GHOST_F1 PROGMEM = { // tail wave B
  {0,0,0,1,1,1,1,0,0,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,2,1,1,1,1,2,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,1,0,1,1,0,1,1,0},
  {0,0,0,0,0,0,0,0,0,0},
};

// ── Character 2: Cat — pointy ears, blinking, stays in place ───────────────
static const SpriteFrame CAT_F_OPEN PROGMEM = {
  {1,0,0,0,0,0,0,0,0,1},
  {1,1,0,0,0,0,0,0,1,1},
  {0,1,1,1,1,1,1,1,1,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,2,1,1,1,2,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,1,2,1,1,2,1,1,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {0,0,1,1,0,0,1,1,0,0},
  {0,0,1,1,0,0,1,1,0,0},
};
static const SpriteFrame CAT_F_BLINK PROGMEM = {
  {1,0,0,0,0,0,0,0,0,1},
  {1,1,0,0,0,0,0,0,1,1},
  {0,1,1,1,1,1,1,1,1,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,1,2,1,1,2,1,1,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {0,0,1,1,0,0,1,1,0,0},
  {0,0,1,1,0,0,1,1,0,0},
};

// ── Character 3: Robot — single eye band scans left/right, patrols by hop ──
static const SpriteFrame ROBOT_F_CENTER PROGMEM = {
  {0,0,1,1,1,1,1,1,0,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,2,2,2,2,1,1,1},
  {1,1,1,2,2,2,2,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,1,1,1,1,1,1,1,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {0,0,1,1,0,0,1,1,0,0},
  {0,0,1,1,0,0,1,1,0,0},
};
static const SpriteFrame ROBOT_F_LEFT PROGMEM = {
  {0,0,1,1,1,1,1,1,0,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,2,2,2,2,1,1,1,1,1},
  {1,2,2,2,2,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,1,1,1,1,1,1,1,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {0,0,1,1,0,0,1,1,0,0},
  {0,0,1,1,0,0,1,1,0,0},
};
static const SpriteFrame ROBOT_F_RIGHT PROGMEM = {
  {0,0,1,1,1,1,1,1,0,0},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,2,2,2,2,1},
  {1,1,1,1,1,2,2,2,2,1},
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,1,1,1,1,1,1,1,0},
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1},
  {0,0,1,1,0,0,1,1,0,0},
  {0,0,1,1,0,0,1,1,0,0},
};

static const int NUM_CHARACTERS = 4;
static const Character CHARACTERS[NUM_CHARACTERS] = {
  { {&BOT_F0, &BOT_F1, &BOT_F2, &BOT_F0}, SpriteMotion::STATIC },
  { {&GHOST_F0, &GHOST_F1, &GHOST_F0, &GHOST_F1}, SpriteMotion::PATROL_GLIDE },
  { {&CAT_F_OPEN, &CAT_F_OPEN, &CAT_F_BLINK, &CAT_F_OPEN}, SpriteMotion::STATIC },
  { {&ROBOT_F_CENTER, &ROBOT_F_LEFT, &ROBOT_F_CENTER, &ROBOT_F_RIGHT}, SpriteMotion::PATROL_HOP },
};
```

- [ ] **Step 2: Create `src/AnimatedSprite.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "SpriteData.h"

class AnimatedSprite {
public:
  void tick(unsigned long nowMs);
  bool needsRedraw() const { return _redrawNeeded; }
  void draw(TFT_eSPI& tft);

private:
  enum class Phase { Idle, Entering, AtLeft, ToTitle, AtTitle, ToLeft };

  bool          _initialized  = false;
  int           _character    = 0;
  int           _frame        = 0;
  unsigned long _lastFrameMs  = 0;
  unsigned long _lastRerollMs = 0;
  unsigned long _lastTickMs   = 0;
  Phase         _phase        = Phase::Idle;
  unsigned long _phaseStartMs = 0;
  float         _x            = 5.0f;
  bool          _redrawNeeded = false;

  void reroll(unsigned long nowMs);
};
```

- [ ] **Step 3: Create `src/AnimatedSprite.cpp`**

```cpp
#include "AnimatedSprite.h"
#include <esp_random.h>
#include <cmath>

namespace {
  constexpr int   LANE_W  = 140;
  constexpr int   LANE_H  = 44;
  constexpr int   SPRITE_Y = 2;
  constexpr float START_X = -33.0f;
  constexpr float REST_X  = 5.0f;
  constexpr float TITLE_X = 95.0f; // TODO: tune on hardware to clear the "Usage" title's left edge

  constexpr unsigned long ENTER_DUR_MS       = 5000;
  constexpr unsigned long PAUSE_MS           = 2000;
  constexpr unsigned long LEG_DUR_MS         = 3000;
  constexpr unsigned long FRAME_INTERVAL_MS  = 250;
  constexpr unsigned long REROLL_INTERVAL_MS = 300000UL;
  constexpr unsigned long MIN_TICK_MS        = 80;

  constexpr int HOP_STEP_PX   = 6;
  constexpr int HOP_BOUNCE_PX = 3;
  constexpr int NUM_FRAMES    = 4;

  float easeInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
  }
}

void AnimatedSprite::reroll(unsigned long nowMs) {
  _character    = esp_random() % NUM_CHARACTERS;
  _frame        = 0;
  _lastFrameMs  = nowMs;
  _lastRerollMs = nowMs;
  if (CHARACTERS[_character].motion == SpriteMotion::STATIC) {
    _phase = Phase::Idle;
    _x     = REST_X;
  } else {
    _phase        = Phase::Entering;
    _phaseStartMs = nowMs;
    _x            = START_X;
  }
}

void AnimatedSprite::tick(unsigned long nowMs) {
  _redrawNeeded = false;

  if (!_initialized) {
    _initialized = true;
    reroll(nowMs);
    _lastTickMs   = nowMs;
    _redrawNeeded = true;
    return;
  }

  if (nowMs - _lastRerollMs >= REROLL_INTERVAL_MS) {
    reroll(nowMs);
    _redrawNeeded = true;
  }

  if (nowMs - _lastTickMs < MIN_TICK_MS) return;
  _lastTickMs = nowMs;

  if (nowMs - _lastFrameMs >= FRAME_INTERVAL_MS) {
    _frame       = (_frame + 1) % NUM_FRAMES;
    _lastFrameMs = nowMs;
    _redrawNeeded = true;
  }

  float newX = _x;
  switch (_phase) {
    case Phase::Idle:
      newX = REST_X;
      break;
    case Phase::Entering: {
      float p = float(nowMs - _phaseStartMs) / ENTER_DUR_MS;
      if (p >= 1.0f) {
        newX = REST_X;
        _phase = Phase::AtLeft;
        _phaseStartMs = nowMs;
      } else {
        newX = START_X + (REST_X - START_X) * easeInOutQuad(p);
      }
      break;
    }
    case Phase::AtLeft:
      newX = REST_X;
      if (nowMs - _phaseStartMs >= PAUSE_MS) {
        _phase = Phase::ToTitle;
        _phaseStartMs = nowMs;
      }
      break;
    case Phase::ToTitle: {
      float p = float(nowMs - _phaseStartMs) / LEG_DUR_MS;
      if (p >= 1.0f) {
        newX = TITLE_X;
        _phase = Phase::AtTitle;
        _phaseStartMs = nowMs;
      } else {
        newX = REST_X + (TITLE_X - REST_X) * easeInOutQuad(p);
      }
      break;
    }
    case Phase::AtTitle:
      newX = TITLE_X;
      if (nowMs - _phaseStartMs >= PAUSE_MS) {
        _phase = Phase::ToLeft;
        _phaseStartMs = nowMs;
      }
      break;
    case Phase::ToLeft: {
      float p = float(nowMs - _phaseStartMs) / LEG_DUR_MS;
      if (p >= 1.0f) {
        newX = REST_X;
        _phase = Phase::AtLeft;
        _phaseStartMs = nowMs;
      } else {
        newX = TITLE_X + (REST_X - TITLE_X) * easeInOutQuad(p);
      }
      break;
    }
  }

  if (newX != _x) {
    _x = newX;
    _redrawNeeded = true;
  }
}

void AnimatedSprite::draw(TFT_eSPI& tft) {
  tft.fillRect(0, 0, LANE_W, LANE_H, TFT_BLACK);

  const uint16_t body = tft.color565(210, 90, 42);
  const uint16_t dark = tft.color565(130, 50, 15);

  const Character&    ch    = CHARACTERS[_character];
  const SpriteFrame&   frame = *ch.frames[_frame];

  int drawX = (int)_x;
  int drawY = SPRITE_Y;

  if (ch.motion == SpriteMotion::PATROL_HOP) {
    long step = lroundf(drawX / (float)HOP_STEP_PX);
    drawX = (int)(step * HOP_STEP_PX);
    drawY = SPRITE_Y - ((step % 2 == 0) ? 0 : HOP_BOUNCE_PX);
  }

  for (int r = 0; r < 12; r++) {
    for (int c = 0; c < 10; c++) {
      uint8_t v = pgm_read_byte(&frame[r][c]);
      if (v) tft.fillRect(drawX + c * 3, drawY + r * 3, 3, 3, v == 1 ? body : dark);
    }
  }

  _redrawNeeded = false;
}
```

- [ ] **Step 4: Compile check**

Run: `pio run`
Expected: `SUCCESS` (this compiles `AnimatedSprite.cpp` as its own translation unit even though nothing references the class yet — PlatformIO compiles every `.cpp` under `src/`).

- [ ] **Step 5: Commit**

```bash
git add src/SpriteData.h src/AnimatedSprite.h src/AnimatedSprite.cpp
git commit -m "Add AnimatedSprite class and mascot pixel-art data

Not yet wired into Renderer — standalone, compile-checked only."
```

---

### Task 2: Wire `AnimatedSprite` into `Renderer`, remove the old static sprite

**Files:**
- Modify: `src/Renderer.h`
- Modify: `src/Renderer.cpp:21-36` (delete old `SPR` array), `src/Renderer.cpp:117-124` (replace inline blit)

**Interfaces:**
- Consumes: `AnimatedSprite` from Task 1 (`tick()`, `needsRedraw()`, `draw()`).
- Produces (consumed by Task 3): `void Renderer::tickSprite();`

- [ ] **Step 1: Add the `AnimatedSprite` member and `tickSprite()` declaration to `src/Renderer.h`**

In `src/Renderer.h`, add the include and member/method:

```cpp
#pragma once
#include <TFT_eSPI.h>
#include "Types.h"
#include "AnimatedSprite.h"

class Renderer {
  TFT_eSPI      _tft;
  UsageData     _prev = { -1, -1, -1, -1, -1 };
  AnimatedSprite _sprite;
```

And add `void tickSprite();` to the public section, alongside the other `void` methods (e.g. right after `void drawWifiIndicator(bool on);`).

- [ ] **Step 2: Delete the old `SPR` array from `src/Renderer.cpp`**

Remove these lines (currently `src/Renderer.cpp:21-36`):

```cpp
// Pixel-art sprite: 10 cols × 12 rows, each cell = 3×3 px → 30×36 px total
// 0=transparent, 1=body, 2=accent
static const uint8_t SPR[12][10] PROGMEM = {
  {0,1,0,0,0,0,0,0,1,0},   // antennae
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,1,1,1,1,1,1,0,0},   // head
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,2,1,1,1,1,2,1,1},   // eyes
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,2,1,1,1,1,2,1,0},   // smile
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},   // body
  {1,1,1,1,1,1,1,1,1,1},
  {0,0,1,1,0,0,1,1,0,0},   // legs
  {0,0,1,1,0,0,1,1,0,0},
};
```

- [ ] **Step 3: Replace the inline blit loop in `drawClaude()` with `_sprite.draw(_tft)`**

In `drawClaude()` (currently `src/Renderer.cpp:117-124`), replace:

```cpp
  // ── Header: sprite + title ─────────────────────────────────────────────────
  const uint16_t body = _tft.color565(210, 90, 42);
  const uint16_t dark = _tft.color565(130, 50, 15);
  for (int r = 0; r < 12; r++)
    for (int c = 0; c < 10; c++) {
      uint8_t v = pgm_read_byte(&SPR[r][c]);
      if (v) _tft.fillRect(5 + c * 3, 2 + r * 3, 3, 3, v == 1 ? body : dark);
    }
```

with:

```cpp
  // ── Header: sprite + title ─────────────────────────────────────────────────
  _sprite.draw(_tft);
```

- [ ] **Step 4: Add `Renderer::tickSprite()` at the end of `src/Renderer.cpp`**

```cpp
void Renderer::tickSprite() {
  _sprite.tick(millis());
  if (_sprite.needsRedraw()) _sprite.draw(_tft);
}
```

- [ ] **Step 5: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 6: Commit**

```bash
git add src/Renderer.h src/Renderer.cpp
git commit -m "Wire AnimatedSprite into Renderer, remove static header sprite"
```

---

### Task 3: Drive the sprite tick from `loop()`

**Files:**
- Modify: `src/main.cpp:55-100` (inside `loop()`)

**Interfaces:**
- Consumes: `Renderer::tickSprite()` from Task 2.

- [ ] **Step 1: Call `renderer.tickSprite()` once per loop iteration while on the Claude screen**

In `src/main.cpp`, at the top of `loop()` (right after the opening brace, before the `touch.poll()` switch, so it runs every iteration regardless of touch/fetch events):

```cpp
void loop() {
  if (state.screen == 0) renderer.tickSprite();

  switch (touch.poll(state.screen)) {
```

- [ ] **Step 2: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "Drive AnimatedSprite tick from the main loop on the Claude screen"
```

---

### Task 4: Flash and verify on hardware

**Files:** none (verification only)

- [ ] **Step 1: Build and upload**

Run: `pio run -t upload`
Expected: upload succeeds, device reboots.

- [ ] **Step 2: Manual verification checklist (per the design spec's Testing section)**

With the device on the Claude screen, over a few minutes confirm:
- A mascot appears in the header and idle-animates (Bot: blinks/smile-widens; Cat: blinks; Ghost/Robot: idle-cycle continues even while paused).
- If Ghost or Robot is showing: it plays a one-time off-screen entrance, then repeatedly paces left-spot → title-spot → left-spot with visible pauses at each end, without ever leaving the screen after the first entrance.
- The sprite never clips or flickers the "Usage" title text. If it does, adjust `TITLE_X` in `AnimatedSprite.cpp` (currently `80.0f`) down until it clears the title's left edge, then re-flash — `LANE_W` derives from `TITLE_X` automatically, so it doesn't need separate tuning.
- Every WiFi fetch / screen switch still redraws the rest of the Claude screen correctly (progress bars, percentages) — the sprite change shouldn't have disturbed `drawClaude()`/`updateClaude()`.
- Leave it running ~5+ minutes to confirm the character changes (reroll).

- [ ] **Step 3: If `TITLE_X` needed adjustment, commit the tuned value**

```bash
git add src/AnimatedSprite.cpp
git commit -m "Tune TITLE_X to clear the Usage title on physical hardware"
```

(Skip this step if no adjustment was needed.)
