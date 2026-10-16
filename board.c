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

#include <pico/time.h>
#include <pico/multicore.h>
#include <hardware/sync.h>
#include <a2pico.h>

#include "firmware.h"
#include "sp.h"
#include "transport.h"

#include "board.h"

static volatile bool active;

static volatile uint32_t offset;

static volatile uint32_t self;

static void __time_critical_func(reset)(bool asserted) {
    static absolute_time_t assert_time;

    if (asserted) {
        if (assert_time != nil_time) {
            // Ignore unstable RESET line during Apple II power-up
            return;
        }
        active = false;
        offset = OFFSET_NORMAL;

        if (self) {
            firmware[self] = 0x3C;  // Identify as Disk II
        }

        multicore_fifo_drain();
        sp_reset();

        assert_time = get_absolute_time();
    } else {
        if (absolute_time_diff_us(assert_time, get_absolute_time()) > 200000) {
            offset = OFFSET_NORMAL;
        }
        assert_time = nil_time;
    }
}

static void __time_critical_func(dummy_get)(void) {
}

static void __time_critical_func(sp_status_get)(void) {
    uint8_t status = 0;
    if (transport_connected) {
        status |= 0x80;
    }
    if (config_required) {
        status |= 0x40;
    }
    a2pico_putdata(status);
}

static const void __not_in_flash("devsel_get") (*devsel_get[])(void) = {
    dummy_get, dummy_get, sp_status_get, dummy_get,
    dummy_get, dummy_get, dummy_get,     dummy_get,
    dummy_get, dummy_get, dummy_get,     dummy_get,
    dummy_get, dummy_get, dummy_get,     dummy_get
};

static void __time_critical_func(dummy_put)(uint32_t data) {
}

static const void __not_in_flash("devsel_put") (*devsel_put[])(uint32_t) = {
    dummy_put, dummy_put, dummy_put, dummy_put,
    dummy_put, dummy_put, dummy_put, dummy_put,
    dummy_put, dummy_put, dummy_put, dummy_put,
    dummy_put, dummy_put, dummy_put, dummy_put
};

static void __time_critical_func(sp_data_get)(void) {
    if (!active) {
        return;
    }
    a2pico_putdata(sp_buffer[sp_read_offset]);
    sp_read_offset++;
}

static void __time_critical_func(sp_control_get)(void) {
    if (!active) {
        return;
    }
    a2pico_putdata(sp_control);
}

static void __time_critical_func(deactivate_get)(void) {
    active = false;
}

static const void __not_in_flash("cffx_get") (*cffx_get[])(void) = {
    sp_data_get, sp_control_get, dummy_get,   dummy_get,
    dummy_get,   dummy_get,      dummy_get,   dummy_get,
    dummy_get,   dummy_get,      dummy_get,   dummy_get,
    dummy_get,   dummy_get,      dummy_get,   deactivate_get
};

static void __time_critical_func(sp_data_put)(uint32_t data) {
    if (!active) {
        return;
    }
    sp_buffer[sp_write_offset++] = data;
}

static void __time_critical_func(sp_control_put)(uint32_t data) {
    if (!active) {
        return;
    }
    __dmb();  // Ensure all sp_buffer parameter writes are visible before updating sp_control
    sp_control = data;
}

static void __time_critical_func(bank_clr_put)(uint32_t data) {
    if (!active) {
        return;
    }
    offset = OFFSET_NORMAL;
}

static void __time_critical_func(bank_set_put)(uint32_t data) {
    if (!active) {
        return;
    }
    offset = data << 11;
}

static void __time_critical_func(deactivate_put)(uint32_t data) {
    active = false;
}

static const void __not_in_flash("cffx_put") (*cffx_put[])(uint32_t) = {
    sp_data_put,  sp_control_put, dummy_put,    dummy_put,
    dummy_put,    dummy_put,      dummy_put,    dummy_put,
    dummy_put,    dummy_put,      dummy_put,    bank_clr_put,
    bank_set_put, dummy_put,      dummy_put,    deactivate_put
};

void __time_critical_func(board)(void) {

    a2pico_init();

    a2pico_resethandler(&reset);

    while (true) {
        uint32_t pico = a2pico_getaddr();
        uint32_t addr = pico & 0x0FFF;
        uint32_t io   = pico & 0x0F00;      // IOSTRB or IOSEL
        uint32_t strb = pico & 0x0800;      // IOSTRB
        uint32_t read = pico & RW_BIT;      // R/W

        if (read) {
            if (addr >= 0x0FF0) {
                cffx_get[addr & 0xF]();
            } else if (!io) {
                devsel_get[addr & 0xF]();
            } else if (!strb || active) {
                a2pico_putdata(firmware[offset + addr]);
            }
        } else {
            uint32_t data = a2pico_getdata();
            if (addr >= 0x0FF0) {
                cffx_put[addr & 0xF](data);
            } else if (!io) {
                devsel_put[addr & 0xF](data);
            }
        }

        if (io && !strb) {
            active = true;
            self = addr & 0x0700 | 0x0007;
            firmware[self] = 0x00;  // Identify as SmartPort
        }
    }
}

uint8_t board_slot(void) {
    return self >> 8;
}
