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
