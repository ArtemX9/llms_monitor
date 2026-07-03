# Multi-Network WiFi + Automatic Proxy Discovery

## Goal

Today the device is hardcoded to one WiFi network (`ssid`/`password` in
`main.cpp`) and one proxy server IP (`proxyUrl`, also in `main.cpp`). Moving
the device between networks (e.g. home vs. another location) currently
requires re-flashing both. This design lets the firmware:

1. Try up to 2-3 pre-defined WiFi networks in priority order.
2. Automatically find the proxy server's IP on whichever network it lands
   on, instead of relying on a hardcoded IP — without any change to the
   proxy server itself (no mDNS, no server-visible hostname).

## Non-goals

- No runtime configuration UI (touchscreen WiFi setup, captive portal,
  etc.) — the network list is still compiled in, just as an array instead
  of a single pair.
- No changes to the proxy server (Electron/Express app). Discovery works
  entirely from the ESP32 side by probing the subnet.
- No support for more than 3 networks or arbitrary/unbounded network lists.

## Part 1 — Multi-network WiFi

A new `WifiCredential { const char* ssid; const char* password; }` struct
(in `Types.h`) backs a small fixed-size array of up to 3 entries, declared
in `main.cpp` the same way `ssid`/`password` are declared today.

`DataFetcher` takes this array (pointer + count) instead of a single
ssid/password pair. Connection logic — both initial `connect()` and
runtime `ensureWifi()` — tries each entry in priority order:

- For each network, `WiFi.begin(ssid, password)` and wait up to ~8s for
  `WL_CONNECTED`, using the existing blink-indicator/RGB callback behavior
  per attempt.
- On timeout, move to the next network in the list.
- If all networks fail, behavior matches today's failure path (`connect()`
  returns false → `showWifiFailed()`; `ensureWifi()` leaves WiFi
  disconnected → `fetch()` counts a failure).

## Part 2 — Proxy discovery

Replaces the fixed `proxyUrl` with runtime discovery + NVS caching, added
to `DataFetcher` (or a small helper it owns). The proxy server's port
(3000) and the expected JSON response shape (`claude`/`grok` keys) stay
fixed, matching today's contract with the server.

**Storage**: `Preferences` (ESP32 NVS), namespace `"netcfg"`, key
`"proxyIp"` — stores the last-known-good proxy IP as a string.

**Validation**: `validateProxy(IPAddress ip)` does a GET to
`http://<ip>:3000/` with a short timeout and confirms the response parses
as JSON with the expected `claude`/`grok` keys — not just "something
answered on that port."

**Resolution flow** (`resolveProxy()`), run once after WiFi connects:

1. Load cached IP from NVS. If present, `validateProxy()` it directly. If
   it validates, use it — this is the fast path for the common case
   (nothing changed since last boot).
2. If there's no cached IP, or it fails validation, run
   `scanSubnet()`:
   - Derive the local `/24` range from `WiFi.localIP()` /
     `WiFi.subnetMask()`.
   - For each candidate host (skipping the device's own IP), first do a
     cheap raw `WiFiClient::connect(ip, 3000, ~150ms)` probe to filter out
     hosts with nothing listening on the port.
   - Only for hosts that accept that TCP connection, run the full
     `validateProxy()` GET+JSON check.
   - Stop at the first host that validates; save its IP to NVS.
   - If no host validates after the full sweep, discovery fails for this
     boot (see Part 3 for what happens next).
3. Worst case (nothing cached, proxy not found quickly) the scan can take
   up to ~30-40s across a full 254-host range. This is the rare path;
   normal boots hit the cached-IP fast path in step 1.

`DataFetcher::fetch()` builds its URL from the resolved IP
(`http://<ip>:3000/`) instead of a static string.

## Part 3 — Failure recovery

Today, 5 consecutive fetch failures triggers `ESP.restart()`
(`main.cpp`). With discovery in place, this becomes a two-step fallback:

1. On the 5th consecutive failure, clear the cached proxy IP and re-run
   `scanSubnet()` (WiFi is assumed still connected at this point — a
   dropped connection is handled separately by `ensureWifi()` inside
   `fetch()`, which retries the WiFi network list per Part 1 before this
   is reached).
2. If the rescan finds a working host, save it and keep running normally
   — no restart.
3. If the rescan also finds nothing, fall back to `ESP.restart()` as
   today.

This covers the common real-world cause of the proxy becoming unreachable
mid-session: the router reassigning the host's DHCP lease to a new IP.

## Code layout

Stays within the existing architecture split — no new files needed:

- **`Types.h`**: add `WifiCredential` struct.
- **`Config.h`**: add `NUM_WIFI_NETWORKS` (or similar) if needed for
  array sizing; proxy port stays a constant here or in `DataFetcher.h`.
- **`DataFetcher.h`/`.cpp`**: constructor takes `(WifiCredential* networks,
  size_t count, uint16_t proxyPort)` instead of `(ssid, password, url)`.
  New private helpers: `scanForProxy()`, `validateProxy(IPAddress)`,
  `loadCachedIp()`, `saveCachedIp(IPAddress)`. `fetch()` gains the
  discovery/failure-recovery integration from Part 3.
- **`main.cpp`**: declares the `WifiCredential` array (2-3 entries)
  instead of single `ssid`/`password` consts; drops `proxyUrl`.
- **`Renderer`/`TouchRouter`**: unchanged.

## Testing

No unit test infrastructure exists for this embedded project (per
existing architecture — all logic is hardware-coupled). Verification is
manual, on real hardware:

- Boot with only network #2 of the list in range → confirm it skips #1
  after timeout and connects to #2.
- Fresh device (no cached IP) on a network with the proxy running →
  confirm subnet scan finds it and subsequent boots use the fast
  cached-IP path.
- Stop the proxy server / change its IP (e.g. restart with a new DHCP
  lease) mid-session → confirm 5 failures trigger a rescan that finds the
  new IP without a full device restart.
- Proxy server down entirely → confirm scan completes (doesn't hang) and
  falls back to `ESP.restart()`.
