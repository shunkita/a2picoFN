/*

MIT License

Copyright (c) 2024 Oliver Schmidt (https://a2retro.de/)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include <hardware/sync.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "firmware.h"
#include "slip.h"
#include "transport.h"
#include "config_ui.h"

#include "sp.h"

volatile uint8_t sp_control;
volatile uint8_t sp_buffer[0x0400];
volatile uint16_t sp_read_offset;
volatile uint16_t sp_write_offset;

void sp_init(void) {
    // Dedicated SmartPort bridge: explicitly reset state before bus operations
    sp_reset();
}

void __time_critical_func(sp_reset)(void) {
    sp_control = CONTROL_NONE;
    sp_read_offset = sp_write_offset = 0;
    sp_buffer[0] = sp_buffer[1] = 0;
    slip_reset();
}

void sp_task(void) {
    slip_task();
    config_ui_task();

    switch (sp_control) {
        case CONTROL_NONE:
        case CONTROL_DONE:
            return;

        case CONTROL_CONFIG: {
            uint8_t key = sp_buffer[0];
            uint8_t ret = config_ui_process(key, (uint8_t *)&sp_buffer[1]);
            sp_buffer[0] = ret;
            if (ret == CONFIG_RET_QUIT) {
                config_required = false;
                sp_read_offset = sp_write_offset = 0;
                __dmb();
                sp_control = CONTROL_DONE; // Complete response to Apple II first to release the bus
                transport_start_wifi();    // Start Wi-Fi connection afterwards
                return;
            }
            break;
        }

        case CONTROL_PRODOS:
        case CONTROL_SP:
            slip_command(sp_control, (uint8_t *)sp_buffer);
            break;
    }

    sp_read_offset = sp_write_offset = 0;
    __dmb(); // Ensure all buffer payload and firmware writes are visible before
             // setting DONE
    sp_control = CONTROL_DONE;
}

