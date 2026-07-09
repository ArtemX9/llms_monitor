#pragma once

// Verbose serial logging for the loop-yield / CPU-frequency power tuning
// (boot reset reason + CPU freq, loop iteration rate, touch event timing).
// Off by default so normal runs stay quiet; enable with
// `-DCM_DEBUG_POWER=1` in platformio.ini's build_flags when you need to
// re-verify the power changes on hardware.
#ifndef CM_DEBUG_POWER
#define CM_DEBUG_POWER 0
#endif

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
#define BAT_SAMPLE_COUNT 32     // raised from 8 on 2026-07-05: on-device logging at 120s
                                // fetch interval showed +-4mV reading-to-reading noise at
                                // ~65% SoC, comparable to the real per-interval charge
                                // signal — more averaging shrinks that noise floor.
#define BAT_MIN_MV       3300  // maps to 0%
#define BAT_MAX_MV       4200  // maps to 100%
#define BAT_LOW_PCT      15    // battery icon renders red at/below this

// Retuned 2026-07-05 from on-device diagnostic logging (see git history /
// conversation): at ~65% SoC on a 120s fetch interval, real consecutive-
// reading deltas while charging were only 1-4mV — the original 15mV
// threshold never triggered because every reading looked "flat" to it.
//
// KNOWN LIMITATION, confirmed on hardware, not fixed further by design
// choice (see conversation history): esp_adc_cal_raw_to_voltage() returns
// 1mV resolution, doubled for the divider, so readings are quantized to
// 2mV steps. At slow charge rates (observed: <1mV/tick average, well under
// one quantization step) most individual ticks read delta=0 with only
// occasional non-consecutive +2/+4mV jumps once drift crosses a
// quantization boundary. Because BAT_CHG_RISE_STREAK requires *consecutive*
// rising ticks, no threshold value in this scheme can reliably detect
// charging in that slow-rate regime — the jumps are real but never
// back-to-back. A fix would require comparing against a reading several
// ticks back instead of just the previous one (a bigger design change),
// which was considered and deliberately not pursued. Practical effect: the
// charging bolt may fail to appear during slow/near-full charging phases
// even though charging is genuinely happening — accepted as-is.
#define BAT_CHG_RISE_MV     4   // mV delta beyond which a reading counts as "rising"/"falling" (else "flat")
#define BAT_CHG_RISE_STREAK 3   // consecutive rising reads required to enter "charging"
#define BAT_CHG_FALL_STREAK 2   // consecutive falling reads required to exit "charging"
