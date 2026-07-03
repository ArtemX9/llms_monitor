#pragma once
#include <Arduino.h>
#include "Types.h"

class DataFetcher {
  const char* _ssid;
  const char* _password;
  const char* _url;
  int _failures = 0;
  bool _ledEnabled = true;
  LedSignal _lastRgbSignal = LedSignal::Red;
  void (*_indicatorCallback)(bool) = nullptr;
  void (*_rgbCallback)(LedSignal) = nullptr;

  void ensureWifi();
  void setIndicator(bool on);
  void setRgb(LedSignal signal);

public:
  DataFetcher(const char* ssid, const char* password, const char* url);
  bool connect(unsigned long timeoutMs = 15000);
  bool fetch(UsageData& out);
  int  consecutiveFailures() const;
  void setLedEnabled(bool enabled);
  void setIndicatorCallback(void (*callback)(bool));
  void setRgbCallback(void (*callback)(LedSignal));
};
