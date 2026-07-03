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
