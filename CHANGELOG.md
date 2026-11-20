# Changelog

All notable changes to **Garadget Local Firmware** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[3.1.0]: https://github.com/Krazy998/garadget-firmware/compare/v3.0.0...v3.1.0
[3.0.0]: https://github.com/Krazy998/garadget-firmware/releases/tag/v3.0.0
