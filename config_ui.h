/*
 * Copyright (c) 2026 a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CONFIG_UI_H
#define CONFIG_UI_H

#include <stdint.h>
#include <stdbool.h>

#define SCREEN_COLS 40
#define SCREEN_ROWS 24
#define SCREEN_SIZE (SCREEN_COLS * SCREEN_ROWS) // 960 bytes

#define MAX_SCAN_APS 10

typedef enum {
    CFG_STATE_SCAN_SELECT = 0, // Select AP (cursor move, [R]escan, [M]anual SSID, [RETURN] confirm)
    CFG_STATE_MANUAL_SSID,     // Manual SSID entry
    CFG_STATE_PASSWORD,        // Password entry (masked display, BS supported, [RETURN] confirm)
    CFG_STATE_TEST_CONNECT,    // Connection test in progress / display result
    CFG_STATE_READY_TO_SAVE,   // Ready to save ([RETURN] SAVE & BOOT, [ESC] CANCEL)
    CFG_STATE_EXIT,            // Exit flag
} config_ui_state_t;

typedef struct {
    char ssid[33];
    int16_t rssi;
    uint32_t auth_mode;
} scan_ap_entry_t;

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_RET_QUIT        0  // Exit CONFIG (clear screen and proceed to normal boot)
#define CONFIG_RET_FULL_UPDATE 1  // Full screen update (initial/major change: 6502 redraws 960 bytes after HOME)
#define CONFIG_RET_UPDATE      CONFIG_RET_FULL_UPDATE // Compatibility alias
#define CONFIG_RET_SKIP        2  // No screen change (6502 skips screen transfer and scans keys)
#define CONFIG_RET_ROW_UPDATE  3  // Row update (6502 overwrites 40 chars of dirty rows without HOME)

// Initialize UI (load SRAM config, init UI states, start AP scan)
void config_ui_init(void);

// Process key input from Apple II (6502) and generate updated screen frame buffer
// Return values: CONFIG_RET_QUIT (0), CONFIG_RET_FULL_UPDATE (1), CONFIG_RET_SKIP (2), CONFIG_RET_ROW_UPDATE (3)
uint8_t config_ui_process(uint8_t key, uint8_t *screen_buffer_960);

// Get pointer to current 960-byte screen buffer
const uint8_t* config_ui_get_screen_buffer(void);

// Start / restart scanning nearby Wi-Fi APs
void config_ui_start_scan(void);

// Background polling task (monitor scan completion, etc.)
void config_ui_task(void);

// Get current UI state
config_ui_state_t config_ui_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_UI_H
