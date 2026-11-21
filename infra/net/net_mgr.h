/*
 * Copyright (c) 2026 a2picoAFP / a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef A2PICO_NET_MGR_H
#define A2PICO_NET_MGR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_WIFI_SSID_MAX_LEN 32
#define NET_WIFI_PASS_MAX_LEN 63

struct netif;

typedef enum {
    NET_TRANSPORT_NONE = 0,
    NET_TRANSPORT_WIFI,
    NET_TRANSPORT_ETHERNET,
    NET_TRANSPORT_USB_ECM,
} net_transport_t;

typedef enum {
    NET_MGR_STATE_DISCONNECTED = 0,
    NET_MGR_STATE_CONNECTING,
    NET_MGR_STATE_CONNECTED,
    NET_MGR_STATE_CONFIG_WAIT,
} net_mgr_state_t;

typedef struct {
    char     ssid[NET_WIFI_SSID_MAX_LEN + 1];
    int16_t  rssi;
    uint32_t auth_mode;
} net_wifi_ap_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Transport-agnostic lifecycle and link status */
bool net_mgr_init(void);
void net_mgr_task(void);
void net_mgr_poll(void);
bool net_mgr_is_connected(void);
net_mgr_state_t net_mgr_get_state(void);
net_transport_t net_mgr_get_transport(void);
bool net_mgr_get_ip_str(char *buf, size_t max_len);
struct netif *net_mgr_get_netif(void);

/* lwIP core synchronization abstraction */
void net_mgr_lwip_lock(void);
void net_mgr_lwip_unlock(void);

/* Optional Wi-Fi capability and delegated operations */
bool net_mgr_supports_wifi(void);
bool net_mgr_wifi_connect(void);
void net_mgr_wifi_disconnect(void);
void net_mgr_wifi_set_config_active(bool active);
bool net_mgr_wifi_is_config_active(void);
bool net_mgr_wifi_has_saved_credentials(void);
bool net_mgr_wifi_save_credentials(const char *ssid, const char *pass);
void net_mgr_wifi_delete_credentials(void);
void net_mgr_wifi_get_ssid(char *buf, size_t max_len);
bool net_mgr_wifi_start_scan(void);
bool net_mgr_wifi_is_scanning(void);
int  net_mgr_wifi_get_scanned_aps_count(void);
bool net_mgr_wifi_get_scanned_ap(int index, net_wifi_ap_t *out_ap);

#ifdef A2PICO_HOST_TEST
/* Host test helpers for transport mode simulation */
void net_mgr_test_set_transport(net_transport_t transport);
void net_mgr_test_set_connected(bool connected, const char *ip_str);
#endif

#ifdef __cplusplus
}
#endif

#endif /* A2PICO_NET_MGR_H */
