#include <Arduino.h>
#include <WiFi.h>
#include "esp_log.h"
#include "Config.h"
#include "Types.h"
#include "DataFetcher.h"
#include "Renderer.h"
#include "NvsConfig.h"
#include "TouchRouter.h"
#include "Battery.h"

const WifiCredential wifiNetworks[] = {
  { "TP-Link_4400", "Chippo545454A" },
  { "at300", "m4ruAvub" },
};

AppState    state;
UsageData   data      = {};
DataFetcher fetcher(wifiNetworks, sizeof(wifiNetworks) / sizeof(wifiNetworks[0]), PROXY_PORT);
Renderer    renderer;
Battery     battery;
TouchRouter touch(renderer.tft());

unsigned long lastFetch = 0;

void wifiIndicator(bool on) {
  renderer.drawWifiIndicator(on);
}

// Diagnostic: logs the ESP-IDF disconnect reason code so a dropped
// connection can be told apart from AP-side vs. RF-side vs. auth causes.
// See esp_wifi_types.h for the full reason-code list.
void logWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[wifi] disconnected, reason=%d\n", info.wifi_sta_disconnected.reason);
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.printf("[wifi] connected to %.*s\n", info.wifi_sta_connected.ssid_len,
                    (const char*)info.wifi_sta_connected.ssid);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[wifi] got IP, RSSI=%d dBm\n", WiFi.RSSI());
      break;
    default: break;
  }
}

// Common anode RGB LED — LOW turns a channel ON, HIGH turns it off.
void rgbLed(LedSignal signal) {
  bool r = false, g = false, b = false;
  switch (signal) {
    case LedSignal::BlueBlinkOn: b = true; break;
    case LedSignal::Red:         r = true; break;
    case LedSignal::Green:       g = true; break;
    case LedSignal::BlueBlinkOff:
    case LedSignal::Off:         break;
  }
  digitalWrite(LED_RED_PIN,   r ? LOW : HIGH);
  digitalWrite(LED_GREEN_PIN, g ? LOW : HIGH);
  digitalWrite(LED_BLUE_PIN,  b ? LOW : HIGH);
}

void setup() {
  Serial.begin(115200);
  esp_log_level_set("*", ESP_LOG_NONE);

  pinMode(LED_RED_PIN,   OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN,  OUTPUT);
  digitalWrite(LED_RED_PIN,   HIGH);
  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_BLUE_PIN,  HIGH);

  fetcher.setIndicatorCallback(wifiIndicator);
  fetcher.setRgbCallback(rgbLed);
  WiFi.onEvent(logWifiEvent);
  // Default modem-sleep power save causes missed beacons -> reason=200
  // disconnect storms on this hardware; disable it before the first begin().
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  state.rotation = NvsConfig::loadRotation(3);
  renderer.init(state.rotation, state.brightness);
  renderer.showConnecting();

  battery.init();
  int pct = battery.readPercent();
  renderer.setBattery(pct, battery.isCharging());

  if (fetcher.connect()) {
    bool ok = false;
    for (int i = 0; i < 3 && !ok; i++) {
      if (i > 0) delay(2000);
      ok = fetcher.fetch(data);
    }
    if (ok) {
      renderer.update(state.screen, data, /*fullRedraw=*/true);
      state.needsFullRedraw = false;
      lastFetch = millis();
    } else {
      renderer.showServerError();
      state.needsFullRedraw = true;
      lastFetch = 0;
    }
  } else {
    renderer.showWifiFailed();
    state.needsFullRedraw = true;
    lastFetch = millis() - 30000;
  }
}

void loop() {
  fetcher.tick();
  if (state.screen == 0) renderer.tickSprite();

  switch (touch.poll(state.screen)) {
    case Event::NavForward:
      if (state.screen == 2) state.rebootArmedAt = 0;
      state.screen = (state.screen + 1) % 3;
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
    case Event::NavBack:
      if (state.screen == 2) state.rebootArmedAt = 0;
      state.screen = (state.screen + 2) % 3;
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
    case Event::BrightnessUp:
      state.brightness = min(255, (int)state.brightness + 20);
      ledcWrite(BL_CHANNEL, state.brightness);
      renderer.updateBrightnessBar(state.brightness);
      break;
    case Event::BrightnessDown:
      if (state.brightness > 20) state.brightness -= 20;
      ledcWrite(BL_CHANNEL, state.brightness);
      renderer.updateBrightnessBar(state.brightness);
      break;
    case Event::Interval30s:
      state.fetchInterval = 30000;
      renderer.updateIntervalButtons(state.fetchInterval);
      break;
    case Event::Interval60s:
      state.fetchInterval = 60000;
      renderer.updateIntervalButtons(state.fetchInterval);
      break;
    case Event::Interval120s:
      state.fetchInterval = 120000;
      renderer.updateIntervalButtons(state.fetchInterval);
      break;
    case Event::ToggleLed:
      state.ledEnabled = !state.ledEnabled;
      fetcher.setLedEnabled(state.ledEnabled);
      renderer.updateLedToggle(state.ledEnabled);
      break;
    case Event::Reboot:
      if (state.rebootArmedAt != 0 && millis() - state.rebootArmedAt < 2000) {
        renderer.showRebooting();
        delay(300);
        ESP.restart();
      } else {
        state.rebootArmedAt = millis();
        renderer.updateRebootIcon(true);
      }
      break;
    case Event::CycleRotation: {
      state.rebootArmedAt = 0; // switchTo redraws the reboot icon disarmed; keep state in sync
      static const uint8_t order[4] = {3, 0, 1, 2};
      int idx = 0;
      for (int i = 0; i < 4; i++) if (order[i] == state.rotation) { idx = i; break; }
      state.rotation = order[(idx + 1) % 4];
      renderer.setRotation(state.rotation);
      NvsConfig::saveRotation(state.rotation);
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
    }
    case Event::Recalibrate:
      state.rebootArmedAt = 0; // recalibrate + switchTo redraw the reboot icon disarmed
      renderer.recalibrate(state.rotation);
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
    default: break;
  }

  if (state.screen == 2 && state.rebootArmedAt != 0 && millis() - state.rebootArmedAt > 2000) {
    state.rebootArmedAt = 0;
    renderer.updateRebootIcon(false);
  }

  if (millis() - lastFetch > state.fetchInterval) {
    int pct = battery.readPercent();
    renderer.setBattery(pct, battery.isCharging());
    if (fetcher.fetch(data)) {
      renderer.update(state.screen, data, state.needsFullRedraw);
      state.needsFullRedraw = false;
    } else if (fetcher.consecutiveFailures() >= 5) {
      if (!fetcher.recoverProxy()) {
        ESP.restart();
      }
    }
    lastFetch = millis();
  }
}
