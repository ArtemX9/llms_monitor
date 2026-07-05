#pragma once

#define TFT_BL_PIN 27
#define BL_CHANNEL 0
#define BL_FREQ    5000
#define BL_RES     8

// Onboard RGB status LED (common anode — LOW turns a channel ON).
#define LED_RED_PIN   22
#define LED_GREEN_PIN 16
#define LED_BLUE_PIN  17

// Proxy discovery
#define PROXY_PORT 3000
#define WIFI_CONNECT_TIMEOUT_MS 8000
#define PROXY_PROBE_TIMEOUT_MS 150
#define PROXY_VALIDATE_TIMEOUT_MS 1000

// Battery level sensing — onboard 100K/100K divider on GPIO34.
// See docs/superpowers/specs/2026-07-05-battery-level-display-design.md
#define BAT_ADC_PIN      34
#define BAT_SAMPLE_COUNT 8
#define BAT_MIN_MV       3300  // maps to 0%
#define BAT_MAX_MV       4200  // maps to 100%
#define BAT_LOW_PCT      15    // battery icon renders red at/below this
