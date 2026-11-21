/*
 * Copyright (c) 2026 a2picoAFP / a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#include "net_mgr.h"
#include <string.h>

#if defined(A2PICO_HOST_TEST) || defined(A2PICO_NET_BACKEND_WIFI)
#include "wifi_mgr.h"
#elif defined(A2PICO_NET_BACKEND_ETHER)
#include "ether_netif.h"
#elif defined(A2PICO_NET_BACKEND_USB_ECM)
#include "usb_ecm_netif.h"
#endif

#if !defined(A2PICO_HOST_TEST)
#include <lwip/netif.h>
#if defined(A2PICO_NET_BACKEND_WIFI)
#include <pico/cyw43_arch.h>
#endif
#endif

#if defined(A2PICO_HOST_TEST) || defined(A2PICO_NET_BACKEND_WIFI)

#ifdef A2PICO_HOST_TEST
static net_transport_t s_test_transport = NET_TRANSPORT_WIFI;

void net_mgr_test_set_transport(net_transport_t transport) {
    s_test_transport = transport;
}

void net_mgr_test_set_connected(bool connected, const char *ip_str) {
    wifi_mgr_test_set_connected(connected, ip_str);
}
#endif

static net_mgr_state_t map_wifi_state(wifi_mgr_state_t st) {
    switch (st) {
        case WIFI_MGR_STATE_CONNECTING:
            return NET_MGR_STATE_CONNECTING;
        case WIFI_MGR_STATE_CONNECTED:
            return NET_MGR_STATE_CONNECTED;
        case WIFI_MGR_STATE_CONFIG_WAIT:
            return NET_MGR_STATE_CONFIG_WAIT;
        case WIFI_MGR_STATE_DISCONNECTED:
        default:
            return NET_MGR_STATE_DISCONNECTED;
    }
}

bool net_mgr_init(void) {
    return wifi_mgr_init();
}

void net_mgr_task(void) {
    wifi_mgr_task();
}

void net_mgr_poll(void) {
    /* Wi-Fi uses threadsafe_background; no extra polling required */
}

bool net_mgr_is_connected(void) {
    return wifi_mgr_is_connected();
}

net_mgr_state_t net_mgr_get_state(void) {
    return map_wifi_state(wifi_mgr_get_state());
}

net_transport_t net_mgr_get_transport(void) {
#ifdef A2PICO_HOST_TEST
    return s_test_transport;
#else
    return NET_TRANSPORT_WIFI;
#endif
}

bool net_mgr_get_ip_str(char *buf, size_t max_len) {
    return wifi_mgr_get_ip_str(buf, max_len);
}

struct netif *net_mgr_get_netif(void) {
#if !defined(A2PICO_HOST_TEST)
    return netif_default;
#else
    return NULL;
#endif
}

void net_mgr_lwip_lock(void) {
#if !defined(A2PICO_HOST_TEST)
    cyw43_arch_lwip_begin();
#endif
}

void net_mgr_lwip_unlock(void) {
#if !defined(A2PICO_HOST_TEST)
    cyw43_arch_lwip_end();
#endif
}

bool net_mgr_supports_wifi(void) {
#ifdef A2PICO_HOST_TEST
    return s_test_transport == NET_TRANSPORT_WIFI;
#else
    return true;
#endif
}

bool net_mgr_wifi_connect(void) {
    return wifi_mgr_connect();
}

void net_mgr_wifi_disconnect(void) {
    wifi_mgr_disconnect();
}

void net_mgr_wifi_set_config_active(bool active) {
    wifi_mgr_set_config_active(active);
}

bool net_mgr_wifi_is_config_active(void) {
    return wifi_mgr_is_config_active();
}

bool net_mgr_wifi_has_saved_credentials(void) {
    return wifi_mgr_has_saved_credentials();
}

bool net_mgr_wifi_save_credentials(const char *ssid, const char *pass) {
    return wifi_mgr_save_credentials(ssid, pass);
}

void net_mgr_wifi_delete_credentials(void) {
    wifi_mgr_delete_credentials();
}

void net_mgr_wifi_get_ssid(char *buf, size_t max_len) {
    wifi_mgr_get_ssid(buf, max_len);
}

bool net_mgr_wifi_start_scan(void) {
    return wifi_mgr_start_scan();
}

bool net_mgr_wifi_is_scanning(void) {
    return wifi_mgr_is_scanning();
}

int net_mgr_wifi_get_scanned_aps_count(void) {
    return wifi_mgr_get_scanned_aps_count();
}

bool net_mgr_wifi_get_scanned_ap(int index, net_wifi_ap_t *out_ap) {
    if (!out_ap) {
        return false;
    }
    const wifi_scan_ap_t *ap = wifi_mgr_get_scanned_ap(index);
    if (!ap) {
        return false;
    }
    memset(out_ap, 0, sizeof(*out_ap));
    strncpy(out_ap->ssid, ap->ssid, NET_WIFI_SSID_MAX_LEN);
    out_ap->ssid[NET_WIFI_SSID_MAX_LEN] = '\0';
    out_ap->rssi = ap->rssi;
    out_ap->auth_mode = ap->auth_mode;
    return true;
}

#elif defined(A2PICO_NET_BACKEND_ETHER)

bool net_mgr_init(void) {
    return ether_netif_init();
}

void net_mgr_task(void) {
    ether_netif_task();
}

void net_mgr_poll(void) {
    ether_netif_poll();
}

bool net_mgr_is_connected(void) {
    return ether_netif_is_connected();
}

net_mgr_state_t net_mgr_get_state(void) {
    if (ether_netif_is_connected()) {
        return NET_MGR_STATE_CONNECTED;
    }
    if (ether_netif_is_link_up()) {
        return NET_MGR_STATE_CONNECTING;
    }
    return NET_MGR_STATE_DISCONNECTED;
}

net_transport_t net_mgr_get_transport(void) {
    return NET_TRANSPORT_ETHERNET;
}

bool net_mgr_get_ip_str(char *buf, size_t max_len) {
    return ether_netif_get_ip_str(buf, max_len);
}

struct netif *net_mgr_get_netif(void) {
    return ether_netif_get_netif();
}

void net_mgr_lwip_lock(void) {}
void net_mgr_lwip_unlock(void) {}
bool net_mgr_supports_wifi(void) { return false; }
bool net_mgr_wifi_connect(void) { return false; }
void net_mgr_wifi_disconnect(void) {}
void net_mgr_wifi_set_config_active(bool active) { (void)active; }
bool net_mgr_wifi_is_config_active(void) { return false; }
bool net_mgr_wifi_has_saved_credentials(void) { return false; }
bool net_mgr_wifi_save_credentials(const char *ssid, const char *pass) {
    (void)ssid;
    (void)pass;
    return false;
}
void net_mgr_wifi_delete_credentials(void) {}
void net_mgr_wifi_get_ssid(char *buf, size_t max_len) {
    if (buf && max_len > 0) {
        buf[0] = '\0';
    }
}
bool net_mgr_wifi_start_scan(void) { return false; }
bool net_mgr_wifi_is_scanning(void) { return false; }
int net_mgr_wifi_get_scanned_aps_count(void) { return 0; }
bool net_mgr_wifi_get_scanned_ap(int index, net_wifi_ap_t *out_ap) {
    (void)index;
    (void)out_ap;
    return false;
}

#elif defined(A2PICO_NET_BACKEND_USB_ECM)

bool net_mgr_init(void) {
    return usb_ecm_netif_init();
}

void net_mgr_task(void) {
    usb_ecm_netif_task();
}

void net_mgr_poll(void) {
    usb_ecm_netif_poll();
}

bool net_mgr_is_connected(void) {
    return usb_ecm_netif_is_connected();
}

net_mgr_state_t net_mgr_get_state(void) {
    if (usb_ecm_netif_is_connected()) {
        return NET_MGR_STATE_CONNECTED;
    }
    if (usb_ecm_netif_is_link_up()) {
        return NET_MGR_STATE_CONNECTING;
    }
    return NET_MGR_STATE_DISCONNECTED;
}

net_transport_t net_mgr_get_transport(void) {
    return NET_TRANSPORT_USB_ECM;
}

bool net_mgr_get_ip_str(char *buf, size_t max_len) {
    return usb_ecm_netif_get_ip_str(buf, max_len);
}

struct netif *net_mgr_get_netif(void) {
    return usb_ecm_netif_get_netif();
}

void net_mgr_lwip_lock(void) {}
void net_mgr_lwip_unlock(void) {}
bool net_mgr_supports_wifi(void) { return false; }
bool net_mgr_wifi_connect(void) { return false; }
void net_mgr_wifi_disconnect(void) {}
void net_mgr_wifi_set_config_active(bool active) { (void)active; }
bool net_mgr_wifi_is_config_active(void) { return false; }
bool net_mgr_wifi_has_saved_credentials(void) { return false; }
bool net_mgr_wifi_save_credentials(const char *ssid, const char *pass) {
    (void)ssid;
    (void)pass;
    return false;
}
void net_mgr_wifi_delete_credentials(void) {}
void net_mgr_wifi_get_ssid(char *buf, size_t max_len) {
    if (buf && max_len > 0) {
        buf[0] = '\0';
    }
}
bool net_mgr_wifi_start_scan(void) { return false; }
bool net_mgr_wifi_is_scanning(void) { return false; }
int net_mgr_wifi_get_scanned_aps_count(void) { return 0; }
bool net_mgr_wifi_get_scanned_ap(int index, net_wifi_ap_t *out_ap) {
    (void)index;
    (void)out_ap;
    return false;
}

#else
#error "Define A2PICO_NET_BACKEND_WIFI, A2PICO_NET_BACKEND_ETHER, A2PICO_NET_BACKEND_USB_ECM, or A2PICO_HOST_TEST"
#endif
