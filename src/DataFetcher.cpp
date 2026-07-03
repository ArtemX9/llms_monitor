#include "DataFetcher.h"
#include "Config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

DataFetcher::DataFetcher(const char* ssid, const char* password, const char* url)
  : _ssid(ssid), _password(password), _url(url) {}

void DataFetcher::setIndicator(bool on) {
  if (_indicatorCallback) _indicatorCallback(_ledEnabled && on);
}

void DataFetcher::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.disconnect(false);
  WiFi.begin(_ssid, _password);
  unsigned long t = millis();
  bool blinkOn = false;
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) {
    blinkOn = !blinkOn;
    setIndicator(blinkOn);
    delay(250);
  }
  setIndicator(WiFi.status() == WL_CONNECTED);
}

bool DataFetcher::connect(unsigned long timeoutMs) {
  WiFi.begin(_ssid, _password);
  unsigned long t = millis();
  bool blinkOn = false;
  while (WiFi.status() != WL_CONNECTED && millis() - t < timeoutMs) {
    blinkOn = !blinkOn;
    setIndicator(blinkOn);
    delay(250);
  }
  setIndicator(WiFi.status() == WL_CONNECTED);
  return WiFi.status() == WL_CONNECTED;
}

void DataFetcher::setLedEnabled(bool enabled) {
  _ledEnabled = enabled;
  setIndicator(WiFi.status() == WL_CONNECTED);
}

void DataFetcher::setIndicatorCallback(void (*callback)(bool)) {
  _indicatorCallback = callback;
}

bool DataFetcher::fetch(UsageData& out) {
  ensureWifi();
  if (WiFi.status() != WL_CONNECTED) { _failures++; return false; }

  HTTPClient http;
  http.begin(_url);
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
  return ok;
}

int DataFetcher::consecutiveFailures() const { return _failures; }
