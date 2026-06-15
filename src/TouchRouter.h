#pragma once
#include <TFT_eSPI.h>
#include "Types.h"

class TouchRouter {
  TFT_eSPI&     _tft;
  unsigned long _lastTouch = 0;

public:
  TouchRouter(TFT_eSPI& tft);
  Event poll(int screen);
};
