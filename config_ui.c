/*
 * Copyright (c) 2026 a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>

#include "wifi_config.h"
#include "flash_config.h"
#include "config_ui.h"

static uint8_t screen_buf[SCREEN_SIZE];
static uint8_t displayed_screen[SCREEN_SIZE];
static bool full_redraw_needed = true;

static config_ui_state_t ui_state = CFG_STATE_SCAN_SELECT;

static char current_ssid[MAX_SSID_LEN + 1] = "";
static char current_password[MAX_PASS_LEN + 1] = "";

static scan_ap_entry_t ap_list[MAX_SCAN_APS];
static int ap_count = 0;
static int selected_ap_idx = 0;
static int ap_scroll_offset = 0;
static bool scan_active = false;
static bool dirty = true;

#define AP_DISPLAY_ROWS 5

// -------------------------------------------------------------
// Apple II screen code conversion utility
// -------------------------------------------------------------

static inline uint8_t to_apple2_screen_code(char c) {
    return (uint8_t)c | 0x80;       // Normal display (Bit 7 = 1)
}

static void fill_row(int row, char c) {
    if (row < 0 || row >= SCREEN_ROWS) return;
    uint8_t code = to_apple2_screen_code(c);
    int offset = row * SCREEN_COLS;
    for (int i = 0; i < SCREEN_COLS; i++) {
        screen_buf[offset + i] = code;
    }
}

static void draw_text(int row, int col, const char *str) {
    if (row < 0 || row >= SCREEN_ROWS || col < 0 || col >= SCREEN_COLS) return;
    int offset = row * SCREEN_COLS + col;
    while (*str && col < SCREEN_COLS) {
        screen_buf[offset++] = to_apple2_screen_code(*str++);
        col++;
    }
}

static void clear_screen(void) {
    for (int i = 0; i < SCREEN_ROWS; i++) {
        fill_row(i, ' ');
    }
}

// -------------------------------------------------------------
// Wi-Fi AP scan callback & control
// -------------------------------------------------------------

static int scan_result_callback(void *env, const cyw43_ev_scan_result_t *result) {
    if (!result || result->ssid_len == 0) {
        return 0;
    }

    // Duplicate check
    char name[33];
    size_t len = result->ssid_len < 32 ? result->ssid_len : 32;
    memcpy(name, result->ssid, len);
    name[len] = '\0';

    for (int i = 0; i < ap_count; i++) {
        if (strcmp(ap_list[i].ssid, name) == 0) {
            if (result->rssi > ap_list[i].rssi) {
                ap_list[i].rssi = result->rssi;
                dirty = true;
            }
            return 0;
        }
    }

    if (ap_count < MAX_SCAN_APS) {
        strncpy(ap_list[ap_count].ssid, name, 32);
        ap_list[ap_count].ssid[32] = '\0';
        ap_list[ap_count].rssi = result->rssi;
        ap_list[ap_count].auth_mode = result->auth_mode;
        ap_count++;
        dirty = true;
    }

    return 0;
}

void config_ui_start_scan(void) {
    if (cyw43_wifi_scan_active(&cyw43_state)) {
        return;
    }
    ap_count = 0;
    selected_ap_idx = 0;
    ap_scroll_offset = 0;
    scan_active = true;
    dirty = true;

    cyw43_wifi_scan_options_t scan_options = {0};
    int rc = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, scan_result_callback);
    if (rc != 0) {
        scan_active = false;
        dirty = true;
    }
}

void config_ui_task(void) {
    if (scan_active && !cyw43_wifi_scan_active(&cyw43_state)) {
        scan_active = false;
        dirty = true;
    }
}

config_ui_state_t config_ui_get_state(void) {
    return ui_state;
}

const uint8_t* config_ui_get_screen_buffer(void) {
    return screen_buf;
}

// -------------------------------------------------------------
// Screen layout rendering engine
// -------------------------------------------------------------

static void render_screen(void) {
    clear_screen();

    // Row 0 - 2: Header
    fill_row(0, '=');
    draw_text(1, 0, "           a2picoFN WI-FI SETUP         ");
    fill_row(2, '=');

    // Row 3: CURRENT SSID display
    char ssid_line[41];
    snprintf(ssid_line, sizeof(ssid_line), "CURRENT SSID: [%-24s]", current_ssid);
    draw_text(3, 0, ssid_line);

    // Row 5: AVAILABLE NETWORKS header
    if (ui_state == CFG_STATE_MANUAL_SSID) {
        draw_text(5, 0, "MANUAL SSID ENTRY:");
        char manual_line[41];
        char manual_buf[37];
        size_t ssid_len = strlen(current_ssid);
        for (size_t i = 0; i < sizeof(manual_buf) - 1; i++) {
            if (i < ssid_len) {
                manual_buf[i] = current_ssid[i];
            } else if (i == ssid_len) {
                manual_buf[i] = '_';
            } else {
                manual_buf[i] = ' ';
            }
        }
        manual_buf[sizeof(manual_buf) - 1] = '\0';
        char manual_ind = (ui_state == CFG_STATE_MANUAL_SSID) ? '*' : ' ';
        snprintf(manual_line, sizeof(manual_line), "%c[%-36s] ", manual_ind, manual_buf);
        draw_text(6, 0, manual_line);
        draw_text(11, 4, "[RETURN] CONFIRM       [ESC] CANCEL");
    } else {
        if (scan_active) {
            draw_text(5, 0, "AVAILABLE NETWORKS: (SCANNING...)");
        } else {
            draw_text(5, 0, "AVAILABLE NETWORKS:");
        }

        // Row 6 - 10: Scan result list (up to 5 items visible & scrollable)
        if (ap_count == 0) {
            draw_text(6, 2, "(NO NETWORKS FOUND - PRESS [R])");
        } else {
            if (selected_ap_idx < ap_scroll_offset) {
                ap_scroll_offset = selected_ap_idx;
            } else if (selected_ap_idx >= ap_scroll_offset + AP_DISPLAY_ROWS) {
                ap_scroll_offset = selected_ap_idx - AP_DISPLAY_ROWS + 1;
            }
            if (ap_scroll_offset < 0) {
                ap_scroll_offset = 0;
            }

            for (int i = 0; i < AP_DISPLAY_ROWS; i++) {
                int idx = ap_scroll_offset + i;
                if (idx >= ap_count) break;
                char ap_line[41];
                char ind = (ui_state == CFG_STATE_SCAN_SELECT && idx == selected_ap_idx) ? '*' : ' ';
                snprintf(ap_line, sizeof(ap_line), "%c   %2d. %-17s [%4ddBm]",
                         ind, idx + 1, ap_list[idx].ssid, ap_list[idx].rssi);
                draw_text(6 + i, 0, ap_line);
            }
        }
        // Row 11: Operation guide
        draw_text(11, 4, "[R]EFRESH LIST   [M]ANUAL ENTRY");
    }

    // Row 12 - 13: PASSWORD input field
    draw_text(12, 0, "PASSWORD:");
    char pass_display[41];
    char pass_buf[37];
    size_t pass_len = strlen(current_password);
    for (size_t i = 0; i < sizeof(pass_buf) - 1; i++) {
        if (i < pass_len) {
            pass_buf[i] = current_password[i];
        } else if (i == pass_len && ui_state == CFG_STATE_PASSWORD) {
            pass_buf[i] = '_';
        } else {
            pass_buf[i] = ' ';
        }
    }
    pass_buf[sizeof(pass_buf) - 1] = '\0';
    char pass_ind = (ui_state == CFG_STATE_PASSWORD) ? '*' : ' ';
    snprintf(pass_display, sizeof(pass_display), "%c[%-36s] ", pass_ind, pass_buf);
    draw_text(13, 0, pass_display);

    // Row 19 - 20: Footer operation line
    fill_row(19, '=');
    char save_line[41];
    char save_ind = (ui_state == CFG_STATE_READY_TO_SAVE) ? '*' : ' ';
    snprintf(save_line, sizeof(save_line), "%c[RETURN] SAVE & BOOT     [ESC] CANCEL  ", save_ind);
    draw_text(20, 0, save_line);

    // Row 22: Status and guide line
    switch (ui_state) {
        case CFG_STATE_SCAN_SELECT:
            draw_text(22, 1, "[UP/DN] SELECT  [RETURN] OK  [ESC] EXIT");
            break;
        case CFG_STATE_MANUAL_SSID:
            draw_text(22, 1, "TYPE SSID  [BS] DEL  [RETURN] CONFIRM");
            break;
        case CFG_STATE_PASSWORD:
            draw_text(22, 1, "TYPE PASS  [BS] DEL  [RETURN] NEXT");
            break;
        case CFG_STATE_TEST_CONNECT:
            draw_text(22, 1, "TESTING... PLEASE WAIT");
            break;
        case CFG_STATE_READY_TO_SAVE:
            draw_text(22, 1, "[RETURN] WRITE FLASH & BOOT  [ESC] QUIT");
            break;
        default:
            break;
    }
}

// -------------------------------------------------------------
// Initialization
// -------------------------------------------------------------

void config_ui_init(void) {
    if (flash_config_is_valid()) {
        const wifi_config_block_t *cfg = flash_config_get();
        strncpy(current_ssid, cfg->ssid, MAX_SSID_LEN);
        current_ssid[MAX_SSID_LEN] = '\0';
        strncpy(current_password, cfg->password, MAX_PASS_LEN);
        current_password[MAX_PASS_LEN] = '\0';
    } else {
        strncpy(current_ssid, WIFI_SSID, MAX_SSID_LEN);
        current_ssid[MAX_SSID_LEN] = '\0';
        strncpy(current_password, WIFI_PASSWORD, MAX_PASS_LEN);
        current_password[MAX_PASS_LEN] = '\0';
    }

    ui_state = CFG_STATE_SCAN_SELECT;
    ap_count = 0;
    selected_ap_idx = 0;
    ap_scroll_offset = 0;
    full_redraw_needed = true;
    memset(displayed_screen, 0, sizeof(displayed_screen));

    // Initialize CYW43 & start AP scan
    cyw43_arch_enable_sta_mode();
    config_ui_start_scan();

    render_screen();
}

// -------------------------------------------------------------
// Build screen update packet (dirty rows / full screen)
// -------------------------------------------------------------

static uint8_t build_screen_update_packet(uint8_t *out_buffer) {
    render_screen();

    if (!out_buffer) {
        dirty = false;
        return CONFIG_RET_SKIP;
    }

    // Initial display or forced full redraw requested
    if (full_redraw_needed) {
        memcpy(out_buffer, screen_buf, SCREEN_SIZE);
        memcpy(displayed_screen, screen_buf, SCREEN_SIZE);
        full_redraw_needed = false;
        dirty = false;
        return CONFIG_RET_FULL_UPDATE; // 1
    }

    // Scan for dirty rows
    uint8_t dirty_rows[SCREEN_ROWS];
    int num_dirty = 0;
    for (int r = 0; r < SCREEN_ROWS; r++) {
        if (memcmp(&displayed_screen[r * SCREEN_COLS], &screen_buf[r * SCREEN_COLS], SCREEN_COLS) != 0) {
            dirty_rows[num_dirty++] = (uint8_t)r;
        }
    }

    // Skip transfer if no changes
    if (num_dirty == 0) {
        dirty = false;
        return CONFIG_RET_SKIP; // 2
    }

    // Fall back to full update if too many rows changed (> 8 rows)
    if (num_dirty > 8) {
        memcpy(out_buffer, screen_buf, SCREEN_SIZE);
        memcpy(displayed_screen, screen_buf, SCREEN_SIZE);
        dirty = false;
        return CONFIG_RET_FULL_UPDATE; // 1
    }

    // Build row update packet
    // out_buffer[0] = num_dirty (number of updated rows)
    // Each entry: [row_idx (1B)] [40 chars (40B)]
    uint8_t *p = out_buffer;
    *p++ = (uint8_t)num_dirty;
    for (int i = 0; i < num_dirty; i++) {
        uint8_t r = dirty_rows[i];
        *p++ = r;
        memcpy(p, &screen_buf[r * SCREEN_COLS], SCREEN_COLS);
        p += SCREEN_COLS;
        memcpy(&displayed_screen[r * SCREEN_COLS], &screen_buf[r * SCREEN_COLS], SCREEN_COLS);
    }

    dirty = false;
    return CONFIG_RET_ROW_UPDATE; // 3
}

// -------------------------------------------------------------
// Key input & state machine processing
// -------------------------------------------------------------

static bool ui_initialized = false;

uint8_t config_ui_process(uint8_t key, uint8_t *screen_buffer_960) {
    // Initial signature from 6502 CFGHDLR ($F1: UPPER, $F2: LOWER)
    // Initialize UI only on first entry, allow subsequent 0xF2 ('r') as regular key
    if (!ui_initialized && (key == 0xF1 || key == 0xF2)) {
        ui_initialized = true;
        config_ui_init();
        full_redraw_needed = true;
        return build_screen_update_packet(screen_buffer_960);
    }

    // 1. Key not pressed (Bit 7 = 0)
    if (key < 0x80) {
        if (!dirty) {
            return CONFIG_RET_SKIP; // No changes: skip screen transfer (2)
        }
        // Update screen if marked dirty (e.g., async Wi-Fi scan completed)
        return build_screen_update_packet(screen_buffer_960);
    }

    // 2. Valid key input (Bit 7 = 1)
    uint8_t k = key & 0x7F; // Normalize to ASCII

    switch (ui_state) {
        case CFG_STATE_SCAN_SELECT:
            if (k == 0x0B || k == 'A' || k == 'a' || k == 'U' || k == 'u') { // UP
                if (selected_ap_idx > 0) {
                    selected_ap_idx--;
                    if (selected_ap_idx < ap_scroll_offset) {
                        ap_scroll_offset = selected_ap_idx;
                    }
                    dirty = true;
                }
            } else if (k == 0x0A || k == 'Z' || k == 'z' || k == 'D' || k == 'd') { // DOWN
                if (selected_ap_idx < ap_count - 1) {
                    selected_ap_idx++;
                    if (selected_ap_idx >= ap_scroll_offset + AP_DISPLAY_ROWS) {
                        ap_scroll_offset = selected_ap_idx - AP_DISPLAY_ROWS + 1;
                    }
                    dirty = true;
                }
            } else if (k == 'R' || k == 'r') {
                config_ui_start_scan();
                dirty = true;
            } else if (k == 'M' || k == 'm') {
                current_ssid[0] = '\0';
                ui_state = CFG_STATE_MANUAL_SSID;
                full_redraw_needed = true;
                dirty = true;
            } else if (k == 0x09) { // TAB
                if (ap_count > 0 && selected_ap_idx < ap_count) {
                    strncpy(current_ssid, ap_list[selected_ap_idx].ssid, MAX_SSID_LEN);
                    current_ssid[MAX_SSID_LEN] = '\0';
                }
                ui_state = CFG_STATE_PASSWORD;
                dirty = true;
            } else if (k == 0x0D) { // RETURN
                if (ap_count > 0 && selected_ap_idx < ap_count) {
                    strncpy(current_ssid, ap_list[selected_ap_idx].ssid, MAX_SSID_LEN);
                    current_ssid[MAX_SSID_LEN] = '\0';
                }
                ui_state = CFG_STATE_PASSWORD;
                dirty = true;
            } else if (k == 0x1B) { // ESC
                ui_state = CFG_STATE_EXIT;
                ui_initialized = false;
                full_redraw_needed = true;
                return CONFIG_RET_QUIT; // Exit (0)
            }
            break;

        case CFG_STATE_MANUAL_SSID:
            if (k == 0x08 || k == 0x7F) { // BS
                size_t len = strlen(current_ssid);
                if (len > 0) {
                    current_ssid[len - 1] = '\0';
                    dirty = true;
                }
            } else if (k == 0x09) { // TAB
                ui_state = CFG_STATE_PASSWORD;
                full_redraw_needed = true;
                dirty = true;
            } else if (k == 0x0D) { // RETURN
                ui_state = CFG_STATE_PASSWORD;
                full_redraw_needed = true;
                dirty = true;
            } else if (k == 0x1B) { // ESC
                ui_state = CFG_STATE_SCAN_SELECT;
                full_redraw_needed = true;
                dirty = true;
            } else if (k >= 0x20 && k <= 0x7E) {
                size_t len = strlen(current_ssid);
                if (len < MAX_SSID_LEN) {
                    current_ssid[len] = (char)k;
                    current_ssid[len + 1] = '\0';
                    dirty = true;
                }
            }
            break;

        case CFG_STATE_PASSWORD:
            if (k == 0x08 || k == 0x7F) { // BS
                size_t len = strlen(current_password);
                if (len > 0) {
                    current_password[len - 1] = '\0';
                    dirty = true;
                }
            } else if (k == 0x09) { // TAB
                ui_state = CFG_STATE_READY_TO_SAVE;
                dirty = true;
            } else if (k == 0x0D) { // RETURN
                ui_state = CFG_STATE_READY_TO_SAVE;
                dirty = true;
            } else if (k == 0x1B) { // ESC
                ui_state = CFG_STATE_SCAN_SELECT;
                dirty = true;
            } else if (k >= 0x20 && k <= 0x7E) {
                size_t len = strlen(current_password);
                if (len < MAX_PASS_LEN) {
                    current_password[len] = (char)k;
                    current_password[len + 1] = '\0';
                    dirty = true;
                }
            }
            break;

        case CFG_STATE_READY_TO_SAVE:
            if (k == 0x09) { // TAB -> Loop back to AP selection
                ui_state = CFG_STATE_SCAN_SELECT;
                dirty = true;
            } else if (k == 0x0D) { // RETURN -> Save to Flash & boot
                flash_config_save(current_ssid, current_password);
                ui_state = CFG_STATE_EXIT;
                ui_initialized = false;
                full_redraw_needed = true;
                return CONFIG_RET_QUIT; // Exit (0)
            } else if (k == 0x1B) { // ESC -> Exit without saving
                ui_state = CFG_STATE_EXIT;
                ui_initialized = false;
                full_redraw_needed = true;
                return CONFIG_RET_QUIT; // Exit (0)
            }
            break;

        case CFG_STATE_EXIT:
        default:
            ui_initialized = false;
            full_redraw_needed = true;
            return CONFIG_RET_QUIT; // Exit (0)
    }

    if (dirty) {
        return build_screen_update_packet(screen_buffer_960);
    }

    return CONFIG_RET_SKIP; // No changes (2)
}
