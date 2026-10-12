# a2picoFN: Apple II SmartPort Wi-Fi Bridge for FujiNet

[![Status: Work in Progress](https://img.shields.io/badge/Status-Work_in_Progress-yellow.svg)](#overview)
[![License](https://img.shields.io/badge/License-GPL--2.0-blue.svg)](LICENSE)

> [!WARNING]
> **Active Development / Work in Progress**
> This project is currently under active development. Architecture, configuration methods, and firmware features are subject to rapid iteration and changes.

**a2picoFN** is an Apple II expansion card firmware that integrates FujiNet functionality using Raspberry Pi Pico 2 W microcontrollers (RP2350). It emulates a SmartPort interface card on the Apple II bus and bridges storage and network requests to **FujiNet-PC** over a transparent Wi-Fi TCP streaming connection using the standard SLIP protocol.

---

## Overview

`a2picoFN` bridges the classic Apple II bus to the modern FujiNet ecosystem wirelessly:

* **Dual-Core Task Separation:**
  * **Core 1 (Apple II Bus Frontend):** Responds in real-time to Apple II slot access (`$Cn00`, `$C800-$CFFF`, SmartPort registers) with sub-microsecond latency, building on low-level PIO bus interfacing from a2pico.
  * **Core 0 (Wi-Fi Transport & Protocol Bridge):** Drives the CYW43 Wi-Fi stack (lwIP raw polling), runs an on-chip TCP server (port 1985), and handles transparent SLIP framing directly with FujiNet-PC.
* **Target Hardware:**
  * **Raspberry Pi Pico 2 W** (RP2350A + CYW43439), tested on **A2Pico2Lite W**.

---

## Tested Environment

Hardware verification and testing have been performed using specific, fixed versions of FujiNet components (pinned versions rather than rolling latest builds):

* **FujiNet Firmware (`fujinet-pc`):** `v1.5.1+git-3d8`
* **CONFIG Program:** `031e086*`

---

## Configuration

> [!NOTE]
> **Provisional Configuration During Development**
> Hardcoding Wi-Fi credentials and connection parameters in header files is a provisional configuration method during development. A persistent/interactive configuration mechanism will be introduced in future updates.

Wi-Fi network credentials and FujiNet-PC settings are configured in `wifi_config.h`:

```c
#define WIFI_SSID       "your-ssid"
#define WIFI_PASSWORD   "your-password"
#define FUJINET_PC_PORT 1985
```

---

## Building the Firmware

Builds are managed via the standard Raspberry Pi Pico SDK CMake workflow:

```bash
mkdir build && cd build
cmake -DPICO_BOARD=a2pico2litew ..
cmake --build .
```

---

## License

This project is licensed under the [GNU General Public License v2.0](LICENSE).

