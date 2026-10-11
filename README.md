# a2picoFN: Apple II SmartPort Bridge for FujiNet

[![Status: Work in Progress](https://img.shields.io/badge/Status-Work_in_Progress-yellow.svg)](#overview)
[![License](https://img.shields.io/badge/License-GPL--2.0-blue.svg)](LICENSE)

> [!NOTE]
> **Work in Progress:** This project currently depends on unmerged upstream fixes in [fujinet-firmware](https://github.com/FujiNetWIFI/fujinet-firmware) (FujiNet-PC). Running `a2picoFN` out-of-the-box with stock upstream releases is not yet supported.

**a2picoFN** is an Apple II expansion card firmware that integrates FujiNet functionality using Raspberry Pi Pico 2 microcontrollers (RP2350). It emulates a SmartPort interface card on the Apple II bus and bridges storage and network requests to **FujiNet-PC** via a high-speed USB CDC link using the SLIP protocol.

---

## Overview

`a2picoFN` bridges the classic Apple II bus to the modern FujiNet ecosystem:

* **Dual-Core Task Separation:**
  * **Core 1 (Apple II Bus Frontend):** Responds in real-time to Apple II slot access (`$Cn00`, `$CFFF`, SmartPort registers) with minimal latency, building on low-level bus interfacing techniques from A2retroNET / a2pico.
  * **Core 0 (Host Transport & Protocol Bridge):** Manages packet serialization and framing over USB CDC (SLIP protocol) directly to a host PC running FujiNet-PC.
* **Target Hardware:**
  * Supports **RP2350 (Raspberry Pi Pico 2)** microcontrollers (tested on **A2Pico2Lite W**).

---

## Building the Firmware

Builds are managed via the standard Raspberry Pi Pico SDK CMake workflow:

```bash
mkdir build && cd build
cmake -DPICO_BOARD=a2pico2litew ..
cmake --build .
```

*Supported `PICO_BOARD` targets:*
* `a2pico2litew`: A2Pico2Lite W (tested)
* `a2pico2-generic`: A2Pico2Lite non-W (untested)

---

## License

This project is licensed under the [GNU General Public License v2.0](LICENSE).
