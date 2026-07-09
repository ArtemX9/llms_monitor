#include "TouchRouter.h"

TouchRouter::TouchRouter(TFT_eSPI& tft) : _tft(tft) {}

Event TouchRouter::poll(int screen) {
  uint16_t x, y;
  bool touched   = _tft.getTouch(&x, &y);
  bool touchDown = touched && !_wasTouched;  // rising edge: first poll of a new press
  _wasTouched    = touched;

  // Rotate-icon press tracking (Settings only). Header icon zone x[82,114] y[6,38].
  // Tracking starts only when a press first LANDS on the icon (touch-down edge in
  // the zone), so a drag that began elsewhere and slid onto the icon is ignored.
  // Once claimed, the press is tracked until the finger truly lifts, so drifting
  // off the 32px icon mid-press (common on resistive touch) neither fires a
  // spurious cycle nor restarts the long-press timer.
  if (screen == 2) {
    bool inRotZone = touched && x >= 82 && x <= 114 && y >= 6 && y <= 38;
    if (touched) {
      // Claim the press only on the touch-down that first lands on the icon.
      if (_rotPressStart == 0 && touchDown && inRotZone) { _rotPressStart = millis(); _rotLongFired = false; }
      if (_rotPressStart != 0) {
        // Press originated on the icon; hold the decision until true release,
        // regardless of whether the finger has since drifted off the icon.
        if (!_rotLongFired && millis() - _rotPressStart >= 800) {
          _rotLongFired = true;
          _lastTouch = millis();
          return Event::Recalibrate;   // fires once while still held
        }
        return Event::None;            // swallow while deciding short vs long
      }
      // Touch began elsewhere: fall through to normal zone handling below.
    } else if (_rotPressStart != 0) {
      // Finger truly lifted. Short tap if the long-press never fired.
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
