# Claude Monitor — ESP32 TFT Usage Display

Displays Claude Code and Grok API usage stats on a 320×240/240×320 (user-selectable rotation) TFT display over WiFi.

**This branch (`esp32-32e-cyd`) targets a different physical board than `main`:** an
all-in-one "3.2 ESP32-32E 240x320 Resistance Touch" module (Sunton/CYD-family clone;
LCDWIKI SKU E32R32P/E32N32P; mfr Shenzhen Hong Shu Yuan Technology), not the hand-wired
ILI9341 module `main` was built for. This is a hardware-swap branch, not a multi-board
build — `platformio.ini`/`Config.h`/`TouchRouter.cpp` are all specific to this board.

## Hardware

| Thing | Detail |
|---|---|
| MCU | ESP32-WROOM-32E (esp32dev) |
| Display | ST7789P3 240×320, all 4 TFT_eSPI rotations supported (landscape 320×240 and portrait 240×320); user-selectable in Settings, persisted in NVS, defaults to rotation 3 (landscape) |
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
  SpriteData.h    — pixel-art mascot frame data (pure data, no logic)
  AnimatedSprite.h/cpp — mascot animation state machine + redraw
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
calibration. The original rotation-3 baseline (`{433, 3490, 314, 3448, 5}`, obtained by
running TFT_eSPI's `calibrateTouch()` against the actual hardware) is now the **seed**
`Renderer::applyTouchCalibration()` derives all four rotations' 5-word calData from in
code — swapping axis roles and invert bits per rotation rather than re-running hardware
calibration for each one. The portrait (rotation 0/2) invert bits are the derived-but-
hardware-verified part of that table. Each rotation can also be overridden permanently: a
long-press (≥800ms) on the Settings rotate icon runs TFT_eSPI's `calibrateTouch()` for the
*current* rotation and persists the result to NVS (`netcfg` keys `cal0`…`cal3`, 10-byte
blobs — see `Renderer::recalibrate()`, `NvsConfig::loadCal`/`saveCal`), which then takes
priority over the derived default for that rotation on every future boot. Re-run
recalibration on any rotation if the touch panel is ever replaced.

### Touch zones are geometry-aware, not just x-orientation-aware
With real calibration applied, touch coordinates already match true visual coordinates in
every rotation — no mirroring needed. `TouchRouter::poll()` now branches on
`_tft.width()` (≥320 = landscape 320×240, else portrait 240×320) and uses a separate set
of button pixel ranges for each orientation, since the two layouts place buttons at
different coordinates. (`main`'s branch still has an inverted-x quirk specific to that
board's wiring; don't carry that logic over here — this board's raw touch x has never
needed inversion in either orientation.)

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

### This panel has no double buffering — any separate "clear, then redraw" step is visible
`AnimatedSprite::draw()` (the animated header mascot on the Claude screen) originally
cleared a large fixed-size rect to black, then redrew the sprite on top, every single
redraw (~4-10Hz). That is genuinely two sequential, visible operations on real hardware
with no frame buffer to hide the gap — it showed up as a deterministic, reproducible black
flash/wedge cutting across the sprite on *every* redraw, not an intermittent glitch. Two
plausible-sounding "SPI timing race" fixes (batching TFT_eSPI calls into one
`startWrite()`/`endWrite()` transaction; consolidating dozens of per-cell `fillRect()`
calls into one `pushImage()`) both compiled fine and changed nothing on hardware — because
neither addressed the real cause. The actual fix: only clear the exact region that's about
to change (the sprite's previous footprint minus whatever overlaps its new position), never
a larger area "just in case." A few px of unavoidable overlap can still very rarely tear if
the panel's own refresh lands mid-write, but that's now small enough to be a non-issue.

Lesson for any future TFT_eSPI drawing code on this board: **never fill an area black and
redraw over it as two separate steps if you can instead compute the exact diff and only
touch what actually changed.** If something *does* look like intermittent tearing, check
first whether it's actually this (a deterministic, always-on redraw artifact) before
chasing SPI-transaction timing — the visible symptom looks similar but the fix is entirely
different.

### `TFT_eSPI::pushImage()` needs pre-swapped color bytes; `fillRect()` doesn't
`fillRect()`'s color path (`pushBlock`) always byte-swaps internally, but `pushImage()`'s
raw pixel path (`pushPixels()` with the default `_swapBytes=false`, see
`Processors/TFT_eSPI_ESP32.c`) writes a buffer's bytes as-is with no swap. `color565()`
returns host (little-endian) byte order. If you build a pixel buffer with `color565()`
values and push it via `pushImage()`, the colors come out wrong (this happened while
building `AnimatedSprite`). Either swap each color once when writing it into the buffer
(`(c >> 8) | (c << 8)`, what `AnimatedSprite::draw()` does) or call
`tft.setSwapBytes(true)` first — the former keeps `pushPixels()`'s faster raw burst-write
path instead of falling back to a per-pixel loop.

## Architecture

Three concerns, strictly separated:

- **DataFetcher** — owns WiFi state and HTTP. Constructor takes a list of `WifiCredential` networks, tried in priority order (`WIFI_CONNECT_TIMEOUT_MS` ≈ 8 s per network). `fetch()` auto-reconnects internally and resolves the proxy server dynamically (see "Data source" below) rather than hitting a fixed URL. Tracks consecutive failures; caller restarts after 5 (see "Proxy recovery" below).
- **Renderer** — pure drawing. Receives all needed values as parameters. Exposes `tft()` ref so TouchRouter can call `getTouch()` without owning the hardware. `setRotation()` applies the rotation to the TFT, re-applies touch calibration for that rotation (`applyTouchCalibration()`), and updates the sprite header width to match. Each screen (Claude, Grok, Settings) has a separate landscape and portrait draw/update path, selected internally by `portrait()` (true for rotation 0/2).
- **TouchRouter** — calls `getTouch()`, interprets zones by current screen, returns an `Event`. Never touches display or mutates state.
- **AppState** (struct in `Types.h`) — `screen`, `brightness`, `fetchInterval`, `needsFullRedraw`. Owned exclusively by `loop()`.
- **AnimatedSprite** — owns the Claude-screen header mascot's animation state (character choice, frame, patrol position). `Renderer::tickSprite()` calls `tick(millis())` then `draw()` only if something changed; driven from `loop()` independently of the WiFi-fetch timer, only while on the Claude screen. Character pixel data lives in `SpriteData.h`, separate from the state machine.

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

### Proxy recovery on repeated fetch failure
After 5 consecutive `fetch()` failures, `loop()` calls `DataFetcher::recoverProxy()`.
`recoverProxy()` first checks `WiFi.status() != WL_CONNECTED` and bails out immediately if
so — without touching the cache or scanning — because a WiFi outage isn't a proxy problem
and a ~30-40 s subnet scan against `0.0.0.0` would just freeze the UI for nothing. Only
when WiFi is confirmed still connected does it clear the cached proxy IP and rescan. If
`recoverProxy()` returns `false` either way (WiFi down, or WiFi up but no proxy found),
`main.cpp` falls back to `ESP.restart()`.

## Data source

The proxy IP is not hardcoded — it's discovered automatically. On boot, `DataFetcher`
checks a cached IP (ESP32 NVS, namespace `netcfg`, key `proxyIp`) by validating it's still
the real proxy; if that fails or nothing is cached, it scans the local `/24` subnet on port
`PROXY_PORT` (3000, fixed) for a host that responds with the expected JSON shape below.
`main.cpp` declares a `wifiNetworks[]` array of `{ssid, password}` pairs instead of a
single hardcoded network/IP.

The same `netcfg` namespace also stores display orientation state: `rot`, a `uint8`
holding the current rotation (0–3), and `cal0`…`cal3`, 10-byte blobs holding the
per-rotation touch calibration override — see the touch calibration quirk above.

Expected JSON shape:
```json
{
  "claude": { "session_pct": 43, "weekly_pct": 9, "reset_min": 240 },
  "grok":   { "token_pct": 0, "request_pct": 12 }
}
```
