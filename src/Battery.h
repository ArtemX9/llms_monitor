#pragma once
#include <Arduino.h>

// Owns ADC characterization and battery-percent conversion for the onboard
// GPIO34 voltage divider. Nothing else in the codebase touches this ADC.
class Battery {
public:
  void init();
  int  readPercent(); // averaged, clamped 0..100
};
