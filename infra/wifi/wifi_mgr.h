/*
 * Copyright (c) 2026 a2picoAFP / a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef WIFI_MGR_H
#define WIFI_MGR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef A2PICO_HOST_TEST
#include <pico/cyw43_arch.h>
#endif

#define MAX_SCAN_APS            10
#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASS_MAX_LEN       63
#define WIFI_RECONNECT_INTERVAL_MS 5000

typedef struct {
    char     ssid[WIFI_SSID_MAX_LEN + 1];
    int16_t  rssi;
    uint32_t auth_mode;
} wifi_scan_ap_t;

typedef enum {
    WIFI_MGR_STATE_DISCONNECTED = 0,
    WIFI_MGR_STATE_CONNECTING,
    WIFI_MGR_STATE_CONNECTED,
    WIFI_MGR_STATE_CONFIG_WAIT,
} wifi_mgr_state_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize CYW43 / GPIO LED, load credentials via a2pico, start async connect if configured */
bool wifi_mgr_init(void);

/* Start async Wi-Fi connection using saved credentials */
bool wifi_mgr_connect(void);

/* Disconnect / reset state to disconnected */
void wifi_mgr_disconnect(void);

/* Check if Wi-Fi link is up and IP is ready */
bool wifi_mgr_is_connected(void);

/* Get current Wi-Fi manager state */
wifi_mgr_state_t wifi_mgr_get_state(void);

/* Background task (poll, LED update, 5000ms async reconnect when !config_active) */
void wifi_mgr_task(void);

/* Suppress or resume background auto-reconnect during config UI / AP scan */
void wifi_mgr_set_config_active(bool active);
bool wifi_mgr_is_config_active(void);

/* Credential helpers wrapping a2pico Flash API */
bool wifi_mgr_has_saved_credentials(void);
bool wifi_mgr_save_credentials(const char *ssid, const char *pass);
void wifi_mgr_delete_credentials(void);

/* Get current IP address as null-terminated string */
bool wifi_mgr_get_ip_str(char *buf, size_t max_len);

/* Get currently active or saved target SSID */
void wifi_mgr_get_ssid(char *buf, size_t max_len);

/* Start async scanning for nearby Wi-Fi APs */
bool wifi_mgr_start_scan(void);

/* Check if AP scan is currently active */
bool wifi_mgr_is_scanning(void);

/* Get count of scanned APs */
int wifi_mgr_get_scanned_aps_count(void);

/* Get scanned AP entry by index (0 .. count - 1) */
const wifi_scan_ap_t* wifi_mgr_get_scanned_ap(int index);

/* Update board LED pattern based on connection state */
void wifi_mgr_update_led(void);

#ifdef A2PICO_HOST_TEST
/* Host test helpers */
void wifi_mgr_test_reset(void);
void wifi_mgr_test_set_connected(bool connected, const char *ip_str);
void wifi_mgr_test_set_credentials(const char *ssid, const char *pass);
void wifi_mgr_test_set_scan_aps(const wifi_scan_ap_t *aps, int count);
void wifi_mgr_test_set_scanning(bool scanning);
int wifi_mgr_test_get_scan_start_count(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MGR_H */
