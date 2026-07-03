# Claude Monitor — ESP32 TFT Usage Display

Displays Claude Code and Grok API usage stats on a 320×240 (rotated) TFT display over WiFi.

**This branch (`esp32-32e-cyd`) targets a different physical board than `main`:** an
all-in-one "3.2 ESP32-32E 240x320 Resistance Touch" module (Sunton/CYD-family clone;
LCDWIKI SKU E32R32P/E32N32P; mfr Shenzhen Hong Shu Yuan Technology), not the hand-wired
ILI9341 module `main` was built for. This is a hardware-swap branch, not a multi-board
build — `platformio.ini`/`Config.h`/`TouchRouter.cpp` are all specific to this board.

## Hardware

| Thing | Detail |
|---|---|
| MCU | ESP32-WROOM-32E (esp32dev) |
| Display | ST7789P3 240×320, rotation 3 (landscape) |
| Touch | XPT2046, calibrated (see quirks below) |
| Backlight | PWM on GPIO27 via LEDC channel 0 |
| Status | on-screen dot (top-right corner) during WiFi connect/reconnect — no physical LED on this board (GPIO2 is the TFT DC line) |

Pins are passed to TFT_eSPI via `build_flags` in `platformio.ini`, same approach as `main`.

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

### TFT_eSPI's own `User_Setup.h` silently overrides `platformio.ini` build_flags
The registry copy of `bodmer/TFT_eSPI` ships a `User_Setup.h` with unguarded `#define`s
for driver/pins. Since it's included *after* `-D` command-line flags, its `#define`s win
silently (only visible as `"X" redefined` compiler warnings, easy to miss in normal
build output) — the firmware can look correctly configured in `platformio.ini` while
actually still running whatever `User_Setup.h` says. This cost a long debugging session
on this branch: pins, driver, and flash mode all matched the physical board and it still
rendered nothing, because `User_Setup.h` was quietly putting it back on the *old* ILI9341
board's pins.

Fix: `-DUSER_SETUP_LOADED=1` in `build_flags` (already set). This stops
`User_Setup_Select.h` from including `User_Setup.h` at all, so the `-D` flags are the only
source of truth. If TFT_eSPI is ever pulled from the registry again instead of the
vendored copy in `lib/TFT_eSPI/`, keep this flag.

### This board needs the panel manufacturer's ST7789 init sequence, not the stock one
`lib/TFT_eSPI/TFT_Drivers/ST7789_Init.h` is patched with the exact sequence from
LCDWIKI's `ST7789P3_Init.txt` for this SKU (also in their "Replaced files" Arduino demo
package) — TFT_eSPI's generic ST7789 driver table does not bring this panel up (backlight
lights, screen stays black). This is why the whole `TFT_eSPI` library is vendored into
`lib/` on this branch instead of pulled from the registry via `lib_deps`.

### Touch requires real calibration, not TFT_eSPI's default
This physical XPT2046 panel's raw ADC range doesn't match TFT_eSPI's built-in default
calibration. `Renderer::init()` calls `_tft.setTouch(calData)` with values obtained by
running TFT_eSPI's `calibrateTouch()` against the actual hardware
(`{433, 3490, 314, 3448, 5}` for this specific unit — re-run calibration if the touch
panel is ever replaced).

### Touch x is *not* inverted on this board (unlike `main`'s ILI9341 board)
With real calibration applied, touch x already matches true visual x — low x = visual
left, high x = visual right, no mirroring needed. `TouchRouter::poll()` on this branch
uses the raw calibrated x directly against button pixel ranges. (`main`'s branch has an
inverted-x quirk specific to that board's wiring; don't carry that logic over here.)

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

### On-screen WiFi indicator doesn't persist across screen redraws (known limitation)
`Renderer::drawWifiIndicator()` draws a small dot in the top-right corner, driven by a
callback from `DataFetcher` during its WiFi connect/blink loop. Because `drawClaude()`/
`drawGrok()`/`drawSettings()` all `fillScreen()` on a full redraw, the dot gets wiped out
by the very next screen draw after it's set — so on a fast, already-connected boot it's
essentially invisible. It still shows correctly during an actual prolonged WiFi outage
(the blink loop keeps re-drawing it every ~250ms). Accepted as-is: fixing this would mean
tracking last-known WiFi state in `Renderer` and re-drawing it after every full redraw,
which wasn't judged worth the added coupling for a rare-to-see status indicator.

## Data source

Proxy server (local network, currently `http://192.168.0.58:3000` on this branch — see
`main.cpp` for whichever WiFi network/IP is active). Expected JSON shape:
```json
{
  "claude": { "session_pct": 43, "weekly_pct": 9, "reset_min": 240 },
  "grok":   { "token_pct": 0, "request_pct": 12 }
}
```
