/*
 * Copyright (c) 2026 a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#include <tusb.h>
#include "transport.h"

void transport_init(void) {
    tusb_init();
}

void transport_task(void) {
    tud_task();
}

bool transport_is_connected(void) {
    return tud_cdc_connected();
}

int transport_read_byte(void) {
    return tud_cdc_read_char();
}

void transport_write_byte(uint8_t data) {
    while (tud_cdc_write_char(data) == 0) {
        if (!tud_cdc_connected()) {
            break;
        }
        tud_cdc_write_flush();
        tud_task();
    }
}

void transport_flush(void) {
    tud_cdc_write_flush();
}
