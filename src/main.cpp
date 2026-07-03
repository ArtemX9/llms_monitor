#include <Arduino.h>
#include "esp_log.h"
#include "Config.h"
#include "Types.h"
#include "DataFetcher.h"
#include "Renderer.h"
#include "TouchRouter.h"

const WifiCredential wifiNetworks[] = {
  { "TP-Link_4400", "Chippo545454A" },
};

AppState    state;
UsageData   data      = {};
DataFetcher fetcher(wifiNetworks, sizeof(wifiNetworks) / sizeof(wifiNetworks[0]), PROXY_PORT);
Renderer    renderer;
TouchRouter touch(renderer.tft());

unsigned long lastFetch = 0;

void wifiIndicator(bool on) {
  renderer.drawWifiIndicator(on);
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

  renderer.init(state.brightness);
  renderer.showConnecting();

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
  if (state.screen == 0) renderer.tickSprite();

  switch (touch.poll(state.screen)) {
    case Event::NavForward:
      state.screen = (state.screen + 1) % 3;
      renderer.switchTo(state.screen, data, state.brightness, state.fetchInterval, state.ledEnabled);
      state.needsFullRedraw = false;
      break;
    case Event::NavBack:
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
      renderer.showRebooting();
      delay(300);
      ESP.restart();
      break;
    default: break;
  }

  if (millis() - lastFetch > state.fetchInterval) {
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
