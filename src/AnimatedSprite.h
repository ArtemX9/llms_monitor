#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "SpriteData.h"

class AnimatedSprite {
public:
  void tick(unsigned long nowMs);
  bool needsRedraw() const { return _redrawNeeded; }
  void draw(TFT_eSPI& tft);

private:
  enum class Phase { Idle, Entering, AtLeft, ToTitle, AtTitle, ToLeft };

  bool          _initialized  = false;
  int           _character    = 0;
  int           _frame        = 0;
  unsigned long _lastFrameMs  = 0;
  unsigned long _lastRerollMs = 0;
  unsigned long _lastTickMs   = 0;
  Phase         _phase        = Phase::Idle;
  unsigned long _phaseStartMs = 0;
  float         _x            = 5.0f;
  bool          _redrawNeeded = false;

  void reroll(unsigned long nowMs);
};
