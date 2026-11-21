/*
 * Copyright (c) 2026 a2picoAFP / a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef A2PICO_W5500_DRV_H
#define A2PICO_W5500_DRV_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize SPI1 (20 MHz), reset W5500, set MAC, and open Socket 0 in MACRAW mode */
bool w5500_init(const uint8_t mac[6]);

/* Query W5500 PHYCFGR link-up bit */
bool w5500_is_link_up(void);

/* Transmit a single raw Ethernet frame (returns true on success) */
bool w5500_send_frame(const uint8_t *frame, uint16_t len);

/*
 * Poll Socket 0 RX buffer for a raw Ethernet frame.
 * Returns frame byte length (>0) if a frame was read into buf, or 0 if none.
 */
uint16_t w5500_recv_frame(uint8_t *buf, uint16_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* A2PICO_W5500_DRV_H */
