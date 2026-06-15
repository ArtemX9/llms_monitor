#pragma once
#include <Arduino.h>
#include "Types.h"

class DataFetcher {
  const char* _ssid;
  const char* _password;
  const char* _url;
  int _failures = 0;

  void ensureWifi();

public:
  DataFetcher(const char* ssid, const char* password, const char* url);
  bool connect(unsigned long timeoutMs = 15000);
  bool fetch(UsageData& out);
  int  consecutiveFailures() const;
};
