# Shadcn-style Settings restyle + consistent fonts across all screens

## Problem

The Settings screen (`Renderer::drawSettings`, [Renderer.cpp:240-296](../../../src/Renderer.cpp#L240)) uses
bright, saturated semantic colors (`TFT_GREEN`, `TFT_YELLOW`, `TFT_RED`, `TFT_CYAN`) and plain
square outlines (`drawRect`/`drawFastHLine` dividers) — a much rougher look than the rounded,
neutral-grey nav buttons already used at the bottom of every screen (`drawButton`,
[Renderer.cpp:43](../../../src/Renderer.cpp#L43)).

Separately, only the Claude screen's "Usage" header uses the vendored vector font
(`FreeSansBold12pt7b`, `TITLE_FONT` at [Renderer.cpp:3](../../../src/Renderer.cpp#L3)) — every
other title, label, and number on all three screens uses TFT_eSPI's blocky built-in GLCD fonts
(font 2 / font 4).

Goal: make Settings visually resemble a Shadcn UI component set (neutral dark cards, one muted
accent color, rounded corners), and make font usage consistent across Claude, Grok, and Settings.

## Non-goals

- No change to touch hit-box coordinates in `TouchRouter.cpp` — all touch zones stay exactly
  where they are today.
- No change to the bottom nav buttons' existing style (`< Grok` / `Claude >` / `< Settings` /
  `Grok >`) — they're already neutral-grey rounded rects and are the reference point the rest of
  the UI is matching.
- No change to `AnimatedSprite`/mascot rendering.
- No change to data/fetch logic, WiFi, or any non-Renderer code.

## Color palette

Replaces bright semantic colors on the Settings screen only (Claude/Grok progress-bar colors via
`progressColor()` are unrelated status indicators and are out of scope):

| Role | Value | Replaces |
|---|---|---|
| Screen background | `color565(8,8,10)` | `TFT_BLACK` |
| Card background | `color565(24,24,27)` | (new — cards didn't exist before) |
| Card border (default) | `color565(63,63,70)` | `TFT_DARKGREY` |
| Accent (selected/active/focus) | `color565(99,102,241)` | `TFT_GREEN` / `TFT_YELLOW` |
| Label / caption text | `color565(140,140,145)` | `TFT_CYAN` |
| Primary value text | `TFT_WHITE` | unchanged |
| Destructive (Reboot) | `color565(220,38,38)` | `TFT_RED` |

## Settings layout: cards instead of dividers

Each control group becomes its own rounded-rect card (radius 6, matching `drawButton`'s existing
radius) drawn at the *same outer pixel footprint* the section already occupies, so
`TouchRouter::poll()`'s hit-boxes ([TouchRouter.cpp:18-36](../../../src/TouchRouter.cpp#L18))
need no changes:

1. **Brightness card** — spans the label + `−`/bar/`+` row. Bar fill and both buttons switch to
   accent indigo fill on interaction; card border uses the default border color.
2. **Refresh card** — spans label + 3 interval buttons. Selected interval gets an accent border
   + accent text instead of green.
3. **LED toggle card** and **Reboot card** — same footprint as today, side by side; LED uses
   accent border+text when on, default border+grey text when off; Reboot uses the destructive
   red border+text (outline style, not solid fill — consistent with the other outline-style
   cards).

`drawFastHLine` section dividers are removed; the card borders themselves separate sections.

The three partial-update functions (`updateBrightnessBar`, `updateIntervalButtons`,
`updateLedToggle`) keep redrawing only their existing fixed pixel rectangle — same
single-pass-redraw discipline already used elsewhere in this codebase (see CLAUDE.md's note on
this board's lack of double buffering); no new flicker risk is introduced since nothing here is a
fast-repeating redraw.

## Font consistency (all three screens)

Three roles, using the vendored GFXFF free fonts already available under
`lib/TFT_eSPI/Fonts/GFXFF/`:

| Role | Font | Replaces | Used for |
|---|---|---|---|
| Title | `FreeSansBold12pt7b` (already used on Claude) | GLCD font 4 | "Usage", "GROK BUILD", "SETTINGS" |
| Value | `FreeSansBold18pt7b` | GLCD font 4 | Big percentages, Grok token/request numbers, Settings `−`/`+`, REBOOT, LED ON/OFF |
| Label | `FreeSans9pt7b` | GLCD font 2 | Pill text, reset-time caption, Tokens/Requests labels, Settings section labels, interval buttons, bottom nav buttons |

Mechanics:
- Replace `drawString(str, x, y, N)` (GLCD font-number form) with `setFreeFont(&FONT)` +
  `drawString(str, x, y)`, followed by `setTextFont(1)` to restore the default GLCD font for any
  code path that doesn't immediately set another free font next.
- Every centered string (`MC_DATUM`) needs no position changes — centering is computed from
  actual rendered width regardless of font metrics.
- Fixed-position (`TL_DATUM`) strings — percentages, titles, section labels — keep their current
  x/y anchors. All current strings are short relative to their allotted space, so proportional
  font width (vs. GLCD's fixed width) is not expected to cause overlap or clipping, but this
  should be visually confirmed on hardware once implemented (see Testing).
- Touches: `drawClaude`, `updateClaude`, `drawGrok`, `updateGrok`, `drawSettings`,
  `updateBrightnessBar`, `updateIntervalButtons`, `drawLedToggle`, `drawButton`, `drawPill`.

## Files changed

- `src/Renderer.cpp` — all drawing/color/font changes described above.
- `src/Renderer.h` — only if new color or font constants need declaring at class/file scope
  (likely just `#define`s near the top of `Renderer.cpp`, same pattern as the existing
  `TITLE_FONT`).

No changes to `TouchRouter.cpp`, `TouchRouter.h`, `main.cpp`, `Types.h`, `Config.h`,
`DataFetcher.*`, `AnimatedSprite.*`, or `SpriteData.h`.

## Testing

This is a pure rendering change with no unit-testable logic (no test harness exists for
`Renderer` — it's hardware-drawing code). Verification is visual, on the actual ESP32-32E CYD
hardware:

- Flash and check all three screens (Claude, Grok, Settings) for correct colors, rounded card
  borders, and font rendering with no clipped/overlapping text.
- Exercise every Settings touch control (brightness −/+, all 3 interval buttons, LED toggle,
  Reboot, both nav buttons) to confirm hit-boxes still line up with the restyled (but
  same-footprint) visuals.
- Confirm partial updates (brightness drag, interval tap, LED toggle) redraw cleanly with no
  visible artifacts, matching the existing "diff-only redraw" discipline.
