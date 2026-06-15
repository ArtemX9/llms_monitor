#include "TouchRouter.h"

TouchRouter::TouchRouter(TFT_eSPI& tft) : _tft(tft) {}

Event TouchRouter::poll(int screen) {
  uint16_t x, y;
  if (!_tft.getTouch(&x, &y))       return Event::None;
  if (millis() - _lastTouch <= 300)  return Event::None;
  _lastTouch = millis();

  if (screen != 2) {
    // Claude / Grok: full-screen tap navigates
    // x < 160 = visual right (>) = forward; x >= 160 = visual left (<) = backward
    return (x < 160) ? Event::NavForward : Event::NavBack;
  }

  // Settings
  if (y > 215) {
    return (x < 160) ? Event::NavForward : Event::NavBack;
  }
  if (y >= 58 && y <= 90) {
    if (x <= 60)  return Event::BrightnessUp;    // visual right: + button
    if (x >= 260) return Event::BrightnessDown;  // visual left:  − button
    return Event::None;
  }
  if (y >= 122 && y <= 154) {
    int mx = 319 - (int)x; // mirror touch x to visual x (x-axis inverted in rotation 3)
    if (mx >= 10  && mx < 105) return Event::Interval30s;
    if (mx >= 115 && mx < 210) return Event::Interval60s;
    if (mx >= 220 && mx < 315) return Event::Interval120s;
    return Event::None;
  }
  if (y >= 175 && y <= 207 && x >= 60 && x <= 260) return Event::Reboot;
  return Event::None;
}
