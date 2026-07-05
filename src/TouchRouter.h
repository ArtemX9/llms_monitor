#pragma once
#include <TFT_eSPI.h>
#include "Types.h"

class TouchRouter {
  TFT_eSPI&     _tft;
  unsigned long _lastTouch = 0;
  unsigned long _rotPressStart = 0;  // when a press in the rotate-icon zone began (0 = none)
  bool          _rotLongFired  = false; // long-press already emitted for this press

public:
  TouchRouter(TFT_eSPI& tft);
  Event poll(int screen);
};
