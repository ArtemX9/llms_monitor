# Claude Monitor

An ESP32-based desk display that shows real-time Claude Code and Grok API usage stats on a 320×240 ILI9341 TFT screen.

## What it does

- **Claude screen** — session usage %, weekly usage %, and minutes until reset
- **Grok screen** — token usage % and request usage %
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

## Data source

The device fetches from a local proxy server (default `http://192.168.2.131:3000`). The proxy should return:

```json
{
  "claude": { "session_pct": 43, "weekly_pct": 9, "reset_min": 240 },
  "grok":   { "token_pct": 0, "request_pct": 12 }
}
```

WiFi credentials and proxy URL are set at the top of `src/main.cpp`.

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
