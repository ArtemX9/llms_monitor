#include <Arduino.h>
#include "esp_log.h"
#include "Config.h"
#include "Types.h"
#include "DataFetcher.h"
#include "Renderer.h"
#include "TouchRouter.h"

const char* ssid     = "at300";
const char* password = "m4ruAvub";
const char* proxyUrl = "http://192.168.2.131:3000";

AppState    state;
UsageData   data      = {};
DataFetcher fetcher(ssid, password, proxyUrl);
Renderer    renderer;
TouchRouter touch(renderer.tft());

unsigned long lastFetch = 0;

void setup() {
  Serial.begin(115200);
  esp_log_level_set("*", ESP_LOG_NONE);
  pinMode(LED_PIN, OUTPUT);

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
      ESP.restart();
    }
    lastFetch = millis();
  }
}
