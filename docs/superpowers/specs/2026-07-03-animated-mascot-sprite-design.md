# Animated mascot sprite — design

## Context

The Claude screen header currently blits a single static 10-col × 12-row pixel-art sprite
(`SPR` in `Renderer.cpp`) inline as part of `drawClaude()`. This design
replaces it with a small library of characters, each with a 4-frame idle animation, one of
which is randomly selected and shown at a time, re-rolled periodically. Two of the
characters additionally patrol back and forth across the header.

Scope: **Claude screen only.** The Grok screen keeps its current text-only header.

## Data format — `src/SpriteData.h`

Pure data, no logic. Same `0/1/2` grid convention as today (`0` = transparent, `1` = body
color, `2` = accent color) — one shared color pair for all characters (`body` = orange,
`dark` = darker orange, same values `drawClaude()` uses today).

Each character is 4 frames of a 12-row × 10-col `PROGMEM` grid, plus a movement type:

```cpp
enum class SpriteMotion { STATIC, PATROL };

struct Character {
  const uint8_t (*frames)[12][10]; // 4 frames
  SpriteMotion motion;
};
```

**Four characters for v1** (grids as previewed and approved in the visual companion
session):

1. **Bot** (today's sprite, reworked into an idle loop) — `STATIC`. Frames cycle
   eyes-open/neutral-smile → blink → eyes-open/wide-smile → eyes-open/neutral-smile.
2. **Ghost** — `PATROL`. Floaty body, wavy two-state tail alternating each frame.
3. **Cat** — `STATIC`. Pointy ears, blinking eyes (blink on frame 3 of 4).
4. **Robot** — `PATROL`. Single eye band that scans across 3 horizontal positions
   (center → left → center → right) over its 4 frames.

Adding a 5th character later means appending 4 grids + one `Character` table entry — no
other code changes.

## `src/AnimatedSprite.h/.cpp`

New class owning all animation state. Does **not** hold a `TFT_eSPI` reference — `draw()`
takes it as a parameter, so there's no constructor-order dependency with `Renderer` (unlike
`TouchRouter`, which does need to capture `renderer.tft()`).

```cpp
class AnimatedSprite {
public:
  void tick(unsigned long nowMs);      // advance frame/position; cheap no-op most calls
  bool needsRedraw() const;            // true if tick() changed frame or position since last draw()
  void draw(TFT_eSPI& tft);            // erase sprite lane + redraw at current frame/position
private:
  int _character;                      // current index into the character table
  int _frame;                          // 0-3
  unsigned long _lastFrameMs;
  unsigned long _lastRerollMs;
  enum class Phase { Idle, Entering, AtLeft, ToTitle, AtTitle, ToLeft } _phase;
  unsigned long _phaseStartMs;
  float _x;                            // current draw x
  void reroll(unsigned long nowMs);    // pick new random character via esp_random(), reset phase
};
```

### Timing constants

| Constant | Value | Notes |
|---|---|---|
| Frame rate (idle animation) | 4 fps (250 ms/frame) | applies to all characters, moving or not |
| Reroll interval | 5 min | `esp_random()`, no seeding needed on ESP32 |
| Entrance (off-screen → left spot) | 5 s, ease-in-out | `PATROL` characters only, plays once per reroll |
| Pause at each stop | 2 s | left spot and title spot |
| Travel leg (left ↔ title) | 3 s, ease-in-out | repeats forever after the one-time entrance |
| Resting/left x | 5 px (today's sprite position) | |
| Title-side x | tuned on hardware, ~90–100 px | must clear the "Usage" title's left edge — exact value picked during implementation by checking on the physical panel |
| Robot hop step size | ~6 px per step, alternating −3px y bounce | quantizes the eased position, doesn't change its own timing |

`STATIC` characters (Bot, Cat) skip all `Phase` states except `Idle`: `_x` is pinned to the
resting x for the character's whole lifetime; only the frame timer runs.

On reroll, `_phase` resets to `Entering` (for `PATROL`) or `Idle` (for `STATIC`) regardless
of whether the newly-picked character happens to match the previous one — no special-casing
for repeats.

## Redraw strategy

Every time `tick()` reports a change, `draw()`:
1. `fillRect(0, 0, 140, 44, TFT_BLACK)` — clears a fixed "sprite lane" spanning the header
   strip left of the "Usage" title. Cheap, and avoids tracking the previous frame's exact
   bounding box.
2. Blits the current character/frame's grid at `(_x, 2)`.

No `fillScreen()` — the rest of the header (title text, divider line) is never touched by
sprite ticks. Full-screen redraws still happen exactly where they do today (screen switch,
data fetch → `drawClaude()`), and `drawClaude()` calls `_sprite.draw()` once as part of its
existing full redraw.

## Integration

- `Renderer` gains `AnimatedSprite _sprite;` and a public `void tickSprite();`:
  ```cpp
  void Renderer::tickSprite() {
    _sprite.tick(millis());
    if (_sprite.needsRedraw()) _sprite.draw(_tft);
  }
  ```
- `drawClaude()` replaces its inline `SPR` blit loop with `_sprite.draw(_tft)`.
- `main.cpp`'s `loop()` calls `renderer.tickSprite()` unconditionally on every iteration
  when `state.screen == 0` — no external rate limiting needed, since `tick()` internally
  no-ops unless ~80ms have elapsed (settles to ~10Hz for position smoothness, independent
  of the 4fps frame rate and 5min reroll timer, both tracked by their own timestamps
  inside `AnimatedSprite`).
- This is a new, independent timer alongside the existing WiFi-fetch timer in `loop()` —
  it does not touch `state.needsFullRedraw` or the fetch cadence.

## Testing

No existing automated test harness in this repo (embedded/visual firmware). Verification
is manual, on the physical device after flashing: confirm each character's idle animation,
confirm Ghost/Robot patrol motion and pacing match what was approved in the visual
companion mockup, confirm reroll picks a new character every ~5 min, and confirm the
sprite lane redraw doesn't clip or flicker the "Usage" title.

## Out of scope (v1)

- Grok screen sprite/header (stays text-only).
- Per-character color variation (all characters share one body/accent color pair).
- More than 4 characters.
- Full off-screen exit-and-re-enter on every patrol cycle (only the very first appearance
  plays the off-screen entrance; subsequent cycles bounce between the left and title spots
  without leaving the screen).
