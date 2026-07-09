#pragma once
#include <Arduino.h>

struct WifiCredential {
  const char* ssid;
  const char* password;
};

struct UsageData {
  int claudeSession, claudeWeekly, claudeReset;
  int grokUsage, grokReset;
};

struct AppState {
  int           screen          = 0;
  uint8_t       brightness      = 200;
  unsigned long fetchInterval   = 60000;
  bool          needsFullRedraw = true;
  bool          ledEnabled      = true;
  uint8_t       rotation        = 3;   // raw TFT_eSPI rotation 0..3 (3 = landscape default)
  unsigned long rebootArmedAt   = 0; // 0 = disarmed; set to millis() on first reboot-icon tap
};

enum class Event {
  None,
  NavForward, NavBack,
  BrightnessUp, BrightnessDown,
  Interval30s, Interval60s, Interval120s,
  ToggleLed,
  Reboot,
  CycleRotation,
  Recalibrate
};

// Onboard RGB LED status — reflects WiFi/server reachability.
enum class LedSignal {
  Off,          // LED toggle disabled in Settings
  BlueBlinkOn,  // WiFi connecting, blink phase on
  BlueBlinkOff, // WiFi connecting, blink phase off
  Red,          // no WiFi or server unreachable
  Green         // WiFi connected and server reachable
};
