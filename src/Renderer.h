#pragma once
#include <TFT_eSPI.h>
#include "Types.h"

class Renderer {
  TFT_eSPI  _tft;
  UsageData _prev = { -1, -1, -1, -1, -1 };

  uint16_t progressColor(int pct);
  void drawProgressBar(int x, int y, int w, int h, int pct, uint16_t color);
  void drawButton(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t border, uint16_t fg);
  void drawPill(int x, int y, int w, int h, const char* label);

  void drawClaude(const UsageData& d);
  void updateClaude(const UsageData& d);
  void drawGrok(const UsageData& d);
  void updateGrok(const UsageData& d);
  void drawSettings(uint8_t brightness, unsigned long fetchInterval, bool ledEnabled);
  void drawLedToggle(bool ledEnabled);

public:
  TFT_eSPI& tft();
  void init(uint8_t brightness);

  void showConnecting();
  void showWifiFailed();
  void showServerError();
  void showRebooting();

  void switchTo(int screen, const UsageData& data,
                uint8_t brightness, unsigned long fetchInterval, bool ledEnabled);
  void update(int screen, const UsageData& data, bool fullRedraw);
  void updateBrightnessBar(uint8_t brightness);
  void updateIntervalButtons(unsigned long fetchInterval);
  void updateLedToggle(bool ledEnabled);
};
