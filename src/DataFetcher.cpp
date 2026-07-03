#include "DataFetcher.h"
#include "Config.h"
#include <WiFi.h>
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

void DataFetcher::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.disconnect(false);
  for (size_t i = 0; i < _networkCount && WiFi.status() != WL_CONNECTED; i++) {
    connectToNetwork(_networks[i], WIFI_CONNECT_TIMEOUT_MS);
  }
  setIndicator(WiFi.status() == WL_CONNECTED);
  if (WiFi.status() != WL_CONNECTED) setRgb(LedSignal::Red);
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
  ensureWifi();
  if (WiFi.status() != WL_CONNECTED) { _failures++; setRgb(LedSignal::Red); return false; }

  char url[40];
  snprintf(url, sizeof(url), "http://192.168.0.58:%u/", _proxyPort);

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
      out.grokTokens    = doc["grok"]["token_pct"];
      out.grokRequests  = doc["grok"]["request_pct"];
      Serial.printf("Claude: %d%% / %d%%  Grok: %d%%\n",
                    out.claudeSession, out.claudeWeekly, out.grokTokens);
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
