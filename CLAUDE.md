# Claude Monitor — ESP32 TFT Usage Display

Displays Claude Code and Grok API usage stats on an ILI9341 320×240 display over WiFi.

## Hardware

| Thing | Detail |
|---|---|
| MCU | ESP32 (esp32dev) |
| Display | ILI9341 320×240, rotation 3 (landscape) |
| Touch | XPT2046 |
| Backlight | PWM on GPIO14 via LEDC channel 0 |
| Status LED | GPIO2 (onboard, blinks during WiFi connect) |

Pins are passed to TFT_eSPI via `build_flags` in `platformio.ini` — no `User_Setup.h` needed.

## Build & flash

```
pio run -t upload
pio device monitor
```

## Project structure

```
src/
  Config.h        — pin/channel #defines
  Types.h         — UsageData, AppState, Event
  DataFetcher.h/cpp — WiFi reconnect + HTTP fetch
  Renderer.h/cpp  — all TFT drawing (pure, no input/state)
  TouchRouter.h/cpp — touch polling, returns Event enum
  main.cpp        — credentials, globals, setup(), loop()
```

## Critical hardware quirks

### Touch x-axis is mirrored in rotation 3
`TFT_eSPI::getTouch()` returns raw x that is inverted relative to visual pixels:
- Low touch x → visual RIGHT side of screen
- High touch x → visual LEFT side of screen
- Threshold: `x < 160` = visual right, `x >= 160` = visual left

`TouchRouter::poll()` uses `319 - x` to convert touch x to visual x before comparing against button positions (see interval buttons in settings). Navigation and brightness zones use the raw inverted x directly with explicit thresholds.

### LEDC API — ESP32 Arduino core 2.x
Use the **3-call** API. Do NOT use the single-call `ledcAttach()` from core 3.x.
```cpp
ledcSetup(channel, freq, resolution);
ledcAttachPin(pin, channel);
ledcWrite(channel, value);
```

### WiFi reconnect
`WiFi.reconnect()` is unreliable on core 2.x. Always use:
```cpp
WiFi.disconnect(false);
WiFi.begin(ssid, password);
```

### HTTP / TCP socket exhaustion
`http.setReuse(false)` is mandatory on every request. ArduinoJson's streaming parse via `http.getStream()` stops reading after the closing `}`, leaving bytes in the TCP buffer. Without `setReuse(false)`, sockets accumulate in CLOSE_WAIT and the device stops fetching after ~1–1.5 hours.

### ESP-IDF log noise
WiFi/TCP stack logs bypass Arduino Serial and print garbage unless suppressed:
```cpp
esp_log_level_set("*", ESP_LOG_NONE);
```

## Architecture

Three concerns, strictly separated:

- **DataFetcher** — owns WiFi state and HTTP. `connect()` for boot (15 s timeout), `fetch()` auto-reconnects internally. Tracks consecutive failures; caller restarts after 5.
- **Renderer** — pure drawing. Receives all needed values as parameters. Exposes `tft()` ref so TouchRouter can call `getTouch()` without owning the hardware.
- **TouchRouter** — calls `getTouch()`, interprets zones by current screen, returns an `Event`. Never touches display or mutates state.
- **AppState** (struct in `Types.h`) — `screen`, `brightness`, `fetchInterval`, `needsFullRedraw`. Owned exclusively by `loop()`.

Global init order in `main.cpp` matters: `renderer` must be declared before `touch` because `TouchRouter touch(renderer.tft())` captures a reference to `renderer._tft`.

## Data source

Proxy server at `http://192.168.2.131:3000` (local network). Expected JSON shape:
```json
{
  "claude": { "session_pct": 43, "weekly_pct": 9, "reset_min": 240 },
  "grok":   { "token_pct": 0, "request_pct": 12 }
}
```
