#pragma once
#include <Arduino.h>

struct UsageData {
  int claudeSession, claudeWeekly, claudeReset;
  int grokTokens, grokRequests;
};

struct AppState {
  int           screen          = 0;
  uint8_t       brightness      = 200;
  unsigned long fetchInterval   = 60000;
  bool          needsFullRedraw = true;
};

enum class Event {
  None,
  NavForward, NavBack,
  BrightnessUp, BrightnessDown,
  Interval30s, Interval60s, Interval120s,
  Reboot
};
