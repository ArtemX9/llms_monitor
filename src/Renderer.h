#pragma once
#include <TFT_eSPI.h>
#include "Types.h"
#include "AnimatedSprite.h"
#include "NvsConfig.h"

class Renderer {
  TFT_eSPI      _tft;
  UsageData     _prev = { -1, -1, -1, -1, -1 };
  AnimatedSprite _sprite;

  uint8_t _rotation = 3;
  bool portrait() const { return _rotation == 0 || _rotation == 2; }
  void applyTouchCalibration(uint8_t rotation);

  uint16_t progressColor(int pct);
  void drawProgressBar(int x, int y, int w, int h, int pct, uint16_t color);
  void drawButton(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t border, uint16_t fg);
  void drawPill(int x, int y, int w, int h, const char* label);

  uint16_t colorScreenBg();
  uint16_t colorCardBg();
  uint16_t colorCardBorder();
  uint16_t colorAccent();
  uint16_t colorLabel();
  uint16_t colorDestructive();
  void drawCard(int x, int y, int w, int h);
  void drawOutlineCard(int x, int y, int w, int h, uint16_t border, uint16_t fg, const char* label);
  void drawIntervalButtons(unsigned long fetchInterval);
  void drawRebootIcon(bool armed);
  uint16_t colorLedOn();

  void drawClaude(const UsageData& d);
  void updateClaude(const UsageData& d);
  void drawClaudePortrait(const UsageData& d);
  void updateClaudePortrait(const UsageData& d);
  void drawGrok(const UsageData& d);
  void updateGrok(const UsageData& d);
  void drawSettings(uint8_t brightness, unsigned long fetchInterval, bool ledEnabled);
  void drawLedToggle(bool ledEnabled);

public:
  TFT_eSPI& tft();
  void init(uint8_t rotation, uint8_t brightness);
  void setRotation(uint8_t rotation);

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
  void updateRebootIcon(bool armed);
  void drawWifiIndicator(bool on);
  void tickSprite();
};
