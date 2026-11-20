# Garadget Local Firmware (v3.0.0)

A complete, modern, **100% local and cloud-free** C++17 rewrite of the firmware for the **Garadget** garage door controller (**Particle P1** hardware — STM32F205RGY6 + BCM43362 Wi-Fi), targeting **Particle Device OS 3.3.1** (the latest and most modern Device OS supported on Particle P1).

---

## Highlights & Improvements Over Original v1.26

- **Zero Cloud Lock-In**: Completely removes Particle Cloud (`Particle.connect()`, `PRODUCT_ID`, `Particle.publish/variable/function`, `spark/`). Runs in `SYSTEM_MODE(MANUAL)` + `SYSTEM_THREAD(ENABLED)` so Wi-Fi/network operations never block door safety logic.
- **Most Modern Supported OS**: Built natively for **Particle Device OS 3.3.1** (`PLATFORM=p1`, Platform ID 8 — Device OS 4.x+ dropped Gen 2 STM32 hardware).
- **Local Web OTA Firmware Updates**: Upload new `.bin` builds directly in the Web UI (**System** tab) or via `make ota HOST=<ip>` (`POST /api/ota`). Streams safely to the P1's dedicated OTA staging slot and validates Particle P1 header, Platform ID, and full IEEE 802.3 CRC32 before rebooting.
- **Modern LAN Management Web UI (`http://<device-ip>/`)**:
  - Zero external CDN dependencies (gzip-embedded in flash, ~7.5 KB).
  - Live animated garage door state, Open / Stop / Close controls, live laser reflection meter with visual threshold marker, ambient brightness, Wi-Fi RSSI, and recent alerts.
  - Full configuration UI (Door & Sensor, MQTT & Home Assistant, Alerts & Time, System & Optional LAN Password), dark/light theme, and REST JSON API.
- **Native Home Assistant Auto-Discovery + MQTT 3.1.1**:
  - Automatic MQTT Discovery for **Garage Door Cover** (5 states: `open`, `closed`, `opening`, `closing`, `stopped`), **Laser Reflection %**, **Ambient Brightness %**, **Wi-Fi Signal dBm**, **Time in State**, **Last Alert**, **Sensor Threshold** (`number`), and **Reboot** (`button`), plus direct `configuration_url` link to the Web UI and automatic refresh on Home Assistant birth (`homeassistant/status` = `online`).
  - Supports both **IPv4 addresses and DNS hostnames** (e.g. `mqtt.lan` or `192.168.1.10`).
  - Backwards-compatible with original `garadget/<topic>/{status,command,config,set-config,LWT}` topics plus `garadget/<topic>/alert`.
- **Full Feature Parity + Hardware & Safety Fixes**:
  - Retains optical laser reflection sensing (`D2`/`A0`), dry-contact switch support (`D0` — now runtime-selectable between Laser, Switch NO, and Switch NC), multi-click relay state machine (`D3`), open timeout alerts (`aot`), night-time alerts (`ans`/`ane`), event alert bitmap (`aev`), and SoftAP Wi-Fi provisioning (`192.168.0.1`).
  - **Local SNTP Client (UDP/123)**: Syncs time directly over the network without Particle Cloud so night-time alerts and DST transitions work offline from Particle.
  - **Fixes legacy bugs**: Southern Hemisphere DST support, signed-safe laser reflection math (no unsigned underflow in bright sunlight), zero heap leaks in MQTT, non-blocking incremental MQTT parser (no infinite socket read lockups), CRC32-verified EEPROM config, proper RFC 8259 JSON escaping, `ApplicationWatchdog` (60s auto-recovery), boot-safe relay LOW initialization, and redacted secrets in config output.

---

## Hardware Pinout (Original Garadget P1)

| Pin | Function |
| --- | --- |
| `D2` | Laser emitter control (pulsed HIGH for 1 ms during optical scan) |
| `A0` | Phototransistor analog input (12-bit ADC, inverted brightness) |
| `D3` | Relay driver (HIGH = button pressed; forced LOW on boot) |
| `D0` | Optional dry-contact reed/limit switch (`INPUT_PULLUP` when Switch NO/NC mode selected) |
| `MODE` button | Hold 3s = enter SoftAP Wi-Fi Setup (`192.168.0.1`); Hold 10s = Factory Reset |

---

## Building Locally (Offline, No Particle Cloud Required)

### Prerequisites
- Linux with `make`, `git`, `curl`, `python3`, `xxd`, `g++` (for unit tests), and `dfu-util` (for USB flashing).

### 1. Download Local Toolchain (One-Time)
Downloads official **Particle Device OS v3.3.1**, prebuilt P1 OS 3.3.1 system images, and **GNU Arm Embedded Toolchain 10.2.1** into `.toolchain/` (gitignored):

```bash
make toolchain
```

### 2. Run Unit Tests (Host C++17)
Runs native unit tests covering the door/relay state machine, laser math safety, rollover-safe timers, Northern & Southern Hemisphere DST, CRC32 config persistence, alert engine, JSON utilities, and Home Assistant discovery payloads:

```bash
make test
```

### 3. Build P1 Firmware
Embeds the gzipped Web UI + SoftAP portal and compiles `target/garadget.bin` (Device OS 3.3.1, Platform 8 / P1):

```bash
make firmware
```

Output binary: `target/garadget.bin` (~86 KB, comfortably inside the P1's 128 KB user slot).

---

## Flashing to Your Garadget Device

### First-Time Flash Over USB (`particle flash --local`)
1. Connect Garadget via a micro-USB data cable.
2. Flash the Device OS 3.3.1 bootloader (required if upgrading from OS < 2.0 — put device in Listening Mode by holding `M` for 3s until blinking dark blue):
   ```bash
   particle flash --local .toolchain/device-os-bins/3.3.1/p1-bootloader@3.3.1+lto.bin
   ```
3. Flash Device OS 3.3.1 system parts and the new firmware:
   ```bash
   particle flash --local .toolchain/device-os-bins/3.3.1/p1-system-part1@3.3.1.bin
   particle flash --local .toolchain/device-os-bins/3.3.1/p1-system-part2@3.3.1.bin
   particle flash --local target/garadget.bin
   ```

### Subsequent Wireless Updates (Local Web OTA — No USB Needed!)
- **From your browser**: Open `http://<garadget-ip>/` → **System** tab → select `target/garadget.bin` → click **Upload & Flash Firmware**.
- **From your terminal**:
  ```bash
  make ota HOST=192.168.1.50
  # Or if you configured a Web UI admin password:
  make ota HOST=192.168.1.50 PASS=yourpassword
  ```

---

## First-Time Setup

1. If no Wi-Fi credentials are stored (or after holding the **`M`** button for 3 seconds), Garadget enters **SoftAP Setup Mode** (blinking dark blue LED) and creates a Wi-Fi hotspot (`Photon-XXXX` / `Garadget-XXXX`).
2. Connect your phone or computer to that hotspot and open **`http://192.168.0.1/`**.
3. Select your Wi-Fi network, enter your Wi-Fi password, optionally set your Device Name & MQTT broker, and click **Save & Connect**.
4. Once connected to your router, open **`http://<garadget-ip>/`** from any browser on your LAN to manage the door, tune laser reflection threshold live, configure MQTT/Home Assistant, and set timezone/alerts.

---

## MQTT & Home Assistant Reference

Assuming Device Name `Garage` (topic slug `Garage`):

| Topic | Direction | Description |
| --- | --- | --- |
| `garadget/Garage/LWT` | Publish (retained) | `Online` / `Offline` |
| `garadget/Garage/status` | Publish (retained) | JSON: `{"status":"closed","time":"5m","sensor":84,"bright":40,"signal":-58,"ip":"192.168.1.50"}` |
| `garadget/Garage/config` | Publish (retained) | Safe JSON configuration (passwords redacted) |
| `garadget/Garage/alert` | Publish | JSON: `{"name":"Garage","type":"timeout","data":"20m"}` |
| `garadget/Garage/command` | Subscribe | `open`, `close`, `stop`, `toggle`, `get-status`, `get-config`, `reboot`, `refresh-discovery` |
| `garadget/Garage/set-config` | Subscribe | JSON object or legacy pipe-delimited `key=val\|key=val` |
| `garadget/Garage/set-threshold` | Subscribe | Numeric `1`–`80` (used by Home Assistant threshold entity) |
| `homeassistant/status` | Subscribe | Listens for `online` birth message to republish discovery |

When **Home Assistant Auto-Discovery** is enabled (default), the device automatically appears under **Settings → Devices & Services → MQTT** as **Garadget P1** with a Cover entity, diagnostic sensors, threshold control, reboot button, and a direct **Visit** link to the local Web UI.

---

## Local REST API (`http://<device-ip>`)

- `GET /` — Modern gzipped Web UI
- `GET /api/status` — Live door, sensor, Wi-Fi, MQTT, NTP, memory, and alert history JSON
- `GET /api/config` — Current configuration JSON (passwords masked)
- `POST /api/config` — Update configuration fields (JSON body)
- `POST /api/command` — Execute door command: `{"command":"open|close|stop|toggle"}`
- `POST /api/system` — Execute action: `{"action":"scan|sync-time|ha-rediscover|wifi-setup|reboot|factory-reset"}`
- `POST /api/ota` — Stream raw `garadget.bin` firmware binary (`Content-Type: application/octet-stream`) for validated local OTA flashing

If an Admin Password (`apass`) is configured, include header `X-Auth-Token: <password>` on `/api/*` requests.

---

## USB Serial CLI (115200 baud)

Connect over USB serial for local diagnostics or configuration:
`help` · `status` · `config` · `set key=val|k=v` · `open` · `close` · `stop` · `toggle` · `scan` · `wifi-setup` · `reboot` · `factory-reset`

---

## Credits & License

- **Original Hardware & Firmware**: Based on the [Garadget](https://github.com/Garadget/firmware) project by Denis Grisak. This is an independent, unofficial, cloud-free rewrite.
- **SoftAP RSA Client**: Uses Tom Wu's BSD-licensed `jsbn` RSA library (`web/softap/rsa-utils/`) for Particle P1 SoftAP credential encryption.
- **License**: GNU General Public License v3.0 (see [LICENSE](LICENSE)).

