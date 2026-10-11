/*
 * Copyright (c) 2026 a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#if TEST_MODE

#include <stdio.h>
#include <string.h>
#include <pico/stdlib.h>
#include <a2pico.h>

#include "sp.h"
#include "slip.h"
#include "test.h"

#if defined(PICO_CYW43_SUPPORTED) && PICO_CYW43_SUPPORTED
#include <pico/cyw43_arch.h>
#endif

typedef enum {
    TEST_STATUS_PENDING = 0,
    TEST_STATUS_RUNNING,
    TEST_STATUS_SUCCESS,
    TEST_STATUS_FAILED
} test_status_t;

static test_status_t current_status = TEST_STATUS_PENDING;

static bool led_ready = false;
#if defined(PICO_CYW43_SUPPORTED) && PICO_CYW43_SUPPORTED
static bool use_cyw43_led = false;
#endif

static void set_led_state(bool on) {
    if (!led_ready) {
        return;
    }
#if defined(PICO_CYW43_SUPPORTED) && PICO_CYW43_SUPPORTED
    if (use_cyw43_led) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
        cyw43_arch_poll();
        return;
    }
#endif
    if (a2pico_led() >= 0) {
        gpio_put(a2pico_led(), on);
    }
}

/*
 * Update on-board LED feedback non-blockingly based on the current test status.
 * - PENDING: LED is OFF.
 * - RUNNING: Fast blinking (100 ms period).
 * - SUCCESS: Solid ON when all steps succeed.
 * - FAILED:  Slow continuous blinking (500 ms period).
 */
static void update_led_feedback(void) {
    static absolute_time_t last_toggle;
    static bool led_state = false;
    static bool led_initialized = false;

    if (!led_initialized) {
#if defined(PICO_CYW43_SUPPORTED) && PICO_CYW43_SUPPORTED
        use_cyw43_led = true;
        led_ready = true;
#endif
        if (!led_ready && a2pico_led() >= 0) {
            gpio_init(a2pico_led());
            gpio_set_dir(a2pico_led(), GPIO_OUT);
            gpio_put(a2pico_led(), false);
            led_ready = true;
        }
        last_toggle = get_absolute_time();
        led_initialized = true;
    }

    if (!led_ready) {
        return;
    }

#if defined(PICO_CYW43_SUPPORTED) && PICO_CYW43_SUPPORTED
    if (use_cyw43_led) {
        cyw43_arch_poll();
    }
#endif

    switch (current_status) {
        case TEST_STATUS_PENDING:
            // Keep solid ON from boot so user has immediate visual confirmation
            set_led_state(true);
            break;

        case TEST_STATUS_RUNNING:
            if (absolute_time_diff_us(last_toggle, get_absolute_time()) >= 100000) {
                led_state = !led_state;
                set_led_state(led_state);
                last_toggle = get_absolute_time();
            }
            break;

        case TEST_STATUS_SUCCESS:
            set_led_state(true);
            break;

        case TEST_STATUS_FAILED:
            if (absolute_time_diff_us(last_toggle, get_absolute_time()) >= 500000) {
                led_state = !led_state;
                set_led_state(led_state);
                last_toggle = get_absolute_time();
            }
            break;
    }
}

/*
 * Diagnostic test runner for Core0.
 * Exercises actual host requests over USB CDC SLIP directly against FujiNet-PC
 * (SLIP_PROTOCOL=COM), distinguishing local controller operations from host requests.
 */
void test_core0_task(void) {
    static bool test_done = false;
    static absolute_time_t connect_time;
    static bool timer_started = false;

    // Continuously maintain LED status pattern even after test completion
    update_led_feedback();

    // Prevent multiple executions once completed
    if (test_done) {
        return;
    }

    // Only start after USB CDC SLIP connection is established
    if (!slip_connected()) {
        current_status = TEST_STATUS_PENDING;
        timer_started = false;
        return;
    }

    // Allow settling delay (500 ms) after connection and initial INIT scan
    current_status = TEST_STATUS_RUNNING;
    if (!timer_started) {
        connect_time = get_absolute_time();
        timer_started = true;
        return;
    }

    if (absolute_time_diff_us(connect_time, get_absolute_time()) < 500000) {
        return;
    }

    printf("\n==================================================\n");
    printf("=== [a2picoFN Core0 Diagnostic Test Mode] ===\n");
    printf("==================================================\n");

    /*
     * Step 1: INIT Scan Result (Local controller loop + Host Unit 1..N scan).
     * Note: A Unit 0 INIT is a local controller scan loop in slip_command().
     * The actual requests sent to FujiNet-PC are INIT commands for Unit 1..N.
     */
    uint8_t units = slip_units();
    printf("[TEST] Step 1 [Local Result]: INIT Scan Summary\n");
    printf("       (Local Unit 0 loop scanned Unit 1..N against FujiNet-PC)\n");
    printf("       Detected SmartPort units: %d\n", units);

    if (units == 0) {
        printf("[TEST] WARNING: 0 units detected from FujiNet-PC.\n");
        printf("       Ensure FujiNet-PC is running with SLIP_PROTOCOL=COM\n");
        printf("       and an image is mounted in drive 1.\n");
        printf("[TEST] Aborting STATUS and READ_BLOCK host tests.\n");
        printf("==================================================\n\n");
        current_status = TEST_STATUS_FAILED;
        test_done = true;
        return;
    }

    static uint8_t test_buf[0x400];

    /*
     * Step 2: STATUS command on Unit 1 (Host request for Device Status).
     * Format: CMD=0, Params=3, Unit=1, Code=0 (STS)
     * Note: Unlike Unit 0 STATUS (which is synthesized locally), Unit 1 STATUS
     * is transmitted as an actual SLIP packet to FujiNet-PC.
     */
    printf("[TEST] Step 2 [Host Request]: Sending STATUS to Unit 1 (Device Status)\n");
    memset(test_buf, 0, sizeof(test_buf));
    test_buf[SP_I_CMD]       = SP_CMD_STATUS;
    test_buf[SP_I_PARAM_CNT] = 3;
    test_buf[SP_I_UNIT]      = 1;
    test_buf[SP_I_STS_CODE]  = SP_STATUS_STS;
    slip_command(CONTROL_SP, test_buf);

    uint8_t retval = test_buf[SP_O_RETVAL];
    if (retval == SP_SUCCESS) {
        uint16_t sts_len = (uint16_t)test_buf[SP_O_STS_CNT + 0] |
                           ((uint16_t)test_buf[SP_O_STS_CNT + 1] << 8);
        printf("       Result: SUCCESS (retval = 0x%02X, length = %u bytes)\n", retval, sts_len);

        // Safe boundary check: Device Status payload requires at least 4 bytes
        if (sts_len >= 4 && sts_len <= (sizeof(test_buf) - SP_O_STS_LIST)) {
            uint8_t stat_byte = test_buf[SP_O_STS_LIST + 0];
            uint32_t blocks = (uint32_t)test_buf[SP_O_STS_LIST + 1] |
                              ((uint32_t)test_buf[SP_O_STS_LIST + 2] << 8) |
                              ((uint32_t)test_buf[SP_O_STS_LIST + 3] << 16);
            printf("       Device status byte: 0x%02X (Online: %d, WProt: %d)\n",
                   stat_byte, (stat_byte & 0x10) ? 1 : 0, (stat_byte & 0x04) ? 1 : 0);
            printf("       Total block count : %lu blocks (%lu KB)\n", blocks, blocks / 2);
        } else {
            printf("       Warning: Unexpected status length %u (expected >= 4)\n", sts_len);
            current_status = TEST_STATUS_FAILED;
            test_done = true;
            return;
        }
    } else {
        printf("       Result: ERROR (retval = 0x%02X)\n", retval);
        current_status = TEST_STATUS_FAILED;
        test_done = true;
        return;
    }

    /*
     * Step 3: READ_BLOCK command on Unit 1, Block 0 (Host request for Boot Block).
     * Format: CMD=1, Params=3, Unit=1, Buff=$2000, Block=0
     * Note: Transmitted as an actual SLIP packet to FujiNet-PC.
     * gen_read_block() generates 6502 JIT code into the internal firmware[] array
     * at BANK 2 / BANK 3. Bus hardware wait cycles are not performed.
     */
    printf("[TEST] Step 3 [Host Request]: Sending READ_BLOCK to Unit 1, Block 0\n");
    memset(test_buf, 0, sizeof(test_buf));
    test_buf[SP_I_CMD]          = SP_CMD_READBLK;
    test_buf[SP_I_PARAM_CNT]    = 3;
    test_buf[SP_I_UNIT]         = 1;
    test_buf[SP_I_BLK_BUFF + 0] = 0x00;  // Target 6502 memory address $2000 (low)
    test_buf[SP_I_BLK_BUFF + 1] = 0x20;  // Target 6502 memory address $2000 (high)
    test_buf[SP_I_BLK_BLOCK + 0]= 0x00;  // Block 0 (low)
    test_buf[SP_I_BLK_BLOCK + 1]= 0x00;  // Block 0 (mid)
    test_buf[SP_I_BLK_BLOCK + 2]= 0x00;  // Block 0 (high)
    slip_command(CONTROL_SP, test_buf);

    retval = test_buf[SP_O_RETVAL];
    if (retval == SP_SUCCESS) {
        printf("       Result: SUCCESS (retval = 0x%02X)\n", retval);
        printf("       Block 0 payload sample (first 16 bytes):\n       ");
        for (int i = 0; i < 16; i++) {
            printf("%02X ", test_buf[SP_O_BLK_BUFFER + i]);
        }
        printf("\n");
        printf("       [DMA Note] 6502 transfer routine written to firmware[] buffer.\n");
        printf("                  Apple II RAM transfer execution remains untested.\n");
        current_status = TEST_STATUS_SUCCESS;
    } else {
        printf("       Result: ERROR (retval = 0x%02X)\n", retval);
        current_status = TEST_STATUS_FAILED;
    }

    printf("==================================================\n");
    if (current_status == TEST_STATUS_SUCCESS) {
        printf("=== [Diagnostic Test Finished Successfully] ===\n");
    } else {
        printf("=== [Diagnostic Test FAILED] ===\n");
    }
    printf("==================================================\n\n");

    test_done = true;
}

#endif
