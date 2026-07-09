#pragma once
#include <Arduino.h>

// Owns ADC characterization and battery-percent conversion for the onboard
// GPIO34 voltage divider. Nothing else in the codebase touches this ADC.
class Battery {
public:
  void init();
  int  readPercent();      // averaged, clamped 0..100; also updates charging state as a side effect
  bool isCharging() const; // reflects state as of the last readPercent() call

private:
  int  readMilliVoltsAveraged(); // the averaging loop, used once per readPercent() call
  void classify(int mv);         // three-bucket rising/flat/falling streak logic

  int  _lastMv        = -1; // sentinel: no previous reading yet
  int  _risingStreak  = 0;
  int  _fallingStreak = 0;
  bool _charging      = false;
};
