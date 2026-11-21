# Security Policy

## Supported Versions

Security fixes are released as new minor/patch versions on the `3.x` branch. Because updates can be installed wirelessly via Local Web OTA, only the latest release is actively maintained.

| Version | Supported |
| --- | --- |
| `3.3.x` (latest) | :white_check_mark: Supported |
| `3.0.x` – `3.2.x` | :x: Please upgrade to `3.3.x` |
| `< 3.0` (legacy Particle Cloud firmware) | :x: Not supported by this fork |

---

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues, discussions, or pull requests.**

Instead, use GitHub's built-in **Private Vulnerability Reporting**:

1. Go to [**Report a vulnerability**](https://github.com/Krazy998/garadget-firmware/security/advisories/new) (under the repository's **Security → Advisories** tab).
2. Include as much detail as possible:
   - Affected firmware version (e.g., `v3.3.0`) and Device OS version (`3.3.1`)
   - Component affected (LAN Web Server, SoftAP portal, MQTT bridge, SNTP client, OTA handler, etc.)
   - Clear reproduction steps or proof-of-concept request/payload
   - Expected impact and any suggested fix
3. Please allow reasonable time (best-effort volunteer response, typically within **7 days**) to investigate and coordinate a fix before public disclosure.

---

## Scope & Threat Model

This firmware runs on resource-constrained **Particle P1** (STM32F205, 128 KB user flash) hardware and is designed strictly for **trusted local networks (LAN / IoT VLAN)**.

### In Scope
- Authentication, CSRF, or per-IP rate-limiting bypasses on `/api/*`
- Memory safety bugs (buffer overflows, integer underflows/overflows, crashes, or hangs)
- Escaping MQTT `set-config` operational scope (`ConfigScope::Operational`) into privileged network, broker, NTP, or password settings
- Bypassing OTA header/bounds/length/CRC32 checks or causing unsafe relay behavior during network/OTA activity
- SNTP spoofing or cross-thread race conditions between SoftAP and the main control loop
- XSS or injection vulnerabilities in the embedded LAN or SoftAP Web UIs

### Known Hardware & Design Boundaries (Out of Scope)
- **Plaintext LAN HTTP (port 80) and MQTT (port 1883)**: Particle P1 lacks the RAM to run full TLS stacks concurrently alongside WICED Wi-Fi, SoftAP, HTTP, and MQTT. **Never expose this device directly to the public internet or port-forward port 80.**
- **Physical Access**: Anyone with physical access to the Garadget can hold the `M` button for 3 seconds to enter SoftAP setup/password reset, hold for 10 seconds to factory-reset, or connect via micro-USB (Serial CLI / DFU flashing).
- **Admin Password = Full Code Execution**: Authenticated users with the Web Admin Password can upload and flash any valid Particle P1 binary via **Local Web OTA** (`POST /api/ota`). OTA validates Particle P1 platform ID, flash slot bounds, binary length, and IEEE 802.3 CRC32 against corruption, not cryptographic signatures.
- **Active SoftAP Setup Mode**: While explicitly in Wi-Fi Setup Mode (`Photon-XXXX` / `192.168.0.1`), the local provisioning portal is intentionally unauthenticated for initial setup and password recovery (and auto-exits after 10 minutes of inactivity when Wi-Fi credentials and an admin password are already configured).

---

## Recommended Deployment Practices

- Use a unique, strong **Web Admin Password** (8+ characters; treat it as root access to the controller).
- Place the Garadget on a segmented **local LAN or IoT VLAN** with no inbound internet port-forwarding.
- Enable **username/password authentication and topic ACLs** on your local MQTT broker if untrusted clients share the broker.
- Keep the firmware updated to the latest release via Local Web OTA.
