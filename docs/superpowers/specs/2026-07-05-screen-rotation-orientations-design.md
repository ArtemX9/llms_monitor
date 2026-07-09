# Screen Rotation & Orientation Support

**Date:** 2026-07-05
**Branch:** `esp32-32e-cyd`
**Status:** Approved design

## Goal

Let the user change the display orientation from the Settings screen and support
all four TFT_eSPI rotations (0, 1, 2, 3) — both landscape orientations and both
portrait orientations — with correct touch mapping in each. The chosen orientation
survives reboot.

## Background

The board is physically 240×320. Today the firmware pins rotation to `3`
(landscape, 320×240) in `Renderer::init()` and every screen's drawing code, every
partial-update path, and every `TouchRouter` hit zone is hardcoded for that single
320×240 geometry. Touch calibration `{433, 3490, 314, 3448, 5}` was obtained via
`calibrateTouch()` for rotation 3 specifically.

There are only **two geometries** to support:

| Rotation | Orientation | Canvas (w×h) |
|---|---|---|
| 1, 3 | Landscape | 320 × 240 |
| 0, 2 | Portrait  | 240 × 320 |

## Non-goals

- No Layout-struct refactor. We keep the existing procedural, coordinate-hardcoded
  style and add portrait coordinates via inline `if (portrait)` branches. (Considered
  a shared `Layout` source of truth to also kill the existing Renderer/TouchRouter
  coordinate duplication; rejected to keep this change smaller and land faster. The
  cost we accept: each draw/update function and each touch zone carries its own
  portrait coordinates, and full-draw vs. partial-update pixel-consistency is
  maintained by hand per function.)
- No multi-board build. This stays a hardware-swap branch.

## Design

### 1. Orientation state & persistence

- Add `uint8_t rotation = 3;` to `AppState` in `Types.h` (raw TFT_eSPI rotation 0–3).
- Persist to NVS. Reuse the existing `netcfg` namespace (already used for `proxyIp`),
  new key `rot` (a `uint8_t`).
- Boot order in `setup()`: read `rot` from NVS **before** `renderer.init(...)` so the
  first draw is already in the saved orientation. If no value is stored, default to `3`.
- Runtime change sequence (no reboot):
  1. `state.rotation = next;`
  2. `renderer.setRotation(next)` — calls `_tft.setRotation(next)` then applies that
     rotation's touch calData (see §3).
  3. full redraw of the current screen via `renderer.switchTo(...)`.
  4. save `rot` to NVS.

### 2. Geometry-aware drawing (inline branches)

Introduce a single notion of "is this rotation portrait":
`bool portrait = (rotation == 0 || rotation == 2);`

`Renderer` needs to know the current rotation to branch. Store it as a member
(`uint8_t _rotation`) set by `setRotation()`. A small helper `bool portrait() const`
keeps branches readable.

Every screen gets a portrait coordinate set alongside its existing landscape one:

- `drawClaude` / `updateClaude`
- `drawGrok` / `updateGrok`
- `drawSettings` (+ `drawIntervalButtons`, `updateBrightnessBar`, `drawRebootIcon`,
  `drawLedToggle`, new rotate icon)
- `drawWifiIndicator` (top-right dot position differs per geometry)
- Status screens (`showConnecting`, etc.) — center text using `_tft.width()/height()`
  instead of hardcoded offsets so they work in both geometries with one code path.

**Portrait layout intent (240 wide × 320 tall):**

- *Claude:* header (sprite + "Usage") across the top; HLine; Session block (%, "Session"
  pill, progress bar ~220 wide, reset text); HLine; Weekly block; nav buttons pinned to
  the bottom. Extra vertical room vs. landscape, so blocks get more breathing space;
  progress bars shrink from 300→~220 wide.
- *Grok:* same vertical-stack treatment; bars ~220 wide.
- *Settings:* title row with reboot / LED / rotate icons; Brightness card (~228 wide);
  Refresh card; nav buttons at the bottom. The brightness bar and interval buttons
  shrink to fit 240 width.

Partial-update paths (`updateClaude`, `updateGrok`, `updateBrightnessBar`,
`updateIntervalButtons`, `updateLedToggle`, `updateRebootIcon`) must clear/redraw at
the **same** coordinates their full-draw counterpart used for the current geometry.
Per the board's no-double-buffer quirk, keep clearing to the exact changed region only.

### 3. Touch calibration — derive + on-device recal

**Derive all four in code.** The XPT2046 raw ADC bounds are physically fixed; only the
rotate/invert flags and the axis roles change per rotation. From the rotation-3 baseline
`{433, 3490, 314, 3448, 5}` (flag 5 = rotate=1, invert_x=0, invert_y=1) we know:

- The rotate=true branch maps raw_y → screenX, raw_x → screenY.
- Raw Y range ≈ [433, 3923] (x0=433, span=3490).
- Raw X range ≈ [314, 3762] (y0=314, span=3448).

Construct calData per rotation:

- **Landscape (1, 3):** `rotate = 1`, width/height = 320/240. calData x-part uses the
  raw_y range, y-part uses the raw_x range: `{433, 3490, 314, 3448, flag}`.
- **Portrait (0, 2):** `rotate = 0`, width/height = 240/320. Axis roles swap:
  `{314, 3448, 433, 3490, flag}`.
- **invert bits per rotation:** rotation 3 is the known-good `{invert_x=0, invert_y=1}`.
  The 180° partner (rotation 1) flips both inverts. Portrait rotations 0 and 2 are a
  90° turn (rotate=0) and likewise differ by both inverts. Exact per-rotation invert
  bits are derived in code and **verified on hardware** — if a rotation reads mirrored,
  the recal gesture (below) corrects it permanently without a re-flash.

**NVS override.** Per-rotation calData may be stored in NVS (`netcfg` keys `cal0`…`cal3`,
each a 10-byte blob = 5×uint16_t). `renderer.setRotation(r)` applies the stored blob if
present, otherwise the derived default.

**Recal gesture.** A hidden long-press (~800 ms) on the Settings rotate icon runs
`_tft.calibrateTouch(buf, ...)` for the current rotation, applies the result, and saves
it to `cal{r}`. This is the escape hatch if a derived default is off.

Encapsulate calData construction/lookup in a small helper so `main.cpp` and `Renderer`
share one implementation. NVS access can live next to the existing proxy-cache NVS code
(in `DataFetcher`) or a tiny dedicated helper — implementer's choice, kept in one place.

### 4. Settings UI control

- Add a **rotate icon** to the Settings header, matching the existing reboot/LED icon
  button pattern (`drawRotateIcon(...)`), placed after the LED icon. Portrait Settings
  places the same three icons within the 240-wide header.
- **Short tap** → cycle rotation `3 → 0 → 1 → 2 → 3`, redraw immediately, persist.
- **Long-press (~800 ms)** → recal gesture (§3). The press-and-hold timing is tracked
  in `loop()` similarly to the existing reboot confirm-tap arming, not with a blocking
  delay.
- New events in `Types.h`: `Event::CycleRotation`, `Event::Recalibrate`.
- `TouchRouter` gains a hit zone for the rotate icon (landscape and portrait
  coordinates) and reports the tap; short-vs-long discrimination is handled in `loop()`
  using press-duration, consistent with how reboot arming works today.

### 5. AnimatedSprite

The header mascot patrols within the header on the Claude screen. Its patrol bounds and
draw origin are geometry-dependent (header is 320 wide in landscape, 240 in portrait).
Pass the current width (or a portrait flag) into the sprite so its patrol range and clear
region stay within the header in both geometries.

### 6. Event handling in `loop()`

- `Event::CycleRotation`: compute next rotation, call `renderer.setRotation`, full
  redraw of current screen, persist `rot`.
- `Event::Recalibrate`: call into renderer to run `calibrateTouch` for the current
  rotation, persist the blob, redraw.
- Reset any transient arming state (like reboot's `rebootArmedAt`) when leaving Settings,
  mirroring existing behavior.

## Files touched

- `src/Types.h` — `rotation` field; `CycleRotation`, `Recalibrate` events.
- `src/main.cpp` — load/save `rot` in NVS; boot rotation; handle new events; long-press
  timing.
- `src/Renderer.h/.cpp` — `_rotation` member, `portrait()` helper, `setRotation()`,
  portrait branches in all draw/update paths, rotate icon, geometry-safe status screens,
  calData derivation + NVS override, recal entry point.
- `src/TouchRouter.cpp` — portrait touch zones for every screen; rotate-icon zone.
- `src/AnimatedSprite.h/.cpp` — geometry-aware patrol bounds / clear region.
- `src/DataFetcher.*` (or a small NVS helper) — `rot` and `cal{0..3}` NVS keys.
- `CLAUDE.md` — document the new rotation feature, per-rotation calibration approach,
  and the recal gesture, replacing the "rotation 3 only" framing.

## Testing / verification

This is embedded hardware with no automated test harness, so verification is on-device:

- Boot in each of the 4 rotations (persisted) and confirm each screen renders upright
  and un-clipped in both geometries.
- Confirm touch hits the correct control in every rotation (nav, brightness ±, interval
  buttons, reboot, LED, rotate icon). If any rotation reads mirrored, exercise the recal
  gesture and confirm it persists across reboot.
- Confirm cycling rotation redraws without reboot and the choice survives a power cycle.
- Confirm the mascot patrols within the header (no clipping/tearing) in both geometries.

## Risks

- **Derived invert bits may be wrong for some rotations.** Mitigated by the on-device
  recal gesture + NVS override. Acceptable per the chosen approach.
- **Full-draw / partial-update coordinate drift** between geometries (inline-branch cost).
  Mitigated by keeping each function's landscape and portrait coordinates adjacent and
  reviewing them together.
- **Touch mapping is the historically painful area** on this board (per CLAUDE.md);
  budget on-device iteration for it.
