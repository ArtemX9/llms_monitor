#include <Arduino.h>
#include <WiFi.h>
#include "esp_log.h"
#include "esp_system.h"
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

#if CM_DEBUG_POWER
// Diagnostic: names the boot cause, so a watchdog reset caused by the
// loop() yield or CPU-frequency change would be visible in the serial log
// instead of looking like a silent power-cycle.
const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}
#endif // CM_DEBUG_POWER

void setup() {
  Serial.begin(115200);
  esp_log_level_set("*", ESP_LOG_NONE);
#if CM_DEBUG_POWER
  Serial.printf("[boot] reset reason=%s\n", resetReasonName(esp_reset_reason()));
#endif

  // This app is I/O-bound (SPI, WiFi, timers), not compute-bound, so 240MHz
  // is unused headroom that costs battery. WiFi requires >=80MHz; SPI/touch
  // clocks come from the fixed 80MHz APB bus and are unaffected by this.
  setCpuFrequencyMhz(80);
#if CM_DEBUG_POWER
  Serial.printf("[boot] cpu freq=%u MHz\n", getCpuFrequencyMhz());
#endif

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

#if CM_DEBUG_POWER
// Diagnostic: prints loop iterations/sec every 5s so the effect of the
// delay(10) yield and CPU-frequency change can be seen directly, and to
// confirm the loop never stalls.
void logLoopRate() {
  static unsigned long lastLogMs = 0;
  static unsigned long iterations = 0;
  iterations++;
  unsigned long now = millis();
  if (now - lastLogMs >= 5000) {
    Serial.printf("[loop] %lu iterations / %lus (%.1f Hz)\n",
                  iterations, (now - lastLogMs) / 1000, iterations * 1000.0f / (now - lastLogMs));
    iterations = 0;
    lastLogMs = now;
  }
}
#endif // CM_DEBUG_POWER

void loop() {
#if CM_DEBUG_POWER
  logLoopRate();
#endif
  fetcher.tick();
  if (state.screen == 0) renderer.tickSprite();

  Event touchEvent = touch.poll(state.screen);
#if CM_DEBUG_POWER
  if (touchEvent != Event::None) {
    // Diagnostic: confirms touch presses still register promptly with the
    // delay(10) yield in place below.
    Serial.printf("[touch] event=%d at t=%lu\n", (int)touchEvent, millis());
  }
#endif

  switch (touchEvent) {
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

  // Yield to the FreeRTOS idle task so the CPU can idle (WFI) between
  // iterations instead of spinning at full clock forever — see battery
  // review, 2026-07-05. 10ms keeps touch feeling instant (~100Hz polling)
  // while giving the idle task a chance to run.
  delay(10);
}
