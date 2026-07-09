# LP405090 battery support: hardware wiring + on-screen battery level

## Problem

The board has a battery connector (JP2), a TP4054 Li-ion charge/discharge
circuit, and a battery-voltage ADC divider already built in — none of it is
used yet. The goal is to connect an LP405090 (3.7V, 1800mAh) pack and show its
charge level as a small persistent indicator on every screen.

## Non-goals

- No charging-state detection ("is USB plugged in / is it charging"). The
  board's design gives no clean software signal for this: `BAT_ADC` (GPIO34)
  reads the raw battery-node voltage through a fixed divider regardless of
  whether the TP4054 is actively charging it or the discharge FET (Q3) is
  supplying the board from it, and there is no separate "USB present" GPIO
  broken out. Inferring it from voltage trends would need debouncing/hysteresis
  well beyond what a simple level readout needs — out of scope here.
- No icon blinking / animation for the low-battery warning — a static color
  change at the threshold, per the earlier design discussion.
- No coulomb counting, no discharge-curve lookup table — a single linear
  mV→% map, same category of approximation the vendor's own demo sketch uses,
  just with a more realistic 0%/100% floor/ceiling (see below).
- No changes to `AnimatedSprite`, `DataFetcher`, `TouchRouter`, or the
  Claude/Grok/Settings body layouts — this only adds one small corner widget
  to each screen's existing header.

## Hardware wiring (no firmware involved)

- JP2 is a 2-pin, **1.25mm-pitch** header (`BAT+` / `GND`), feeding the
  onboard TP4054 charge IC directly — no external charge circuitry needed.
- ⚠️ LP405090 packs commonly ship with a 2-pin **JST-PH (2.0mm pitch)**
  connector. Check the pack's actual plug against JP2 before connecting — if
  the pitch doesn't match, either source/rewire a 1.25mm-pitch JST plug for
  the pack, or use a 2.0mm→1.25mm adapter. Do not force a mismatched
  connector onto the header.
- ⚠️ **Verify polarity against the PCB's own `+`/`-` silkscreen at JP2**
  before plugging in. This isn't confirmable from the vendor PDF text alone,
  and reversing it can damage the TP4054 circuit.
- Charging current is fixed by the board's R27 (500mA max) — nothing to
  configure in firmware.

## ADC reading

Confirmed from both the schematic ("Battery level detection circuit") and the
vendor's own `13_Get_Battery_Voltage` demo sketch:

- `BAT_ADC` is GPIO34, fed by a 100K/100K divider off `BAT+` (exactly 2:1).
- Read via `analogRead(34)` + `esp_adc_cal_raw_to_voltage()`, then **×2** to
  undo the divider — matches the vendor demo exactly, which is the
  known-working reference for this exact board (see the `User_Setup.h`
  lesson in `CLAUDE.md` about trusting vendor-verified specifics over
  assumptions on this hardware).
- Mapping to percent: the vendor demo uses 2500mV(0%)–4200mV(100%), but
  2500mV is an undervoltage-cutoff sentinel, not a meaningful "empty"
  point for a single-cell LiPo. This design uses **3300mV(0%)–4200mV(100%)**
  instead — a standard, simple linear approximation for a single-cell
  Li-ion/LiPo without a fuel-gauge IC.

## `Config.h` additions

```cpp
#define BAT_ADC_PIN      34
#define BAT_SAMPLE_COUNT 8     // averaged per reading, matches ADC noise-reduction practice
#define BAT_MIN_MV       3300  // maps to 0%
#define BAT_MAX_MV       4200  // maps to 100%
#define BAT_LOW_PCT      15    // battery icon renders red at/below this
```

## New `Battery.h` / `Battery.cpp`

A single-responsibility module, following the project's existing
one-concern-per-file convention (`DataFetcher` owns WiFi, `Renderer` owns
drawing, `Battery` owns ADC→percent — nothing else touches the ADC).

```cpp
class Battery {
public:
  void init();        // characterizes the ADC once via esp_adc_cal
  int  readPercent();  // averages BAT_SAMPLE_COUNT analogRead() samples,
                       // doubles for the divider, clamps/maps to 0-100
};
```

No `readMilliVolts()` or other public surface beyond this — nothing else in
the codebase needs the raw voltage, so there's nothing to expose (YAGNI). No
base class or interface — there is exactly one battery on one board.

## Renderer changes

One new private method draws the entire indicator (icon + percent text) and
is called identically from all six existing draw paths — it needs no
per-screen or per-orientation variant because it positions itself purely from
`_tft.width()`, the same trick `drawWifiIndicator` already uses to work
unmodified across rotations:

```cpp
void drawBatteryIcon(int pct); // private
```

**Geometry** (all relative to `_tft.width()`, so identical in every
rotation):

| Element | Position | Notes |
|---|---|---|
| Clear rect (drawn first) | `x=width()-72, y=2, w=50, h=12`, fill `TFT_BLACK` | Erases the previous frame's text/icon in one step before redrawing, so a shrinking percentage ("100%"→"9%") never leaves a stray digit. `TFT_BLACK` is used even on the Settings screen (whose actual background is `colorScreenBg()`, `rgb(8,8,10)`) — the two are visually indistinguishable at this darkness, so `drawBatteryIcon` stays screen-agnostic rather than taking a background-color parameter for an imperceptible difference. |
| Percent text | `setTextFont(1)`, `setTextColor(color)`, `MR_DATUM` at `(width()-45, 8)` | Right-aligned so it always butts up against the icon regardless of digit count. Font, color, and datum are explicit on every call (not inherited from whatever the previous drawing statement left set) — same discipline the rest of `Renderer.cpp` already follows around every temporary `setFreeFont`/`setTextDatum` use (e.g. `drawClaude`'s `setTextDatum(MC_DATUM)` / `setTextDatum(TL_DATUM)` pair). Resets to `setTextFont(0)` / `setTextDatum(TL_DATUM)` before returning so it never leaves state for whichever draw call runs next. |
| Battery body | `drawProgressBar(width()-42, 4, 16, 8, pct, color)` | Reuses the existing progress-bar helper as-is (border + proportional fill) instead of writing new border/fill logic — it already does exactly what a battery body needs. |
| Nub | `fillRect(width()-26, 6, 2, 4, color)` | The one bit of new drawing code needed; `drawProgressBar` has no concept of a battery nub. |

`color` is `TFT_GREEN` when `pct > BAT_LOW_PCT`, else `colorDestructive()` —
a plain inline ternary, not a new named color-mapping helper (the existing
`progressColor()` helper's high-is-bad semantics are inverted from battery's
high-is-good meaning, so it can't be reused directly, and one ternary isn't
worth abstracting).

This block sits at `x ≈ width()-72..width()-24`, `y=2..14` — clear of the
WiFi dot (`width()-10, r=4`, occupying `width()-14..width()-6`) with a 10px
gap, and clear of every screen's existing header content (title text, the
Settings header icon buttons, and the Claude-screen sprite, whose patrol is
capped at `headerWidth/4` — far left of this corner) in both landscape
orientations and in portrait Claude/Grok.

**Unverified risk — portrait Settings only:** `drawSettingsPortrait` centers
`"SETTINGS"` at `x=178`, explicitly chosen (per its own comment) as the
midpoint of the *entire* free zone from the header icons (ending `x=114`) to
the screen edge (`x=240`) — i.e. it was laid out assuming it may use close
to the full available width up to 240. Static analysis can't confirm the
actual rendered glyph width of that `FreeSans9pt7b` string, so overlap with
this design's icon zone (starting at `x=168`) can't be ruled out without
running it on hardware. **This must be checked during implementation/testing**;
if it overlaps, the fix is narrowing the title's centering zone (e.g. center
within `114..168` instead of `114..240`), a one-line coordinate change in
`drawSettingsPortrait`, not a rework of this design.

**New state:** `int _batteryPct = 100;` member (mirrors `_prev` as
Renderer-owned display state, not `AppState` — nothing outside Renderer needs
this value, same reasoning as why `_prev` isn't in `AppState` either).

**New public method:**

```cpp
void setBattery(int pct); // _batteryPct = pct; drawBatteryIcon(_batteryPct);
```

Called from `main.cpp` whenever a fresh reading is taken. Each of the six
existing draw functions (`drawClaude`/`drawGrok`/`drawSettings` ×
landscape/portrait) gets one added line, `drawBatteryIcon(_batteryPct);`,
right after their `fillScreen()` — using the cached value directly, not
calling `setBattery` again (that would just be a redundant store of the same
value it already holds).

No `updateBattery()` wrapper is needed alongside `setBattery()` — unlike
`updateBrightnessBar`/`updateLedToggle` (which exist because their "draw"
and "update" cases touch different amounts of surrounding UI),
`drawBatteryIcon` is already the minimal, self-contained redraw in both the
full-screen-redraw case and the periodic-tick case. Adding a second method
that does the same thing under a different name would be duplication, not
API clarity.

## `main.cpp` integration

- `#include "Battery.h"`, `Battery battery;` alongside the other global
  objects.
- In `setup()`, right after `renderer.showConnecting()`:
  ```cpp
  battery.init();
  renderer.setBattery(battery.readPercent());
  ```
- In `loop()`, inside the existing fetch-interval block (so the reading
  refreshes on the same cadence as the data fetch, independent of whether
  that fetch itself succeeds):
  ```cpp
  if (millis() - lastFetch > state.fetchInterval) {
    renderer.setBattery(battery.readPercent());
    if (fetcher.fetch(data)) {
      ...
  ```

No new `Event`, no new `AppState` field, no new timer variable — it rides
the fetch-interval timer that already exists.

## Files changed

- `src/Config.h` — battery ADC pin + mV/percent constants.
- `src/Battery.h`, `src/Battery.cpp` — new module.
- `src/Renderer.h` — add `_batteryPct` member, `drawBatteryIcon` (private),
  `setBattery` (public).
- `src/Renderer.cpp` — `drawBatteryIcon` implementation; one added call in
  each of the six draw functions.
- `src/main.cpp` — instantiate `Battery`, `init()` + initial `setBattery()`
  call in `setup()`, refresh call in `loop()`'s fetch-interval block.

No changes to `Types.h`, `TouchRouter.*`, `DataFetcher.*`, `AnimatedSprite.*`,
`SpriteData.h`, or `NvsConfig.*` — the battery level is not persisted and has
no touch interaction.

## Testing

No automated test harness exists for `Renderer` or ADC/hardware behavior
(consistent with the rest of this project). Verification is `pio run`
compiling cleanly, then hardware flash + manual checks with the battery
physically connected:

- Battery icon renders in the top-right corner on all three screens, in both
  landscape and portrait, without colliding with the WiFi dot, titles, sprite,
  or Settings header icons — **specifically check portrait Settings**, the
  one placement this design couldn't verify statically (see the flagged risk
  above); narrow the title's centering zone in `drawSettingsPortrait` if it
  overlaps.
- Percentage number and fill level are plausible against a multimeter
  reading of the battery voltage (allowing for the linear approximation).
- Unplugging USB (battery-only power) and replugging it doesn't change the
  displayed percentage in a discontinuous/wrong way (voltage-divider node is
  the same either way).
- Icon turns red at/below `BAT_LOW_PCT` (15%) — verified either on a
  genuinely low pack or by temporarily lowering the threshold for the test.
- A shrinking percentage (e.g. "100%" → "9%" across a reading) leaves no
  stray leftover digit from the previous frame.
