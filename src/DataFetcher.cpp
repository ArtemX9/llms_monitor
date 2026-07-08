#include "DataFetcher.h"
#include "Config.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

DataFetcher::DataFetcher(const WifiCredential* networks, size_t networkCount, uint16_t proxyPort)
  : _networks(networks), _networkCount(networkCount), _proxyPort(proxyPort) {}

void DataFetcher::setIndicator(bool on) {
  if (_indicatorCallback) _indicatorCallback(_ledEnabled && on);
}

void DataFetcher::setRgb(LedSignal signal) {
  _lastRgbSignal = signal;
  if (_rgbCallback) _rgbCallback(_ledEnabled ? signal : LedSignal::Off);
}

bool DataFetcher::connectToNetwork(const WifiCredential& net, unsigned long timeoutMs) {
  WiFi.begin(net.ssid, net.password);
  unsigned long t = millis();
  bool blinkOn = false;
  while (WiFi.status() != WL_CONNECTED && millis() - t < timeoutMs) {
    blinkOn = !blinkOn;
    setIndicator(blinkOn);
    setRgb(blinkOn ? LedSignal::BlueBlinkOn : LedSignal::BlueBlinkOff);
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool DataFetcher::loadCachedIp(IPAddress& out) {
  Preferences prefs;
  prefs.begin("netcfg", true);
  String ip = prefs.getString("proxyIp", "");
  prefs.end();
  if (ip.length() == 0) return false;
  return out.fromString(ip);
}

void DataFetcher::saveCachedIp(IPAddress ip) {
  Preferences prefs;
  prefs.begin("netcfg", false);
  prefs.putString("proxyIp", ip.toString());
  prefs.end();
}

void DataFetcher::clearCachedIp() {
  Preferences prefs;
  prefs.begin("netcfg", false);
  prefs.remove("proxyIp");
  prefs.end();
}

bool DataFetcher::validateProxy(IPAddress ip) {
  char url[40];
  snprintf(url, sizeof(url), "http://%s:%u/", ip.toString().c_str(), _proxyPort);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(PROXY_VALIDATE_TIMEOUT_MS);
  http.setReuse(false);
  int code = http.GET();

  bool valid = false;
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getStream()) == DeserializationError::Ok) {
      valid = !doc["claude"].isNull() || !doc["grok"].isNull();
    }
  } else if (code == 503) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getStream()) == DeserializationError::Ok) {
      valid = !doc["error"].isNull();
    }
  }
  http.end();
  return valid;
}

bool DataFetcher::scanForProxy() {
  IPAddress local = WiFi.localIP();
  for (int host = 1; host <= 254; host++) {
    IPAddress candidate(local[0], local[1], local[2], host);
    if (candidate == local) continue;

    WiFiClient probe;
    bool open = probe.connect(candidate, _proxyPort, PROXY_PROBE_TIMEOUT_MS);
    probe.stop();
    if (!open) continue;

    if (validateProxy(candidate)) {
      saveCachedIp(candidate);
      _proxyIp = candidate;
      _proxyResolved = true;
      return true;
    }
  }
  return false;
}

bool DataFetcher::resolveProxy() {
  IPAddress cached;
  if (loadCachedIp(cached) && validateProxy(cached)) {
    _proxyIp = cached;
    _proxyResolved = true;
    return true;
  }
  return scanForProxy();
}

void DataFetcher::tick() {
  if (WiFi.status() == WL_CONNECTED) {
    if (_connecting) {
      _connecting = false;
      setIndicator(true);
    }
    return;
  }

  unsigned long now = millis();
  if (!_connecting) {
    _connecting = true;
    _networkIndex = 0;
    _attemptStartMs = now;
    WiFi.disconnect(false);
    WiFi.begin(_networks[0].ssid, _networks[0].password);
  } else if (now - _attemptStartMs >= WIFI_CONNECT_TIMEOUT_MS) {
    _networkIndex++;
    if (_networkIndex >= _networkCount) {
      // Exhausted every network this cycle; stop and let the next tick()
      // (still disconnected) restart from network 0.
      _connecting = false;
      setIndicator(false);
      setRgb(LedSignal::Red);
      return;
    }
    _attemptStartMs = now;
    WiFi.begin(_networks[_networkIndex].ssid, _networks[_networkIndex].password);
  }

  bool blinkOn = ((now / 250) % 2) == 0;
  setIndicator(blinkOn);
  setRgb(blinkOn ? LedSignal::BlueBlinkOn : LedSignal::BlueBlinkOff);
}

bool DataFetcher::connect(unsigned long perNetworkTimeoutMs) {
  for (size_t i = 0; i < _networkCount; i++) {
    if (connectToNetwork(_networks[i], perNetworkTimeoutMs)) {
      setIndicator(true);
      return true;
    }
  }
  setIndicator(false);
  setRgb(LedSignal::Red);
  return false;
}

void DataFetcher::setLedEnabled(bool enabled) {
  _ledEnabled = enabled;
  setIndicator(WiFi.status() == WL_CONNECTED);
  if (_rgbCallback) _rgbCallback(enabled ? _lastRgbSignal : LedSignal::Off);
}

void DataFetcher::setIndicatorCallback(void (*callback)(bool)) {
  _indicatorCallback = callback;
}

void DataFetcher::setRgbCallback(void (*callback)(LedSignal)) {
  _rgbCallback = callback;
}

bool DataFetcher::fetch(UsageData& out) {
  if (WiFi.status() != WL_CONNECTED) { _failures++; setRgb(LedSignal::Red); return false; }

  if (!_proxyResolved && !resolveProxy()) {
    _failures++;
    setRgb(LedSignal::Red);
    return false;
  }

  char url[40];
  snprintf(url, sizeof(url), "http://%s:%u/", _proxyIp.toString().c_str(), _proxyPort);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(3000);
  http.setReuse(false);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    if (err) {
      Serial.printf("JSON error: %s\n", err.c_str());
    } else {
      out.claudeSession = doc["claude"]["session_pct"];
      out.claudeWeekly  = doc["claude"]["weekly_pct"];
      out.claudeReset   = doc["claude"]["reset_min"];
      out.grokUsage     = doc["grok"]["usage_pct"];
      out.grokReset     = doc["grok"]["reset_min"];
      Serial.printf("Claude: %d%% / %d%%  Grok: %d%%\n",
                    out.claudeSession, out.claudeWeekly, out.grokUsage);
      ok = true;
    }
  } else {
    Serial.printf("HTTP error: %d\n", code);
  }
  http.end();
  if (ok) _failures = 0; else _failures++;
  setRgb(ok ? LedSignal::Green : LedSignal::Red);
  return ok;
}

int DataFetcher::consecutiveFailures() const { return _failures; }

bool DataFetcher::recoverProxy() {
  if (WiFi.status() != WL_CONNECTED) return false;
  clearCachedIp();
  _proxyResolved = false;
  if (scanForProxy()) {
    _failures = 0;
    return true;
  }
  return false;
}
