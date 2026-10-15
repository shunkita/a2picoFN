/*
 * Copyright (c) 2026 a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Initialize the underlying transport hardware (USB stack or CYW43/lwIP).
 */
void transport_init(void);

/*
 * Process background transport events (e.g., tud_task() or cyw43_arch_poll()).
 */
void transport_task(void);

/*
 * Returns true if an active host connection is established:
 * - CDC: DTR/RTS connected.
 * - TCP: FujiNet-PC client has connected to port 1985.
 */
extern volatile bool transport_connected;
extern volatile bool config_required;
bool transport_is_connected(void);
bool transport_start_wifi(void);

/*
 * Read a single byte from the transport stream.
 * Returns the byte (0x00 - 0xFF) if available, or -1 if no data is available.
 */
int transport_read_byte(void);

/*
 * Buffer or transmit a single byte over the transport stream.
 */
void transport_write_byte(uint8_t data);

/*
 * Flush any buffered transmit data over the wire/network.
 */
void transport_flush(void);

#endif
