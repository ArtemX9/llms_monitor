# Claude Monitor

An ESP32-based desk display that shows real-time Claude Code and Grok API usage stats on a 320×240 ILI9341 TFT screen.

## What it does

- **Claude screen** — session usage %, weekly usage %, and minutes until reset
- **Grok screen** — usage % and time until the billing period resets
- **Settings screen** — backlight brightness, refresh interval (30 / 60 / 120 s), reboot button
- Swipe left/right anywhere on the screen to navigate between the three screens
- Fetches data from a local proxy server over WiFi; auto-reconnects and restarts after 5 consecutive failures

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32 |
| Display | ILI9341 320×240 SPI TFT |
| Touch | XPT2046 resistive |
| Backlight | PWM-controlled via GPIO14 |

### Wiring

| Signal | GPIO |
|---|---|
| TFT CS | 15 |
| TFT DC | 21 |
| TFT RST | 4 |
| TFT MOSI | 23 |
| TFT SCLK | 18 |
| TFT MISO | 19 |
| Touch CS | 5 |
| Backlight | 14 |
| Status LED | 2 |

## Software

Built with [PlatformIO](https://platformio.org/) and the Arduino framework.

**Dependencies** (declared in `platformio.ini`, installed automatically):
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) — display + touch driver
- [ArduinoJson v7](https://arduinojson.org/) — JSON parsing

## Build & flash

```bash
pio run -t upload
pio device monitor       # 115200 baud
```

Prebuilt firmware binaries (for flashing with `esptool` instead of building from source) are published on the [Releases page](../../releases) for each tagged version.

## Data source

The device fetches from a local proxy server (default `http://192.168.2.131:3000`). The proxy should return:

```json
{
  "claude": { "session_pct": 43, "weekly_pct": 9, "reset_min": 240 },
  "grok":   { "usage_pct": 12, "reset_min": 44640 }
}
```

Note: on flat-rate Grok plans (e.g. X Premium), `usage_pct` reads `0` regardless of actual usage — the upstream billing endpoint only meters pay-as-you-go/on-demand usage, not flat-rate quota.

WiFi credentials and proxy URL are set at the top of `src/main.cpp`.

## Case (CAD)

```
cad/
  case.scad         — OpenSCAD source for the 3D-printed enclosure
  stl/
    case_body.stl    — exported body, ready to slice
    case_lid.stl      — exported lid, ready to slice
```

Edit `case.scad` in [OpenSCAD](https://openscad.org/) and re-export the STLs after any change.

## Project structure

```
src/
  Config.h          — pin and PWM channel defines
  Types.h           — UsageData, AppState, Event
  DataFetcher.h/cpp — WiFi management and HTTP fetch
  Renderer.h/cpp    — TFT drawing (pure, stateless)
  TouchRouter.h/cpp — touch input, returns Event enum
  main.cpp          — credentials, globals, setup(), loop()
```
