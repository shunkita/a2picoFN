# a2picoFN: Apple II SmartPort Network Bridge for FujiNet

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**a2picoFN** is an Apple II expansion card firmware for the A2Pico2 series (RP2350). It emulates a SmartPort interface card on the Apple II bus and bridges storage and network requests to **FujiNet-PC** over TCP (port 1985) using the standard SLIP protocol across multiple network transports: **Wi-Fi**, **W5500 Ethernet**, and **USB CDC-ECM**.

---

## Firmware Variants & Supported Hardware

A single CMake build produces three firmware variants for the `a2pico2` board family (runtime board detection via `a2pico_init()`):

| Transport | A2Pico2Lite | A2Pico2Lite W | A2Pico2 WE | Variant Binary |
| :--- | :---: | :---: | :---: | :--- |
| Wi-Fi | | ○ | ○ | `a2picoFN-Wifi-<VERSION>.uf2` |
| Ether | | | ○ | `a2picoFN-Ether-<VERSION>.uf2` |
| CDC-ECM | ○ | ○ | ○ | `a2picoFN-UsbEcm-<VERSION>.uf2` |

---

## Tested Environment

* **Host Machine:** Apple IIe enhanced
* **FujiNet Firmware (`fujinet-pc`):** `v1.6.1`
* **CONFIG Program:** `c6b51eb`

---

## Configuration

### FujiNet-PC Setup (`fnconfig.ini`)

`a2picoFN` operates as a TCP server listening on port `1985`. Configure `fujinet-pc` in `fnconfig.ini`:

**Wi-Fi / W5500 Ethernet (DHCP):**
```ini
[BOIP]
enabled=1
host=<a2picoFN_IP_address>
```

**USB CDC-ECM (Direct USB connection to PC/Mac, static `169.254.19.85/16`):**
```ini
[BOIP]
enabled=1
host=169.254.19.85
```

### Apple II On-Screen Setup / Status (CONFIG)

* **Wi-Fi Variant:**
  * On first boot (unconfigured), the interactive Wi-Fi CONFIG screen opens automatically to scan APs and save credentials to Flash (`TAB` to switch fields, `RETURN` to save).
  * On subsequent boots, press `C` or `ESC` while waiting for connection to reopen CONFIG.
* **W5500 Ethernet & USB CDC-ECM Variants:**
  * Boot directly into connection standby without manual configuration.
  * Press `C` or `ESC` during standby to view the read-only network status screen (`RETURN` or `ESC` to exit).

---

## Building the Firmware

Builds use the standard Raspberry Pi Pico SDK CMake workflow with `PICO_BOARD=a2pico2`, generating all three variant `.uf2` files in a single build:

```bash
cmake -S . -B build -DPICO_BOARD=a2pico2
cmake --build build -j
```

Output binaries in `build/`:
* `a2picoFN-Wifi-<VERSION>.uf2`
* `a2picoFN-Ether-<VERSION>.uf2`
* `a2picoFN-UsbEcm-<VERSION>.uf2`

Optional CMake variables:
* `-DVERSION=<YYYY-MM-DD>`: Firmware version suffix.
* `-DNET_HOSTNAME=<name>`: DHCP hostname (default: `a2picofn`).
* `-DFUJINET_PC_PORT=<port>`: TCP listen port (default: `1985`).

---

## License

This project is licensed under the [MIT License](LICENSE).
