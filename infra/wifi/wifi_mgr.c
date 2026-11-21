/*
 * Copyright (c) 2026 a2picoAFP / a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "wifi_mgr.h"

#ifndef A2PICO_HOST_TEST
#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>
#include <lwip/dhcp.h>
#include <a2pico.h>
#ifndef CYW43_WL_GPIO_LED_PIN
#define CYW43_WL_GPIO_LED_PIN 0
#endif
#endif

static wifi_mgr_state_t mgr_state = WIFI_MGR_STATE_DISCONNECTED;
static int last_led_state = -1;
static bool config_active = false;
static bool radio_ready = false;

static wifi_scan_ap_t scanned_aps[MAX_SCAN_APS];
static int scanned_aps_count = 0;
static bool scan_in_progress = false;

#ifdef A2PICO_HOST_TEST

static char test_ssid[WIFI_SSID_MAX_LEN + 1] = "";
static char test_pass[WIFI_PASS_MAX_LEN + 1] = "";
static char test_ip[16] = "0.0.0.0";
static bool test_connected = false;
static int test_scan_start_count = 0;

void wifi_mgr_test_reset(void) {
    mgr_state = WIFI_MGR_STATE_DISCONNECTED;
    last_led_state = -1;
    config_active = false;
    radio_ready = true;
    scanned_aps_count = 0;
    scan_in_progress = false;
    test_scan_start_count = 0;
    test_ssid[0] = '\0';
    test_pass[0] = '\0';
    strncpy(test_ip, "0.0.0.0", sizeof(test_ip));
    test_connected = false;
}

void wifi_mgr_test_set_connected(bool connected, const char *ip_str) {
    test_connected = connected;
    if (connected) {
        mgr_state = WIFI_MGR_STATE_CONNECTED;
        if (ip_str) {
            strncpy(test_ip, ip_str, sizeof(test_ip) - 1);
            test_ip[sizeof(test_ip) - 1] = '\0';
        }
    } else {
        mgr_state = WIFI_MGR_STATE_DISCONNECTED;
        strncpy(test_ip, "0.0.0.0", sizeof(test_ip));
    }
}

void wifi_mgr_test_set_credentials(const char *ssid, const char *pass) {
    if (ssid && ssid[0] != '\0') {
        strncpy(test_ssid, ssid, WIFI_SSID_MAX_LEN);
        test_ssid[WIFI_SSID_MAX_LEN] = '\0';
        if (pass) {
            strncpy(test_pass, pass, WIFI_PASS_MAX_LEN);
            test_pass[WIFI_PASS_MAX_LEN] = '\0';
        } else {
            test_pass[0] = '\0';
        }
    } else {
        test_ssid[0] = '\0';
        test_pass[0] = '\0';
    }
}

void wifi_mgr_test_set_scan_aps(const wifi_scan_ap_t *aps, int count) {
    if (count < 0) count = 0;
    if (count > MAX_SCAN_APS) count = MAX_SCAN_APS;
    scanned_aps_count = count;
    for (int i = 0; i < count; i++) {
        scanned_aps[i] = aps[i];
    }
    scan_in_progress = false;
}

void wifi_mgr_test_set_scanning(bool scanning) {
    scan_in_progress = scanning;
}

int wifi_mgr_test_get_scan_start_count(void) {
    return test_scan_start_count;
}

bool wifi_mgr_init(void) {
    wifi_mgr_test_reset();
    return true;
}

bool wifi_mgr_connect(void) {
    if (test_ssid[0] == '\0') {
        mgr_state = WIFI_MGR_STATE_CONFIG_WAIT;
        return false;
    }
    /* Match hardware reconnect: drop previous link/IP until new link comes up */
    test_connected = false;
    strncpy(test_ip, "0.0.0.0", sizeof(test_ip));
    mgr_state = WIFI_MGR_STATE_CONNECTING;
    return true;
}

void wifi_mgr_disconnect(void) {
    test_connected = false;
    mgr_state = WIFI_MGR_STATE_DISCONNECTED;
}

bool wifi_mgr_has_saved_credentials(void) {
    return test_ssid[0] != '\0';
}

bool wifi_mgr_save_credentials(const char *ssid, const char *pass) {
    if (!ssid || ssid[0] == '\0') return false;
    wifi_mgr_test_set_credentials(ssid, pass);
    return true;
}

void wifi_mgr_delete_credentials(void) {
    test_ssid[0] = '\0';
    test_pass[0] = '\0';
}

bool wifi_mgr_get_ip_str(char *buf, size_t max_len) {
    if (!buf || max_len == 0) return false;
    strncpy(buf, test_connected ? test_ip : "0.0.0.0", max_len);
    buf[max_len - 1] = '\0';
    return test_connected;
}

void wifi_mgr_get_ssid(char *buf, size_t max_len) {
    if (!buf || max_len == 0) return;
    strncpy(buf, test_ssid, max_len);
    buf[max_len - 1] = '\0';
}

bool wifi_mgr_start_scan(void) {
    test_scan_start_count++;
    test_connected = false;
    mgr_state = WIFI_MGR_STATE_CONFIG_WAIT;
    scan_in_progress = false;
    return true;
}

bool wifi_mgr_is_scanning(void) {
    return scan_in_progress;
}

void wifi_mgr_update_led(void) {
}

void wifi_mgr_task(void) {
}

#else /* !A2PICO_HOST_TEST */

#define WIFI_SCAN_TIMEOUT_MS 8000U

static uint32_t last_reconnect_ms = 0;
static uint32_t scan_start_ms = 0;
static bool connect_attempted = false;

static void wifi_reset_link_and_dhcp(void) {
    (void)cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    cyw43_arch_lwip_begin();
    struct netif *netif = &cyw43_state.netif[CYW43_ITF_STA];
    netif_set_link_down(netif);
#if LWIP_DHCP
    dhcp_stop(netif);
#endif
#if LWIP_IPV4
    ip4_addr_t zero = IPADDR4_INIT(0);
    netif_set_addr(netif, &zero, &zero, &zero);
#endif
#if LWIP_DHCP
    dhcp_start(netif);
#endif
    cyw43_arch_lwip_end();
}

static inline void wifi_set_led(bool on) {
    if (a2pico_led() >= 0) {
        gpio_put(a2pico_led(), on ? 1 : 0);
    } else if (a2pico_radio()) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
    }
}

static int wifi_scan_callback(void *env, const cyw43_ev_scan_result_t *result) {
    (void)env;
    if (!result || result->ssid_len == 0) {
        return 0;
    }

    char name[WIFI_SSID_MAX_LEN + 1];
    size_t len = result->ssid_len < WIFI_SSID_MAX_LEN ? result->ssid_len : WIFI_SSID_MAX_LEN;
    memcpy(name, result->ssid, len);
    name[len] = '\0';

    for (int i = 0; i < scanned_aps_count; i++) {
        if (strcmp(scanned_aps[i].ssid, name) == 0) {
            if (result->rssi > scanned_aps[i].rssi) {
                scanned_aps[i].rssi = result->rssi;
            }
            return 0;
        }
    }

    if (scanned_aps_count < MAX_SCAN_APS) {
        strncpy(scanned_aps[scanned_aps_count].ssid, name, WIFI_SSID_MAX_LEN);
        scanned_aps[scanned_aps_count].ssid[WIFI_SSID_MAX_LEN] = '\0';
        scanned_aps[scanned_aps_count].rssi = result->rssi;
        scanned_aps[scanned_aps_count].auth_mode = result->auth_mode;
        scanned_aps_count++;
    }

    return 0;
}

static bool load_saved_credentials(const char **ssid, const char **pass) {
    const char *s = NULL;
    const char *p = NULL;
    if (a2pico_load_wifi_credentials(&s, &p) && s && s[0] != '\0') {
        *ssid = s;
        *pass = p ? p : "";
        return true;
    }
    *ssid = NULL;
    *pass = NULL;
    return false;
}

bool wifi_mgr_init(void) {
    last_led_state = -1;
    mgr_state = WIFI_MGR_STATE_DISCONNECTED;
    config_active = false;
    radio_ready = false;
    scanned_aps_count = 0;
    scan_in_progress = false;
    last_reconnect_ms = 0;
    connect_attempted = false;

    if (a2pico_led() >= 0) {
        gpio_init(a2pico_led());
        gpio_set_dir(a2pico_led(), GPIO_OUT);
        gpio_put(a2pico_led(), 0);
    }

    if (!a2pico_radio()) {
        return false;
    }

    if (cyw43_arch_init() != 0) {
        return false;
    }

    if (a2pico_led() < 0) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    }
    cyw43_arch_enable_sta_mode();
#if LWIP_NETIF_HOSTNAME
#ifndef A2PICO_NET_HOSTNAME
#define A2PICO_NET_HOSTNAME "a2pico"
#endif
    cyw43_arch_lwip_begin();
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], A2PICO_NET_HOSTNAME);
    cyw43_arch_lwip_end();
#endif
    radio_ready = true;

    const char *ssid = NULL;
    const char *pass = NULL;
    if (!load_saved_credentials(&ssid, &pass)) {
        mgr_state = WIFI_MGR_STATE_CONFIG_WAIT;
        wifi_mgr_update_led();
        return true;
    }

    wifi_mgr_connect();
    return true;
}

bool wifi_mgr_connect(void) {
    if (!radio_ready) {
        return false;
    }

    const char *ssid = NULL;
    const char *pass = NULL;
    if (!load_saved_credentials(&ssid, &pass)) {
        mgr_state = WIFI_MGR_STATE_CONFIG_WAIT;
        wifi_mgr_update_led();
        return false;
    }

    config_active = false;
    mgr_state = WIFI_MGR_STATE_CONNECTING;
    last_reconnect_ms = to_ms_since_boot(get_absolute_time());
    wifi_mgr_update_led();

    /*
     * On reconnect, explicitly drop any previous STA association and clear
     * lwIP link/DHCP/IPv4 state so cyw43_tcpip_link_status() cannot report a
     * stale CYW43_LINK_UP before the new link and DHCP lease are established.
     */
    if (connect_attempted) {
        wifi_reset_link_and_dhcp();
    }
    connect_attempted = true;

    int rc = cyw43_arch_wifi_connect_async(
        ssid,
        pass,
        CYW43_AUTH_WPA2_MIXED_PSK
    );

    return (rc == 0);
}

void wifi_mgr_disconnect(void) {
    mgr_state = WIFI_MGR_STATE_DISCONNECTED;
    wifi_mgr_update_led();
}

bool wifi_mgr_has_saved_credentials(void) {
    const char *ssid = NULL;
    const char *pass = NULL;
    return load_saved_credentials(&ssid, &pass);
}

bool wifi_mgr_save_credentials(const char *ssid, const char *pass) {
    if (!ssid || ssid[0] == '\0') {
        return false;
    }
    char safe_ssid[WIFI_SSID_MAX_LEN + 1];
    char safe_pass[WIFI_PASS_MAX_LEN + 1];
    strncpy(safe_ssid, ssid, WIFI_SSID_MAX_LEN);
    safe_ssid[WIFI_SSID_MAX_LEN] = '\0';
    if (pass) {
        strncpy(safe_pass, pass, WIFI_PASS_MAX_LEN);
        safe_pass[WIFI_PASS_MAX_LEN] = '\0';
    } else {
        safe_pass[0] = '\0';
    }
    return a2pico_save_wifi_credentials(safe_ssid, safe_pass);
}

void wifi_mgr_delete_credentials(void) {
    a2pico_delete_wifi_credentials();
}

bool wifi_mgr_get_ip_str(char *buf, size_t max_len) {
    if (!buf || max_len == 0) return false;

    if (mgr_state == WIFI_MGR_STATE_CONNECTED) {
        ip4addr_ntoa_r(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]), buf, (int)max_len);
        return true;
    }

    strncpy(buf, "0.0.0.0", max_len);
    buf[max_len - 1] = '\0';
    return false;
}

void wifi_mgr_get_ssid(char *buf, size_t max_len) {
    if (!buf || max_len == 0) return;

    const char *ssid = NULL;
    const char *pass = NULL;
    if (load_saved_credentials(&ssid, &pass)) {
        strncpy(buf, ssid, max_len);
    } else {
        buf[0] = '\0';
    }
    buf[max_len - 1] = '\0';
}

bool wifi_mgr_start_scan(void) {
    if (!radio_ready) {
        return false;
    }

    /*
     * Stop any in-progress async join or active STA association before escan.
     * On CYW43, issuing cyw43_wifi_scan() while cyw43_wifi_join() is active
     * can abort escan without a status=0 completion event, leaving
     * cyw43_state.wifi_scan_state stuck at 1 permanently.
     */
    if (connect_attempted) {
        wifi_reset_link_and_dhcp();
    }

    cyw43_arch_lwip_begin();
    cyw43_state.wifi_scan_state = 0;
    cyw43_arch_lwip_end();

    scanned_aps_count = 0;
    scan_in_progress = true;
    scan_start_ms = to_ms_since_boot(get_absolute_time());

    cyw43_wifi_scan_options_t scan_options = {0};
    int rc = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, wifi_scan_callback);
    if (rc != 0) {
        cyw43_arch_lwip_begin();
        cyw43_state.wifi_scan_state = 0;
        cyw43_arch_lwip_end();
        scan_in_progress = false;
        return false;
    }

    return true;
}

bool wifi_mgr_is_scanning(void) {
    if (scan_in_progress) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (!cyw43_wifi_scan_active(&cyw43_state)) {
            scan_in_progress = false;
        } else if ((uint32_t)(now - scan_start_ms) >= WIFI_SCAN_TIMEOUT_MS) {
            cyw43_arch_lwip_begin();
            cyw43_state.wifi_scan_state = 0;
            cyw43_arch_lwip_end();
            scan_in_progress = false;
        }
    }
    return scan_in_progress;
}

void wifi_mgr_update_led(void) {
    int led_on = 0;

    switch (mgr_state) {
        case WIFI_MGR_STATE_CONNECTED:
            led_on = 1;
            break;

        case WIFI_MGR_STATE_CONNECTING:
        case WIFI_MGR_STATE_CONFIG_WAIT: {
            uint32_t phase = to_ms_since_boot(get_absolute_time()) % 1000U;
            led_on = (phase < 150U) ? 1 : 0;
            break;
        }

        case WIFI_MGR_STATE_DISCONNECTED:
        default:
            led_on = 0;
            break;
    }

    if (led_on != last_led_state) {
        wifi_set_led(led_on != 0);
        last_led_state = led_on;
    }
}

void wifi_mgr_task(void) {
    if (!radio_ready) {
        return;
    }

    cyw43_arch_poll();

    (void)wifi_mgr_is_scanning();

    if (config_active) {
        mgr_state = WIFI_MGR_STATE_CONFIG_WAIT;
        wifi_mgr_update_led();
        return;
    }

    int link_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link_status == CYW43_LINK_UP) {
        mgr_state = WIFI_MGR_STATE_CONNECTED;
    } else {
        if (mgr_state == WIFI_MGR_STATE_CONNECTED) {
            mgr_state = WIFI_MGR_STATE_DISCONNECTED;
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((uint32_t)(now - last_reconnect_ms) >= WIFI_RECONNECT_INTERVAL_MS) {
            const char *ssid = NULL;
            const char *pass = NULL;
            if (load_saved_credentials(&ssid, &pass)) {
                last_reconnect_ms = now;
                mgr_state = WIFI_MGR_STATE_CONNECTING;
                cyw43_arch_wifi_connect_async(
                    ssid,
                    pass,
                    CYW43_AUTH_WPA2_MIXED_PSK
                );
            } else {
                mgr_state = WIFI_MGR_STATE_CONFIG_WAIT;
            }
        }
    }

    wifi_mgr_update_led();
}

#endif /* A2PICO_HOST_TEST */

bool wifi_mgr_is_connected(void) {
    return (mgr_state == WIFI_MGR_STATE_CONNECTED);
}

wifi_mgr_state_t wifi_mgr_get_state(void) {
    return mgr_state;
}

void wifi_mgr_set_config_active(bool active) {
    config_active = active;
}

bool wifi_mgr_is_config_active(void) {
    return config_active;
}

int wifi_mgr_get_scanned_aps_count(void) {
    return scanned_aps_count;
}

const wifi_scan_ap_t* wifi_mgr_get_scanned_ap(int index) {
    if (index >= 0 && index < scanned_aps_count) {
        return &scanned_aps[index];
    }
    return NULL;
}
