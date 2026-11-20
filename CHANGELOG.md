# Changelog

All notable changes to **Garadget Local Firmware** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [3.2.2] - 2026-11-21

### Security
- **Server-Side SoftAP Password Enforcement**: SoftAP `POST /save-config` now enforces server-side that a Web Admin Password is configured (rolling back if omitted on first-time setup).
- **Fail-Closed Auth Token Check**: Updated `verifyAuthToken()` to fail closed (`false`) if `adminPass` is empty.
- **Hardware-RNG SNTP Nonce**: Upgraded SNTP Originate Timestamp nonces to use Particle P1's hardware-backed `random()` + `micros()` and added `gNtpUdp.flush()` to drain oversized datagrams cleanly.

### Fixed
- **Thread-Safe SoftAP Exit & Activity Tracking**: Deferred `WiFi.listen(false)` from `/exit-setup` to the main loop (350 ms delay) so HTTP responses flush cleanly before SoftAP tears down, and switched SoftAP activity tracking to cross-thread `volatile` flags.
- **SoftAP Static IP Pre-Validation & Recovery UX**: Validated Static IP/Subnet/Gateway fields in the SoftAP UI before `/clear-wifi` runs so typos don't clear saved Wi-Fi credentials, and exposed the **Set / Reset Admin Password Only (Keep Current Wi-Fi)** button whenever saved Wi-Fi exists.
- **SoftAP → LAN Config Snapshot Sync**: Resynced IP/MQTT change-detection snapshots and timezone upon exiting SoftAP so subsequent unrelated LAN saves don't trigger spurious reboots.
- **HTTP Server & Login Polish**: Added `408 Request Timeout` for truncated non-OTA request bodies, clamped `snprintf` header writes safely, cleared the login password field on submit, and added an `Invalid admin password` toast on `401`.

---

## [3.2.1] - 2026-11-21

### Security
- **Mandatory Web Admin Password Setup**: When no Web Admin Password is configured (first boot or after factory reset), all `/api/*` endpoints return `428 Precondition Required` and the LAN Web UI prompts the user to create an admin password (minimum 4 characters) via `POST /api/setup-password` before exposing door controls, settings, or OTA updates.
- **SoftAP Password Setup & Recovery**: Updated the SoftAP portal (`192.168.0.1`) to require setting a Web Admin Password alongside Wi-Fi on first-time setup, and added support for resetting a forgotten Web Admin Password directly in Wi-Fi Setup Mode (including a **Reset Admin Password Only** option via `/exit-setup` that keeps existing Wi-Fi credentials).
- **SNTP Anti-Spoofing**: Verified incoming UDP/123 replies against the expected NTP server IP, source port `123`, server mode (`4`), non-zero stratum (rejecting Kiss-o'-Death packets), and a 64-bit transmit timestamp nonce echoed in Originate Timestamp. Also added direct IPv4 literal parsing for air-gapped LAN NTP servers.
- **Per-IP Auth Rate Limiting**: Upgraded `AuthRateLimiter` to track failed attempts per client IP (4 slots) so failed requests from one LAN host cannot lock out the legitimate admin.
- **SoftAP Auto-Exit Timeout**: Automatically exits Wi-Fi Setup Mode after 10 minutes of inactivity when saved Wi-Fi credentials already exist (activity resets the timer).
- **CI & Supply-Chain Hardening**: Scoped GitHub Actions `contents: write` exclusively to the tag-triggered `release` job and added SHA-256 checksum verification for prebuilt Device OS 3.3.1 binaries in `scripts/setup-toolchain.sh`.

### Fixed
- **Atomic Config Validation & Rollback**: Added backup + `validate()` + rollback to `applyPipeConfig()` (preventing invalid `ipm=1` pipe commands from triggering a factory reset on reboot) and made `applyJsonConfig()` fail atomically instead of silently dropping invalid fields when another field succeeds.
- **JSON & HTTP Buffer Sizing**: Increased `toJson()` output buffers to `1152` bytes (`ha_mqtt.cpp`, `softap_portal.cpp`, `serial_cli.cpp`), `MqttClient::kBufferSize` to `1280` bytes, and `WebServer` request buffer to `2560` bytes with proper `413 Payload Too Large` and `431 Request Header Fields Too Large` responses.
- **OTA Flash Alignment & Header Length Validation**: Enforced exact chunk reads during streaming OTA so STM32F205 flash writes stay 4-byte aligned, verified `(endAddr - startAddr) + 4 == totalLength` in `OtaValidator::validateHeader()`, and temporarily restored LED visibility during OTA when Stealth Mode is active.
- **`clear-wifi` Static IP Cleanup**: Reset `ipMode = 0` (DHCP) in EEPROM when clearing Wi-Fi credentials from LAN, SoftAP, or Serial CLI so stale Static IP configs cannot make the device unreachable on a new subnet.
- **Reliability & UI Fixes**:
  - Guarded `checkModeButton()` against repeated EEPROM/DCT writes during a 10s factory-reset hold.
  - Applied timezone immediately on warm reboot (`setup()`) and ran DST checks whenever `Time.isValid()`.
  - Tuned MQTT keepalive `PINGREQ` to `75%` of `keepAliveSec` (refreshed on outgoing packets) with unconditional `gMqttTcp.stop()`, and debounced ±5% reflection-only MQTT publishes to at most once per 5 seconds.
  - Fixed Web UI `saveAdminPass()` to wait for HTTP `200 OK` before updating `sessionStorage`, fixed the MQTT topic preview regex, validated `POST /api/system` action names (`400` on unknown actions), updated running open-timeout timers immediately when `aot` changes, preserved SoftAP MQTT toggle state, and drove relay/laser latches `LOW` before setting `OUTPUT` mode.

---

## [3.2.0] - 2026-11-20

### Added
- **RGB LED Dimming & Stealth Mode**: Control status LED brightness (`0–100%`, `ledb`) and **Stealth Mode** (`leds`, completely turns off the status LED for dark garages) from the Web UI (**System** tab) or Home Assistant. LED brightness automatically restores during SoftAP setup mode so the blinking setup indicator stays visible.
- **Home Assistant Auto-Discovered LED Entities**: Automatically discovers a **Stealth Mode** `switch` (`garadget/<topic>/set-stealth`) and **LED Brightness** `number` slider (`garadget/<topic>/set-led-brightness`) under the Garadget P1 device, applying changes instantaneously without reconnecting MQTT.
- **Config Backup & Restore**: One-click **Export Config (.json)** (passwords safely omitted) and **Import Config (.json)** in the LAN Web UI (**System** tab).
- **Seamless EEPROM Schema Migration (v3 → v4)**: Preserves all existing v3.x settings (including Static IP & DNS) on OTA upgrade.

---

## [3.1.0] - 2026-11-20

### Added
- **Static IP & Custom DNS Support**: Configure DHCP or Static IPv4 (`sip`, `smask`, `sgw`, `sdns`) in both the LAN Web UI (**System** tab) and SoftAP setup portal (`192.168.0.1`), with atomic validation rollback and display of the active DNS server.
- **Seamless EEPROM Schema Migration (v2 → v3)**: Upgrading from `v3.0.x` preserves all existing MQTT, sensor, alert, and timezone settings automatically.
- **Forget Saved Wi-Fi Networks**: Added a **"Forget previously saved Wi-Fi networks"** checkbox (checked by default) in SoftAP setup (`POST /clear-wifi`), a **Forget Wi-Fi & Setup** button in the LAN System tab, and a `clear-wifi` USB Serial CLI command so old P1 credential slots don't hijack reconnects.
- **Security Hardening**:
  - Cross-origin CSRF protection (`Origin` vs `Host` validation + non-simple `Content-Type` enforcement) on all state-changing `/api/*` endpoints.
  - Security response headers (`X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, and `Content-Security-Policy`).
  - Constant-time `X-Auth-Token` verification, header-boundary-strict HTTP parsing, and brute-force rate limiting (`429 Too Many Requests` for 10s after 5 consecutive failures).
  - HTML escaping (`esc()`) in both LAN and SoftAP Web UIs to prevent DOM XSS (including crafted nearby Wi-Fi SSIDs).
  - SoftAP `/save-config` heap allocation cap (`<= 1024` bytes) and ACL blocking `apass` changes or `defaults` resets from MQTT and SoftAP.
  - Redacted `set-config` payloads in USB serial logs and tighter OTA stream timeouts (2s header, 5s chunk idle, 30s total cap).

### Fixed
- **SoftAP Wi-Fi Provisioning (`configure-ap`)**:
  - Left-padded RSA-1024 ciphertext hex strings to 256 characters (128 bytes) to prevent leading-zero truncation bugs in Particle's `decrypt()`.
  - Switched SoftAP `POST` requests to `Content-Type: multipart/form-data` for full compatibility with Particle's WICED HTTP server and mobile captive portals.
  - Added dynamic ASN.1 DER parsing for the device `public-key` response and surfaced exact error/return codes in the SoftAP UI.

---

## [3.0.0] - 2026-11-20

### Added
- **Complete Cloud-Free C++17 Rewrite** targeting **Particle P1** (`PLATFORM=p1`, Platform ID 8) on **Particle Device OS 3.3.1** (latest supported OS for Gen 2 hardware).
- **Local LAN Web Dashboard (`http://<device-ip>/`)**: Offline, gzip-embedded responsive UI (dark/light theme) with live animated garage door state, Open/Stop/Close controls, live laser reflection meter with threshold marker, tabbed settings, optional Admin Password, and REST JSON API.
- **Local Web OTA Firmware Updates (`POST /api/ota`)**: Wireless `.bin` upload via the Web UI (**System** tab) or `make ota HOST=<ip>` with streaming Particle P1 header, flash bounds, and IEEE 802.3 CRC32 validation before committing.
- **Home Assistant MQTT Auto-Discovery & MQTT 3.1.1**: Zero-heap, non-blocking MQTT client supporting IPv4 + DNS hostnames, backwards-compatible `garadget/<name>/{status,command,config,set-config,alert,LWT}` topics, HA birth message refresh, and 8 auto-discovered entities (Cover, Reflection, Brightness, Wi-Fi Signal, Time in State, Last Alert, Sensor Threshold number, Reboot button) with `configuration_url`.
- **Local SNTP Client (UDP/123)** & **Fixed DST Engine**: Local clock synchronization without Particle Cloud plus full Northern and Southern Hemisphere DST support.
- **Runtime-Selectable Sensor Mode**: Switch between Laser Optical (`D2`/`A0`), Dry-Contact Switch NO (`D0`), and Dry-Contact Switch NC (`D0`) without recompiling.
- **Offline Build System & CI**: Self-contained `make toolchain` / `make firmware` / `make test` workflow, USB Serial CLI (`115200` baud), native host C++17 unit test suite, and GitHub Actions CI + automated Release publishing.

### Removed
- Removed all Particle Cloud and Garadget Cloud code (`PRODUCT_ID`, `Particle.connect()`, cloud variables/functions/events, `spark/` subscriptions).

### Fixed (vs. Legacy Garadget v1.26)
- Eliminated `sprintf` buffer overflows, MQTT heap leaks on config reload, infinite blocking TCP socket reads, unsigned integer underflow in laser reflection math, and unverified EEPROM config loads (now CRC32-protected with 60s `ApplicationWatchdog` and boot-safe `PIN_RELAY` LOW).

[3.2.2]: https://github.com/Krazy998/garadget-firmware/compare/v3.2.1...v3.2.2
[3.2.1]: https://github.com/Krazy998/garadget-firmware/compare/v3.2.0...v3.2.1
[3.2.0]: https://github.com/Krazy998/garadget-firmware/compare/v3.1.0...v3.2.0
[3.1.0]: https://github.com/Krazy998/garadget-firmware/compare/v3.0.0...v3.1.0
[3.0.0]: https://github.com/Krazy998/garadget-firmware/releases/tag/v3.0.0
