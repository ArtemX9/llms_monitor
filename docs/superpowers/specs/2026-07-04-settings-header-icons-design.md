# Settings screen: header icon buttons for Reboot and LED

## Problem

The Settings screen currently shows Reboot and LED-toggle as full-width labeled
cards in a body row (`drawOutlineCard` at y180-212, see
[Renderer.cpp:326-328](../../../src/Renderer.cpp#L326)). The goal is to move both
into small icon-only buttons in the header, freeing that body row, and to make
Reboot safer against accidental taps now that it sits right next to the title
rather than at arm's length at the bottom of the screen.

## Non-goals

- No change to Claude or Grok screens.
- No change to Brightness or Refresh cards' layout, coordinates, or behavior.
- No new `Event` enum value — `Event::Reboot` and `Event::ToggleLed` are reused;
  arm/confirm timing is `main.cpp`'s concern, not `TouchRouter`'s.
- No persistence of reboot-armed state across screen switches or reboots — it's
  transient UI state, reset to disarmed whenever the Settings screen isn't the
  active screen or the 2-second window lapses.

## Header layout

Header region is y=0 to y=44 (Brightness card starts at y=44, unchanged).

| Element | Position | Notes |
|---|---|---|
| Reboot icon button | `(6, 6, 32, 32)` rounded rect, radius 6 | Outline style: `colorCardBg()` fill, `colorDestructive()` border (normal state) |
| LED icon button | `(44, 6, 32, 32)` rounded rect, radius 6 | Filled bulb glyph, no separate border needed — the fill color itself is the state signal |
| "SETTINGS" title | `x=86, y=8` (was `x=10`) | Same `TITLE_FONT`, same y — only x moves to clear the icons |
| WiFi status dot | `(310, 8)` | Unchanged |

Gap between the two icon buttons: 6px+32px=38, +6px gap = LED starts at x=44. Both fit comfortably before the title at x=86.

## Icon glyphs

Both drawn from `TFT_eSPI` primitives — no icon font or image assets.

**Reboot (refresh/restart glyph):** a broken ring drawn with `_tft.drawArc(cx, cy, r, ir, startAngle, endAngle, fgColor, bgColor, true)` spanning roughly 270° (e.g. 45°→315°, leaving a 90° gap), plus a small filled triangle arrowhead at the arc's leading end (`_tft.fillTriangle(...)`, three points computed from the end angle) to imply direction. Center at the button's center `(22, 22)` relative to button origin, i.e. absolute `(6+16, 6+16) = (22, 22)`; ring radius `r=9`, inner radius `ir=6` (3px-thick ring, comfortably legible at this size).

- Normal (disarmed) state: button background `colorCardBg()`, border `colorDestructive()`, glyph drawn in `colorDestructive()`.
- Armed state (after first tap): button background fills solid `colorDestructive()` (no border needed), glyph redrawn in a contrasting color (`TFT_WHITE`) so it stays visible against the red fill.

**LED (lamp glyph):** a simple bulb shape — a filled circle (the bulb's glass) plus a small filled rectangle beneath it (the base/screw), both drawn in the same single fill color representing state, no separate dot:
- On: `colorLedOn()` — new helper, `color565(34, 197, 94)` (matches the mockup's green, distinct from the general UI accent indigo since this is a literal on/off traffic-light signal, not a "selected" state)
- Off: `colorDestructive()` (red) — reusing the existing destructive-red helper since both represent "off"/negative state colors in this palette

Bulb circle: center `(22, 22)` relative to button origin (absolute `(62, 22)`), radius 8. Base rectangle: `4px` wide, `3px` tall, centered below the circle.

## Body layout after removing the old LED+Reboot row

- Brightness card: unchanged, `drawCard(6, 44, 308, 56)`.
- Refresh card: unchanged, `drawCard(6, 110, 308, 58)`.
- The old row at y180-212 (`drawLedToggle` + the Reboot `drawOutlineCard` call in `drawSettings`) is deleted entirely. The vertical space from y=168 (Refresh card's bottom edge) to the nav buttons' new top edge (y=209, see below) is simply empty — no new element fills it.

## Nav buttons resize

`< Grok` / `Claude >` (`drawButton` calls in `drawSettings`, currently `(8, 216, 142, 22)` and `(170, 216, 142, 22)`):

- Width stays `142` (already uses 304 of the 320px screen width across both buttons; ×1.3 would overflow off-screen).
- Height scales ×1.3: `22 → 29`.
- New y: `209` (was `216`) — keeps the same ~2px bottom margin as before (`209+29=238`, screen height is 240, same bottom margin the old buttons had at `216+22=238`).
- New calls: `drawButton(8, 209, 142, 29, "< Grok", f, b, TFT_WHITE)` and `drawButton(170, 209, 142, 29, "Claude >", f, b, TFT_WHITE)`.

This is the *only* change to Claude/Grok-shared code in this spec — `drawButton`'s signature and body are untouched; only the call-site arguments in `drawSettings` change.

## Reboot confirm-tap mechanics

New `AppState` field: `unsigned long rebootArmedAt = 0;` (`0` means disarmed), declared alongside the struct's other fields in [Types.h](../../../src/Types.h#L14), owned exclusively by `loop()` per existing convention.

Flow, entirely in `main.cpp`'s `loop()`:

1. Tap on the reboot icon always emits `Event::Reboot` (no new `Event` value).
2. On `Event::Reboot`:
   - If `state.rebootArmedAt != 0 && millis() - state.rebootArmedAt < 2000`: this is the confirming second tap — proceed exactly as today (`renderer.showRebooting(); delay(300); ESP.restart();`).
   - Else: this is the arming first tap — set `state.rebootArmedAt = millis();` and call a new `renderer.updateRebootIcon(true)` to redraw the icon in its armed (solid red) visual.
3. Every `loop()` iteration, if `state.screen == 2 && state.rebootArmedAt != 0 && millis() - state.rebootArmedAt > 2000`: auto-disarm — set `state.rebootArmedAt = 0;` and call `renderer.updateRebootIcon(false)` to revert the icon to its normal outline visual.
4. On `Event::NavForward`/`Event::NavBack` while `state.screen == 2` (i.e. leaving Settings): reset `state.rebootArmedAt = 0;` silently — no redraw call needed since `switchTo()` already does a full redraw of the destination screen.

No debounce changes needed in `TouchRouter` — its existing 300ms `_lastTouch` debounce (unrelated to this 2-second arm window) continues to work exactly as today; the two taps needed to confirm a reboot are two separate, individually-debounced touch events.

## TouchRouter changes

Two new zones added to `TouchRouter::poll()`'s `screen == 2` branch, checked before the existing zones (they occupy y=6-38, below any other zone's range):

```cpp
if (y >= 6 && y <= 38) {
  if (x >= 6  && x <= 38) return Event::Reboot;
  if (x >= 44 && x <= 76) return Event::ToggleLed;
  return Event::None;
}
```

The old zone check for the y180-212 row (LED at x10-155, Reboot at x165-310) is deleted entirely — that row no longer has any controls.

The nav-row check moves from `y > 213` to `y > 204` (buffer above the new button top edge at y=209; still well below the Refresh card's bottom edge at y=168 and the interval buttons' zone which ends at y=162, so no ambiguity with the now-empty gap between them).

## Renderer changes

- `drawSettings`: header title x moves to 86; calls a new private `drawRebootIcon(false)` and a rewritten `drawLedToggle(ledEnabled)` (now draws the icon button, not the old text card) at their header positions instead of the old body-row `drawOutlineCard`/`drawLedToggle` calls; nav button calls updated to the new y=209, h=29 coordinates.
- New private `void drawRebootIcon(bool armed)` — draws the button background/border per the armed/disarmed states above, plus the arc+arrowhead glyph.
- New public `void updateRebootIcon(bool armed)` — thin wrapper calling `drawRebootIcon(armed)`, mirroring the existing `updateLedToggle` pattern (declared in `Renderer.h`'s public section alongside `updateLedToggle`).
- `drawLedToggle(bool ledEnabled)` rewritten to draw the bulb glyph (filled circle + base rect) at its new header position and size, using `colorLedOn()`/`colorDestructive()` instead of the old outline-card-with-text approach. Its existing public wrapper `updateLedToggle` needs no signature change — it already just calls `drawLedToggle`.
- New private `uint16_t colorLedOn()` color helper alongside the existing `colorAccent()`/`colorDestructive()`/etc. in the "Shadcn-style palette" block.
- `drawOutlineCard` (added for the previous restyle) becomes unused by `drawSettings` once this change lands. It's still a generically useful helper (rounded card + centered label) — leaving it in place is fine (YAGNI cuts against inventing a reason to delete a small, still-coherent, still-compiling helper it just isn't used by anymore, not against harmless unused code); if a future task never finds a use for it, removing it then is a one-line cleanup, not a design concern for this spec.

## Files changed

- `src/Types.h` — add `rebootArmedAt` field to `AppState`.
- `src/TouchRouter.cpp` — add header icon zones, remove old y180-212 zone, adjust nav-row y threshold.
- `src/Renderer.h` — add `updateRebootIcon` to the public section, `drawRebootIcon` and `colorLedOn` to the private section.
- `src/Renderer.cpp` — all drawing changes described above.
- `src/main.cpp` — reboot arm/confirm/auto-disarm logic in `loop()`.

No changes to `src/DataFetcher.*`, `src/AnimatedSprite.*`, `src/SpriteData.h`, `src/Config.h`, or the Claude/Grok screens beyond the shared `drawButton` call-site coordinate change already covered above.

## Testing

Same as the prior restyle work — no automated test harness exists for `Renderer`
or hardware touch/timing behavior. Verification is `pio run` compiling cleanly,
then hardware flash + manual verification:

- Both header icons render at the correct position/size, don't visually collide
  with the shifted title or the WiFi dot.
- Tapping the LED icon toggles it on/off exactly as before (just via the new
  icon instead of the old text card), with the bulb filling green/red correctly.
- Tapping the reboot icon once arms it (fills solid red); tapping it again
  within 2 seconds actually reboots the device; waiting past 2 seconds without
  a second tap reverts it to the normal outline automatically.
- Navigating away from Settings while armed and back again shows the icon
  disarmed (normal outline), not stuck in the armed state.
- Nav buttons (`< Grok` / `Claude >`) are visibly taller and still register
  taps correctly across their full new height.
- The empty space where the old LED+Reboot row was doesn't respond to taps
  (falls through to `Event::None`).
