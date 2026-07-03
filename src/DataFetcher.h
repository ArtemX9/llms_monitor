#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include "Types.h"
#include "Config.h"

class DataFetcher {
  const WifiCredential* _networks;
  size_t _networkCount;
  uint16_t _proxyPort;
  IPAddress _proxyIp;
  bool _proxyResolved = false;
  int _failures = 0;
  bool _ledEnabled = true;
  LedSignal _lastRgbSignal = LedSignal::Red;
  void (*_indicatorCallback)(bool) = nullptr;
  void (*_rgbCallback)(LedSignal) = nullptr;

  bool connectToNetwork(const WifiCredential& net, unsigned long timeoutMs);
  bool loadCachedIp(IPAddress& out);
  void saveCachedIp(IPAddress ip);
  void clearCachedIp();
  bool validateProxy(IPAddress ip);
  bool scanForProxy();
  bool resolveProxy();
  void ensureWifi();
  void setIndicator(bool on);
  void setRgb(LedSignal signal);

public:
  DataFetcher(const WifiCredential* networks, size_t networkCount, uint16_t proxyPort);
  bool connect(unsigned long perNetworkTimeoutMs = WIFI_CONNECT_TIMEOUT_MS);
  bool fetch(UsageData& out);
  int  consecutiveFailures() const;
  bool recoverProxy();
  void setLedEnabled(bool enabled);
  void setIndicatorCallback(void (*callback)(bool));
  void setRgbCallback(void (*callback)(LedSignal));
};
