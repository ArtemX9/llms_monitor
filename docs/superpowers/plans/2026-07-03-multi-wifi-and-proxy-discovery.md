# Multi-Network WiFi + Automatic Proxy Discovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the ESP32 firmware try up to 3 pre-defined WiFi networks in priority order, and automatically discover the proxy server's IP on whichever network it lands on (instead of a hardcoded IP), with NVS caching for fast subsequent boots and a rescan-before-restart failure path.

**Architecture:** All new logic lives in `DataFetcher` (WiFi connection loop over a credential array; proxy discovery via NVS-cached IP + subnet scan with a cheap TCP-probe-then-JSON-validate two-phase check). `main.cpp` only changes how it constructs `DataFetcher` and how it reacts to repeated fetch failures. `Renderer` and `TouchRouter` are untouched.

**Tech Stack:** Arduino/ESP32 (arduino-esp32 core), `WiFi.h`, `WiFiClient`, `HTTPClient`, `Preferences.h` (built into the core, no new `lib_deps`), `ArduinoJson` (already a dependency).

## Global Constraints

- Proxy port stays fixed at 3000 (matches `claude_monitor_be/src/config-store.js` default). Known limitation, not handled: if the port is ever changed in the Electron app's Settings, the ESP32 firmware's `PROXY_PORT` constant must be updated and reflashed.
- Subnet scan assumes a `/24` network (per spec) — iterates host octet 1-254 off `WiFi.localIP()`'s first three octets. Does not attempt to generalize to other mask sizes.
- `validateProxy()` must accept both valid response shapes from the real server: `200` with `claude`/`grok` keys, and `503` with an `error` key (the server returns this before its first poll completes — see `claude_monitor_be/src/server-manager.js:15-22`). Anything else is "not the proxy."
- No unit test infrastructure exists in this project (all logic is hardware-coupled). Verification per task is: does it compile (`pio run`), and — for the final task — manual on-hardware checks.
- LEDC/WiFi-reconnect/HTTP-reuse/log-suppression quirks documented in `CLAUDE.md` must still be respected (they aren't touched by this plan, but don't reintroduce `WiFi.reconnect()` or drop `http.setReuse(false)`).

---

## File Structure

- **Modify `src/Types.h`**: add `WifiCredential` struct.
- **Modify `src/Config.h`**: add `PROXY_PORT`, `WIFI_CONNECT_TIMEOUT_MS`, `PROXY_PROBE_TIMEOUT_MS`, `PROXY_VALIDATE_TIMEOUT_MS`.
- **Modify `src/DataFetcher.h`**: constructor takes `(const WifiCredential* networks, size_t networkCount, uint16_t proxyPort)`; new private helpers for connection-per-network, IP caching, validation, scanning, resolution; new public `recoverProxy()`.
- **Modify `src/DataFetcher.cpp`**: implement all of the above.
- **Modify `src/main.cpp`**: declare the WiFi network array (3 entries — 1 real, 2 placeholders the user fills in), drop `proxyUrl`, update `DataFetcher` construction, update the 5-failure branch in `loop()` to call `recoverProxy()` before restarting.

---

### Task 1: WifiCredential struct and config constants

**Files:**
- Modify: `src/Types.h`
- Modify: `src/Config.h`

**Interfaces:**
- Produces: `struct WifiCredential { const char* ssid; const char* password; };` (used by Task 2+ and `main.cpp`).
- Produces: `PROXY_PORT`, `WIFI_CONNECT_TIMEOUT_MS`, `PROXY_PROBE_TIMEOUT_MS`, `PROXY_VALIDATE_TIMEOUT_MS` (used by Task 2, 4, 5).

- [ ] **Step 1: Add `WifiCredential` to `Types.h`**

Add this struct near the top of `src/Types.h`, right after the `#include`:

```cpp
struct WifiCredential {
  const char* ssid;
  const char* password;
};
```

- [ ] **Step 2: Add proxy/WiFi constants to `Config.h`**

Append to `src/Config.h`:

```cpp

// Proxy discovery
#define PROXY_PORT 3000
#define WIFI_CONNECT_TIMEOUT_MS 8000
#define PROXY_PROBE_TIMEOUT_MS 150
#define PROXY_VALIDATE_TIMEOUT_MS 1000
```

- [ ] **Step 3: Compile check**

Run: `pio run`
Expected: `SUCCESS` (these are pure declarations, nothing consumes them yet, so this only checks for syntax errors).

- [ ] **Step 4: Commit**

```bash
git add src/Types.h src/Config.h
git commit -m "Add WifiCredential struct and proxy discovery constants"
```

---

### Task 2: Multi-network WiFi connection in DataFetcher

**Files:**
- Modify: `src/DataFetcher.h`
- Modify: `src/DataFetcher.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `WifiCredential` (Task 1), `WIFI_CONNECT_TIMEOUT_MS` (Task 1).
- Produces: `DataFetcher(const WifiCredential* networks, size_t networkCount, uint16_t proxyPort)` constructor — later tasks (3-6) add more behavior to this same class but don't change this signature. `connect()` and internal `ensureWifi()` now try every network in `_networks` in order instead of a single ssid/password.

- [ ] **Step 1: Rewrite `DataFetcher.h`**

Replace the full contents of `src/DataFetcher.h` with:

```cpp
#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include "Types.h"
#include "Config.h"

class DataFetcher {
  const WifiCredential* _networks;
  size_t _networkCount;
  uint16_t _proxyPort;
  int _failures = 0;
  bool _ledEnabled = true;
  LedSignal _lastRgbSignal = LedSignal::Red;
  void (*_indicatorCallback)(bool) = nullptr;
  void (*_rgbCallback)(LedSignal) = nullptr;

  bool connectToNetwork(const WifiCredential& net, unsigned long timeoutMs);
  void ensureWifi();
  void setIndicator(bool on);
  void setRgb(LedSignal signal);

public:
  DataFetcher(const WifiCredential* networks, size_t networkCount, uint16_t proxyPort);
  bool connect(unsigned long perNetworkTimeoutMs = WIFI_CONNECT_TIMEOUT_MS);
  bool fetch(UsageData& out);
  int  consecutiveFailures() const;
  void setLedEnabled(bool enabled);
  void setIndicatorCallback(void (*callback)(bool));
  void setRgbCallback(void (*callback)(LedSignal));
};
```

Note: `WIFI_CONNECT_TIMEOUT_MS` is used as a default parameter value, so `Config.h` is now included directly by `DataFetcher.h` (added above) rather than relying on callers to include it first.

- [ ] **Step 2: Rewrite `DataFetcher.cpp`'s WiFi connection logic**

Replace the full contents of `src/DataFetcher.cpp` with:

```cpp
#include "DataFetcher.h"
#include "Config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

DataFetcher::DataFetcher(const WifiCredential* networks, size_t networkCount, uint16_t proxyPort)
  : _networks(networks), _networkCount(networkCount), _proxyPort(proxyPort) {}

void DataFetcher::setIndicator(bool on) {
  if (_indicatorCallback) _indicatorCallback(_ledEnabled && on);
}

void DataFetcher::setRgb(LedSignal signal) {
  _lastRgbSignal = signal;
  if (_rgbCallback) _rgbCallback(_ledEnabled ? signal : LedSignal::Off);
}

bool DataFetcher::connectToNetwork(const WifiCredential& net, unsigned long timeoutMs) {
  WiFi.begin(net.ssid, net.password);
  unsigned long t = millis();
  bool blinkOn = false;
  while (WiFi.status() != WL_CONNECTED && millis() - t < timeoutMs) {
    blinkOn = !blinkOn;
    setIndicator(blinkOn);
    setRgb(blinkOn ? LedSignal::BlueBlinkOn : LedSignal::BlueBlinkOff);
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

void DataFetcher::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.disconnect(false);
  for (size_t i = 0; i < _networkCount && WiFi.status() != WL_CONNECTED; i++) {
    connectToNetwork(_networks[i], WIFI_CONNECT_TIMEOUT_MS);
  }
  setIndicator(WiFi.status() == WL_CONNECTED);
  if (WiFi.status() != WL_CONNECTED) setRgb(LedSignal::Red);
}

bool DataFetcher::connect(unsigned long perNetworkTimeoutMs) {
  for (size_t i = 0; i < _networkCount; i++) {
    if (connectToNetwork(_networks[i], perNetworkTimeoutMs)) {
      setIndicator(true);
      return true;
    }
  }
  setIndicator(false);
  setRgb(LedSignal::Red);
  return false;
}

void DataFetcher::setLedEnabled(bool enabled) {
  _ledEnabled = enabled;
  setIndicator(WiFi.status() == WL_CONNECTED);
  if (_rgbCallback) _rgbCallback(enabled ? _lastRgbSignal : LedSignal::Off);
}

void DataFetcher::setIndicatorCallback(void (*callback)(bool)) {
  _indicatorCallback = callback;
}

void DataFetcher::setRgbCallback(void (*callback)(LedSignal)) {
  _rgbCallback = callback;
}

bool DataFetcher::fetch(UsageData& out) {
  ensureWifi();
  if (WiFi.status() != WL_CONNECTED) { _failures++; setRgb(LedSignal::Red); return false; }

  char url[40];
  snprintf(url, sizeof(url), "http://192.168.0.58:%u/", _proxyPort);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(3000);
  http.setReuse(false);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    if (err) {
      Serial.printf("JSON error: %s\n", err.c_str());
    } else {
      out.claudeSession = doc["claude"]["session_pct"];
      out.claudeWeekly  = doc["claude"]["weekly_pct"];
      out.claudeReset   = doc["claude"]["reset_min"];
      out.grokTokens    = doc["grok"]["token_pct"];
      out.grokRequests  = doc["grok"]["request_pct"];
      Serial.printf("Claude: %d%% / %d%%  Grok: %d%%\n",
                    out.claudeSession, out.claudeWeekly, out.grokTokens);
      ok = true;
    }
  } else {
    Serial.printf("HTTP error: %d\n", code);
  }
  http.end();
  if (ok) _failures = 0; else _failures++;
  setRgb(ok ? LedSignal::Green : LedSignal::Red);
  return ok;
}

int DataFetcher::consecutiveFailures() const { return _failures; }
```

This is an intentional intermediate state: `fetch()` still uses a temporary hardcoded IP (`192.168.0.58`) so the class compiles and the WiFi-list behavior can be verified in isolation before Task 3-5 replace it with real discovery.

- [ ] **Step 3: Update `main.cpp` wiring for the new constructor**

In `src/main.cpp`, replace:

```cpp
const char* ssid     = "TP-Link_4400";
const char* password = "Chippo545454A";
const char* proxyUrl = "http://192.168.0.58:3000";

AppState    state;
UsageData   data      = {};
DataFetcher fetcher(ssid, password, proxyUrl);
```

with:

```cpp
const WifiCredential wifiNetworks[] = {
  { "TP-Link_4400", "Chippo545454A" },
};

AppState    state;
UsageData   data      = {};
DataFetcher fetcher(wifiNetworks, sizeof(wifiNetworks) / sizeof(wifiNetworks[0]), PROXY_PORT);
```

(This is a single-entry array for now — Task 7 expands it to the full 3-entry list once discovery is fully wired up, so each intermediate task stays independently buildable and testable.)

- [ ] **Step 4: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 5: Manual hardware check**

Flash and monitor: `pio run -t upload && pio device monitor`
Expected: device connects to `TP-Link_4400` same as before (single network in the list), fetches from the still-hardcoded `192.168.0.58` proxy IP, and behaves identically to pre-change firmware.

- [ ] **Step 6: Commit**

```bash
git add src/DataFetcher.h src/DataFetcher.cpp src/main.cpp
git commit -m "Support connecting to a prioritized list of WiFi networks"
```

---

### Task 3: NVS proxy IP caching

**Files:**
- Modify: `src/DataFetcher.h`
- Modify: `src/DataFetcher.cpp`

**Interfaces:**
- Produces: private `bool loadCachedIp(IPAddress& out)`, `void saveCachedIp(IPAddress ip)`, `void clearCachedIp()` — consumed by Task 5's `resolveProxy()` and Task 6's `recoverProxy()`.

- [ ] **Step 1: Add caching helper declarations to `DataFetcher.h`**

In `src/DataFetcher.h`, add these three private method declarations right after `connectToNetwork`:

```cpp
  bool loadCachedIp(IPAddress& out);
  void saveCachedIp(IPAddress ip);
  void clearCachedIp();
```

- [ ] **Step 2: Implement the caching helpers in `DataFetcher.cpp`**

Add `#include <Preferences.h>` to the top of `src/DataFetcher.cpp` (after the existing includes), then add these three functions right after `connectToNetwork`'s implementation:

```cpp
bool DataFetcher::loadCachedIp(IPAddress& out) {
  Preferences prefs;
  prefs.begin("netcfg", true);
  String ip = prefs.getString("proxyIp", "");
  prefs.end();
  if (ip.length() == 0) return false;
  return out.fromString(ip);
}

void DataFetcher::saveCachedIp(IPAddress ip) {
  Preferences prefs;
  prefs.begin("netcfg", false);
  prefs.putString("proxyIp", ip.toString());
  prefs.end();
}

void DataFetcher::clearCachedIp() {
  Preferences prefs;
  prefs.begin("netcfg", false);
  prefs.remove("proxyIp");
  prefs.end();
}
```

- [ ] **Step 3: Compile check**

Run: `pio run`
Expected: `SUCCESS` (these functions aren't called yet, but must compile standalone).

- [ ] **Step 4: Commit**

```bash
git add src/DataFetcher.h src/DataFetcher.cpp
git commit -m "Add NVS-backed proxy IP caching helpers"
```

---

### Task 4: Proxy validation

**Files:**
- Modify: `src/DataFetcher.h`
- Modify: `src/DataFetcher.cpp`

**Interfaces:**
- Consumes: `PROXY_VALIDATE_TIMEOUT_MS` (Task 1), `_proxyPort` (Task 2).
- Produces: private `bool validateProxy(IPAddress ip)` — consumed by Task 5's `scanForProxy()`/`resolveProxy()` and Task 6's `recoverProxy()`.

- [ ] **Step 1: Add `validateProxy` declaration to `DataFetcher.h`**

Add this private method declaration in `src/DataFetcher.h`, right after `clearCachedIp()`:

```cpp
  bool validateProxy(IPAddress ip);
```

- [ ] **Step 2: Implement `validateProxy` in `DataFetcher.cpp`**

Add this function in `src/DataFetcher.cpp`, right after `clearCachedIp()`'s implementation:

```cpp
bool DataFetcher::validateProxy(IPAddress ip) {
  char url[40];
  snprintf(url, sizeof(url), "http://%s:%u/", ip.toString().c_str(), _proxyPort);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(PROXY_VALIDATE_TIMEOUT_MS);
  http.setReuse(false);
  int code = http.GET();

  bool valid = false;
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getStream()) == DeserializationError::Ok) {
      valid = !doc["claude"].isNull() || !doc["grok"].isNull();
    }
  } else if (code == 503) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getStream()) == DeserializationError::Ok) {
      valid = !doc["error"].isNull();
    }
  }
  http.end();
  return valid;
}
```

- [ ] **Step 3: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 4: Commit**

```bash
git add src/DataFetcher.h src/DataFetcher.cpp
git commit -m "Add proxy response validation (200 claude/grok or 503 error shape)"
```

---

### Task 5: Subnet scan and proxy resolution, wired into fetch()

**Files:**
- Modify: `src/DataFetcher.h`
- Modify: `src/DataFetcher.cpp`

**Interfaces:**
- Consumes: `loadCachedIp`/`saveCachedIp` (Task 3), `validateProxy` (Task 4), `PROXY_PROBE_TIMEOUT_MS` (Task 1).
- Produces: private `bool scanForProxy()`, `bool resolveProxy()`, and new member state `IPAddress _proxyIp; bool _proxyResolved = false;` — consumed by `fetch()` in this task and by Task 6's `recoverProxy()`.

- [ ] **Step 1: Add scan/resolve declarations and state to `DataFetcher.h`**

Add these two private method declarations right after `validateProxy`:

```cpp
  bool scanForProxy();
  bool resolveProxy();
```

Add this member state right after the `_networkCount` / `_proxyPort` members:

```cpp
  IPAddress _proxyIp;
  bool _proxyResolved = false;
```

- [ ] **Step 2: Implement `scanForProxy` and `resolveProxy` in `DataFetcher.cpp`**

Add `#include <WiFiClient.h>` to the includes at the top of `src/DataFetcher.cpp`.

Add these two functions right after `validateProxy`'s implementation:

```cpp
bool DataFetcher::scanForProxy() {
  IPAddress local = WiFi.localIP();
  for (int host = 1; host <= 254; host++) {
    IPAddress candidate(local[0], local[1], local[2], host);
    if (candidate == local) continue;

    WiFiClient probe;
    bool open = probe.connect(candidate, _proxyPort, PROXY_PROBE_TIMEOUT_MS);
    probe.stop();
    if (!open) continue;

    if (validateProxy(candidate)) {
      saveCachedIp(candidate);
      _proxyIp = candidate;
      _proxyResolved = true;
      return true;
    }
  }
  return false;
}

bool DataFetcher::resolveProxy() {
  IPAddress cached;
  if (loadCachedIp(cached) && validateProxy(cached)) {
    _proxyIp = cached;
    _proxyResolved = true;
    return true;
  }
  return scanForProxy();
}
```

- [ ] **Step 3: Wire resolution into `fetch()`, removing the temporary hardcoded IP**

In `src/DataFetcher.cpp`, replace the hardcoded-IP block from Task 2:

```cpp
  char url[40];
  snprintf(url, sizeof(url), "http://192.168.0.58:%u/", _proxyPort);

  HTTPClient http;
  http.begin(url);
```

with:

```cpp
  if (!_proxyResolved && !resolveProxy()) {
    _failures++;
    setRgb(LedSignal::Red);
    return false;
  }

  char url[40];
  snprintf(url, sizeof(url), "http://%s:%u/", _proxyIp.toString().c_str(), _proxyPort);

  HTTPClient http;
  http.begin(url);
```

- [ ] **Step 4: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 5: Manual hardware check**

Flash and monitor: `pio run -t upload && pio device monitor`
Expected on a fresh device (erase flash first with `pio run -t erase` if you want a truly clean NVS): boots, connects to `TP-Link_4400`, has no cached IP, runs the subnet scan (visible as a pause before the first successful fetch), finds the proxy (make sure the Electron app is running on the same network), and logs a successful `Claude: X% / Y%  Grok: Z%` fetch. Power-cycle the device again and confirm the second boot fetches immediately without the scan pause (cached-IP fast path).

- [ ] **Step 6: Commit**

```bash
git add src/DataFetcher.h src/DataFetcher.cpp
git commit -m "Add subnet-scan proxy discovery and wire it into fetch()"
```

---

### Task 6: Rescan-before-restart failure recovery

**Files:**
- Modify: `src/DataFetcher.h`
- Modify: `src/DataFetcher.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `clearCachedIp` (Task 3), `scanForProxy` (Task 5).
- Produces: public `bool recoverProxy()` — consumed by `main.cpp`'s `loop()`.

- [ ] **Step 1: Add `recoverProxy` declaration to `DataFetcher.h`**

Add this to the **public** section of `src/DataFetcher.h`, right after `consecutiveFailures()`:

```cpp
  bool recoverProxy();
```

- [ ] **Step 2: Implement `recoverProxy` in `DataFetcher.cpp`**

Add this function in `src/DataFetcher.cpp`, right after `consecutiveFailures()`'s implementation:

```cpp
bool DataFetcher::recoverProxy() {
  clearCachedIp();
  _proxyResolved = false;
  if (scanForProxy()) {
    _failures = 0;
    return true;
  }
  return false;
}
```

- [ ] **Step 3: Update `main.cpp`'s failure-threshold branch**

In `src/main.cpp`'s `loop()`, replace:

```cpp
  if (millis() - lastFetch > state.fetchInterval) {
    if (fetcher.fetch(data)) {
      renderer.update(state.screen, data, state.needsFullRedraw);
      state.needsFullRedraw = false;
    } else if (fetcher.consecutiveFailures() >= 5) {
      ESP.restart();
    }
    lastFetch = millis();
  }
```

with:

```cpp
  if (millis() - lastFetch > state.fetchInterval) {
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
```

- [ ] **Step 4: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 5: Manual hardware check**

With the device running normally against the proxy, stop the Electron app's server (or quit the app) to force fetch failures. Watch the serial log for 5 consecutive `HTTP error` lines, then confirm a rescan runs (visible as the scan-timing pause) rather than an immediate reboot. Restart the Electron app's server before the rescan completes and confirm the device finds it again and resumes fetching without ever calling `ESP.restart()`. Then repeat with the server left off through the whole rescan and confirm it *does* restart (serial log resets / boot banner reappears) once the rescan also fails.

- [ ] **Step 6: Commit**

```bash
git add src/DataFetcher.h src/DataFetcher.cpp src/main.cpp
git commit -m "Rescan for the proxy before restarting on repeated fetch failures"
```

---

### Task 7: Expand to the full 3-network WiFi list

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `WifiCredential` (Task 1), the array-based `DataFetcher` constructor (Task 2).

- [ ] **Step 1: Expand the network array in `main.cpp`**

Replace:

```cpp
const WifiCredential wifiNetworks[] = {
  { "TP-Link_4400", "Chippo545454A" },
};
```

with:

```cpp
const WifiCredential wifiNetworks[] = {
  { "TP-Link_4400", "Chippo545454A" },
  { "YourSecondNetworkSSID", "YourSecondNetworkPassword" },
  { "YourThirdNetworkSSID", "YourThirdNetworkPassword" },
};
```

**You must edit the second and third entries with real SSID/password values before flashing** — these are placeholder strings, the same way `TP-Link_4400`'s credentials are literal hardcoded values today. If you only have one or two networks to add, delete the unused entry/entries instead of leaving placeholder text in the array (an unreachable placeholder SSID just costs one wasted `WIFI_CONNECT_TIMEOUT_MS` timeout per boot when out of range, but leaving in fake credentials that don't correspond to any real network is pointless).

- [ ] **Step 2: Compile check**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 3: Manual hardware check**

With only your second network's AP powered on (first and third out of range or powered off), flash and monitor: `pio run -t upload && pio device monitor`. Expected: serial log shows the first network's connection attempt timing out after ~8s, then the second network connecting successfully.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "Add second and third WiFi network slots"
```

---

### Task 8: Full manual verification pass

**Files:** none (verification only)

- [ ] **Step 1: Priority-order network selection**

Boot with only network #2 of the list in range. Confirm serial log shows #1 timing out (~8s) before #2 connects. (Covered already in Task 7 Step 3 — re-confirm here as part of the full pass if it wasn't just tested.)

- [ ] **Step 2: Fresh-device discovery**

Erase flash (`pio run -t erase`), reflash, boot on a network with the Electron proxy app running. Confirm the subnet scan runs and finds it, and the fetch succeeds. Reboot again and confirm the second boot uses the fast cached-IP path (no scan pause).

- [ ] **Step 3: Mid-session IP change recovery**

While running normally, force the proxy's apparent IP to change (e.g. toggle the server off/on in a way that gets a new DHCP lease, or simulate by changing which machine runs the Electron app on the same network). Confirm 5 consecutive failures trigger a rescan (not an immediate restart) and the device recovers once the rescan finds the new IP.

- [ ] **Step 4: Full outage fallback**

With the Electron app fully stopped and no proxy reachable anywhere on the subnet, confirm that after 5 failures the rescan also fails to find anything and the device falls back to `ESP.restart()` (serial log shows the boot banner again).

- [ ] **Step 5: All-networks-unreachable boot**

Power on with none of the 1-3 configured networks in range. Confirm `connect()` in `setup()` returns false after trying all networks (~8s × network count) and `renderer.showWifiFailed()` is displayed, matching today's existing failure-path behavior.

No commit for this task — it's a verification pass over work already committed in Tasks 1-7.

---

## Self-Review Notes

- **Spec coverage**: Part 1 (multi-network WiFi) → Tasks 2, 7. Part 2 (discovery + caching) → Tasks 3, 4, 5. Part 3 (failure recovery) → Task 6. Code layout section → matches Tasks 1-7's file list exactly. Testing section → Task 8.
- **Placeholder scan**: Task 7's SSID/password placeholders are intentional and flagged as user-edit-required (credentials can't be known ahead of time), not left as unresolved implementation TBDs — every other code block in this plan is complete, working code.
- **Type consistency**: `WifiCredential` (Task 1) → constructor param in Task 2 → array declared in Task 2/7 all match `{const char* ssid; const char* password;}`. `IPAddress _proxyIp` (Task 5) used consistently in `resolveProxy()`, `scanForProxy()`, `recoverProxy()` (Task 6), and `fetch()`. `_proxyResolved` set in Task 5, reset in Task 6 — consistent.
