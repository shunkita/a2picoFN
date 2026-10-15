#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <pico/cyw43_arch.h>

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef WIFI_AUTH_TYPE

#define WIFI_AUTH_TYPE CYW43_AUTH_WPA2_MIXED_PSK
#endif

#ifndef FUJINET_PC_IP
#define FUJINET_PC_IP ""
#endif

#ifndef FUJINET_PC_PORT
#define FUJINET_PC_PORT 1985
#endif

#endif
