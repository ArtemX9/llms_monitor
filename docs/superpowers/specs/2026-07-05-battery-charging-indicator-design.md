# Battery charging indicator (software voltage-trend heuristic)

## Problem

The battery level indicator (already shipped) shows charge percentage but not
whether the battery is currently charging. The board gives no clean hardware
signal for this — see "Why no hardware signal" below — so this adds a
software-only approximation: infer "charging" from whether recent battery
voltage readings are trending up.

## Why no hardware signal exists (recap from design discussion)

Checked the full ESP32-WROOM-32E main control circuit schematic (vendor
manual, Figure 3.10): every net wired to a GPIO is explicitly listed, and
neither the TP4054's `CHRG` status pin nor the board's `+5V`/`VCC5V` rail
(which would indicate "USB present") appears anywhere in that list. Both are
genuinely unrouted to the microcontroller on this board — not a software gap,
a wiring one. The only path to *real* charging detection is a hardware
modification (bodge `CHRG` to a spare GPIO like `IO35`/`IO39`), which is out
of scope for this feature — this spec is the software-only fallback.

## Non-goals

- No hardware modification (see above) — this is the deliberately-approximate
  software path, chosen after that alternative was discussed and declined.
- No attempt to distinguish "fully charged, charger stopped" from "still
  slowly topping off (CV taper)" — the algorithm is intentionally sticky
  through flat readings (see below), which means it will show "charging" for
  a while after the charger has effectively finished, and there is no fix for
  this without current sensing (which this board doesn't have either).
- No change to the existing battery-level color logic (`TFT_GREEN`/
  `colorDestructive()` by `BAT_LOW_PCT`) — charging state is drawn as an
  overlay glyph, layered independently on top of whatever color the level
  logic already picked, so the two concerns don't interact.
- No new files — this extends the existing `Battery`/`Renderer`/`main.cpp`
  trio from the battery-level feature.

## Detection algorithm

Lives entirely in `Battery`, since it's derived from the same voltage samples
that class already collects — `Renderer` and `main.cpp` only ever see the
resulting bool, never raw mV or streak state (SRP, same boundary as the level
feature).

Each new averaged mV reading is compared to the previous one and classified
into three buckets using a noise-floor threshold, `BAT_CHG_RISE_MV` (15mV —
comfortably above this ADC's jitter even after `BAT_SAMPLE_COUNT`-sample
averaging, per the existing `readPercent()` averaging loop):

- **Rising**: `delta > +BAT_CHG_RISE_MV`
- **Falling**: `delta < -BAT_CHG_RISE_MV`
- **Flat**: everything in between

State machine (two streak counters, both reset by any non-matching sample —
i.e. streaks must be *consecutive*, not just "N rising samples somewhere in
history"):

- Enter `_charging = true` after `BAT_CHG_RISE_STREAK` (3) consecutive
  Rising samples. A Flat or Falling sample resets the rising streak to 0.
- Once charging, a Flat sample does **not** change `_charging` and does not
  count toward the falling streak (this is what keeps the indicator alive
  through the CV taper instead of flapping off as voltage flattens near
  full charge) — it does still reset the falling streak to 0, so exit
  requires a genuinely consecutive decline.
- Exit `_charging = false` after `BAT_CHG_FALL_STREAK` (2) consecutive
  Falling samples.
- The very first reading ever (no previous mV to diff against, tracked via
  a `_lastMv = -1` sentinel) skips classification entirely — `_charging`
  starts `false`.

This means ordinary discharge can never falsely *trigger* charging (a
declining or flat-under-load battery cannot produce 3 consecutive clear
rises), and a real unplug-while-charging reliably exits within 2 fetch
intervals once the battery starts actually dropping under the display's own
load.

### `Config.h` additions

```cpp
#define BAT_CHG_RISE_MV     15  // mV delta beyond which a reading counts as "rising"/"falling" (else "flat")
#define BAT_CHG_RISE_STREAK 3   // consecutive rising reads required to enter "charging"
#define BAT_CHG_FALL_STREAK 2   // consecutive falling reads required to exit "charging"
```

### `Battery.h` / `Battery.cpp` changes

Public surface grows by exactly one method — `readPercent()`'s signature and
behavior are unchanged:

```cpp
class Battery {
public:
  void init();
  int  readPercent();       // unchanged signature; now also updates charging state as a side effect
  bool isCharging() const;  // new — reflects state as of the last readPercent() call

private:
  int  readMilliVoltsAveraged(); // the existing averaging loop, factored out (used once per readPercent() call — no double-sampling)
  void classify(int mv);         // the three-bucket streak logic above

  int  _lastMv        = -1;
  int  _risingStreak  = 0;
  int  _fallingStreak = 0;
  bool _charging      = false;
};
```

```cpp
int Battery::readMilliVoltsAveraged() {
  uint32_t sum = 0;
  for (int i = 0; i < BAT_SAMPLE_COUNT; i++) {
    sum += analogRead(BAT_ADC_PIN);
    delayMicroseconds(100);
  }
  uint32_t raw = sum / BAT_SAMPLE_COUNT;
  return esp_adc_cal_raw_to_voltage(raw, &s_adcChars) * 2;
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

## Icon rendering

`drawBatteryIcon(int pct)` becomes `drawBatteryIcon(int pct, bool charging)`.
When `charging` is true, a small 2-segment lightning-bolt glyph is drawn in
`TFT_WHITE` on top of the existing body fill (after the existing
`drawProgressBar` + nub calls) — same convention as the reboot icon's armed
state, which also draws a white glyph over a colored fill for contrast
against either color. This keeps the level-color logic (green/red by
`BAT_LOW_PCT`) completely untouched; charging is a layered overlay, not a
color change, so a low, charging battery still reads as both "low" (red
body) and "charging" (white bolt on top) simultaneously.

```cpp
void Renderer::drawBatteryIcon(int pct, bool charging) {
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  uint16_t color = (pct > BAT_LOW_PCT) ? TFT_GREEN : colorDestructive();

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
    int bx = _tft.width() - 42; // body's left edge, matches drawProgressBar's x above
    _tft.drawLine(bx + 9, 5,  bx + 6, 8,  TFT_WHITE);
    _tft.drawLine(bx + 6, 8,  bx + 10, 11, TFT_WHITE);
  }
}
```

Both line segments stay within the existing body footprint (`x: bx..bx+16`,
`y: 4..12`) — no change to the icon's overall bounding box, so none of the
six screens' collision clearances (including the flagged, hardware-verified
portrait-Settings risk from the level-indicator spec) are reopened.

`Renderer.h` gains `bool _batteryCharging = false;` next to `_batteryPct`,
and `setBattery`'s signature changes to match:

```cpp
void Renderer::setBattery(int pct, bool charging) {
  _batteryPct = pct;
  _batteryCharging = charging;
  drawBatteryIcon(_batteryPct, _batteryCharging);
}
```

Each of the six existing draw functions' `drawBatteryIcon(_batteryPct);`
call becomes `drawBatteryIcon(_batteryPct, _batteryCharging);` — one word
added per call site, same six locations as the level-indicator feature
(`drawClaude`, `drawClaudePortrait`, `drawGrok`, `drawGrokPortrait`,
`drawSettings`, `drawSettingsPortrait`).

## `main.cpp` integration

Both existing call sites change from a single-expression call to two
statements — **this ordering is required, not stylistic**: C++ does not
guarantee left-to-right evaluation of a function's argument expressions, so
writing `renderer.setBattery(battery.readPercent(), battery.isCharging())`
as one expression risks `isCharging()` evaluating before `readPercent()`
updates the streak state, silently reading last cycle's charging flag
instead of this one's. Splitting into two statements removes the ambiguity:

In `setup()` (replacing the existing single-line call):
```cpp
  battery.init();
  int pct = battery.readPercent();
  renderer.setBattery(pct, battery.isCharging());
```

In `loop()`'s existing fetch-interval block (replacing the existing
single-line call):
```cpp
  if (millis() - lastFetch > state.fetchInterval) {
    int pct = battery.readPercent();
    renderer.setBattery(pct, battery.isCharging());
    if (fetcher.fetch(data)) {
```

## Files changed

- `src/Config.h` — three new constants.
- `src/Battery.h`, `src/Battery.cpp` — `isCharging()`, internal streak state,
  `readMilliVoltsAveraged()`/`classify()` factored out of `readPercent()`.
- `src/Renderer.h` — `_batteryCharging` member; `drawBatteryIcon`/
  `setBattery` signatures gain the `bool charging` parameter.
- `src/Renderer.cpp` — bolt-drawing addition in `drawBatteryIcon`; `charging`
  threaded through `setBattery`; six call-site updates.
- `src/main.cpp` — both `setBattery` call sites split into two statements
  per the evaluation-order note above.

No new files, no changes to `Types.h`, `TouchRouter.*`, `DataFetcher.*`,
`AnimatedSprite.*`, `NvsConfig.*` — charging state is transient, Battery/
Renderer-owned only, same as the level percentage.

## Testing

No automated test harness exists for this embedded project. Verification is
`pio run` compiling cleanly, then hardware checks with the battery connected:

- Plug in USB-C while running on battery: bolt appears within a few fetch
  intervals (not instantly — this is inherent to requiring 3 consecutive
  rising reads, already disclosed as a tradeoff).
- Bolt renders correctly layered on top of both a green (charged) and red
  (low-battery) body fill.
- Unplug USB-C while the bolt is showing: bolt disappears within roughly 2
  fetch intervals of the battery visibly starting to discharge under load.
- Leave it charging until the battery reaches ~100%: confirm the known,
  accepted limitation — the bolt is expected to keep showing for a while
  into the CV-taper flat period, not disappear the instant charging
  current tapers off.
- No new collision with the WiFi dot, titles, sprite, or Settings header
  icons introduced — bolt stays within the existing icon footprint, so this
  should be a no-op check, but confirm on at least one screen since it's a
  new visual element.
