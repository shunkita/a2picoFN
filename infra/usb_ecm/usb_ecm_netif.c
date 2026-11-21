/*
 * Copyright (c) 2026 a2picoAFP / a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#include "usb_ecm_netif.h"

#include <stdio.h>
#include <string.h>
#include <a2pico.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"
#include <tusb.h>

#include <lwip/etharp.h>
#include <lwip/init.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/timeouts.h>
#include <netif/ethernet.h>

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT        0
#endif

#define USB_ECM_FRAME_MAX_LEN   1536U
#define USB_ECM_FIXED_IP_STR    "169.254.19.85"

/*
 * Locally Administered Unicast MAC Addresses (LAA):
 * - Device MAC (lwIP / Pico side):    02:xx:xx:xx:xx:01
 * - Host MAC   (PC/Mac side via ECM): 02:xx:xx:xx:xx:02
 */
uint8_t tud_network_mac_address[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static uint8_t s_host_mac_addr[6]  = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
static char s_serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1] = {0};

static struct netif s_ecm_netif;
static uint8_t s_tx_frame[USB_ECM_FRAME_MAX_LEN];
static bool s_ecm_initialized = false;
static bool s_link_up = false;

static void usb_ecm_set_led(bool on) {
    int pin = a2pico_led();
    if (pin >= 0) {
        gpio_put((uint)pin, on ? 1 : 0);
    }
}

static void usb_ecm_update_led(void) {
    usb_ecm_set_led(s_ecm_initialized && s_link_up);
}

static void init_mac_addresses(void) {
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    pico_get_unique_board_id_string(s_serial_str, sizeof(s_serial_str));

    tud_network_mac_address[0] = 0x02U;
    tud_network_mac_address[1] = uid.id[4];
    tud_network_mac_address[2] = uid.id[5];
    tud_network_mac_address[3] = uid.id[6];
    tud_network_mac_address[4] = uid.id[7];
    tud_network_mac_address[5] = 0x01U;

    s_host_mac_addr[0] = 0x02U;
    s_host_mac_addr[1] = uid.id[4];
    s_host_mac_addr[2] = uid.id[5];
    s_host_mac_addr[3] = uid.id[6];
    s_host_mac_addr[4] = uid.id[7];
    s_host_mac_addr[5] = 0x02U;
}

/*
 * -----------------------------------------------------------------------------
 * USB Descriptors (CDC-ECM)
 * -----------------------------------------------------------------------------
 */

#define USB_VID 0x6502
#define USB_PID 0x4020

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_INTERFACE,
    STRID_MAC,
    STRID_COUNT
};

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define EPNUM_NET_NOTIF 0x81
#define EPNUM_NET_OUT   0x02
#define EPNUM_NET_IN    0x82

static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_ECM_DESC_LEN)

static uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_ECM_DESCRIPTOR(ITF_NUM_CDC, STRID_INTERFACE, STRID_MAC,
                           EPNUM_NET_NOTIF, 64,
                           EPNUM_NET_OUT, EPNUM_NET_IN,
                           CFG_TUD_NET_ENDPOINT_SIZE, CFG_TUD_NET_MTU),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_fs_configuration;
}

static const char *const string_desc_arr[] = {
    [STRID_LANGID]       = (const char[]){0x09, 0x04},
    [STRID_MANUFACTURER] = "a2picoFN",
    [STRID_PRODUCT]      = "a2picoFN Virtual Ethernet",
    [STRID_SERIAL]       = s_serial_str,
    [STRID_INTERFACE]    = "a2picoFN CDC-ECM Network",
};

static uint16_t s_desc_str_buf[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    unsigned int chr_count = 0;

    if (index == STRID_LANGID) {
        memcpy(&s_desc_str_buf[1], string_desc_arr[STRID_LANGID], 2);
        chr_count = 1;
    } else if (index == STRID_MAC) {
        const char hex_chars[] = "0123456789ABCDEF";
        for (int i = 0; i < 6; i++) {
            s_desc_str_buf[1 + chr_count++] = hex_chars[(s_host_mac_addr[i] >> 4) & 0x0F];
            s_desc_str_buf[1 + chr_count++] = hex_chars[s_host_mac_addr[i] & 0x0F];
        }
    } else {
        if (index >= STRID_COUNT) return NULL;
        const char *str = string_desc_arr[index];
        if (!str) return NULL;

        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (unsigned int i = 0; i < chr_count; i++) {
            s_desc_str_buf[1 + i] = (uint16_t)str[i];
        }
    }

    s_desc_str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return s_desc_str_buf;
}

/*
 * -----------------------------------------------------------------------------
 * TinyUSB Network Class Callbacks
 * -----------------------------------------------------------------------------
 */

void tud_network_init_cb(void) {
}

// TinyUSB ecm_rndis_device.c unconditionally references rndis_class_set_handler.
void rndis_class_set_handler(uint8_t *data, int size) {
    (void)data;
    (void)size;
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (s_ecm_initialized && s_link_up && src && size >= 14 && size <= USB_ECM_FRAME_MAX_LEN) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (p) {
            if (pbuf_take(p, src, size) == ERR_OK) {
                if (s_ecm_netif.input(p, &s_ecm_netif) != ERR_OK) {
                    pbuf_free(p);
                }
            } else {
                pbuf_free(p);
            }
        }
    }
    tud_network_recv_renew();
    return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    memcpy(dst, ref, arg);
    return arg;
}

/*
 * -----------------------------------------------------------------------------
 * lwIP netif Integration
 * -----------------------------------------------------------------------------
 */

static err_t usb_ecm_linkoutput(struct netif *netif, struct pbuf *p) {
    (void)netif;
    if (!s_ecm_initialized || !s_link_up || !tud_ready()) {
        return ERR_IF;
    }
    if (!p || p->tot_len == 0 || p->tot_len > USB_ECM_FRAME_MAX_LEN) {
        return ERR_VAL;
    }
    if (!tud_network_can_xmit(p->tot_len)) {
        return ERR_MEM;
    }

    uint16_t copied = pbuf_copy_partial(p, s_tx_frame, p->tot_len, 0);
    if (copied != p->tot_len) {
        return ERR_BUF;
    }

    tud_network_xmit((void *)s_tx_frame, copied);
    return ERR_OK;
}

static err_t usb_ecm_lwip_init_cb(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = 'c';
    netif->output = etharp_output;
    netif->linkoutput = usb_ecm_linkoutput;
    netif->mtu = 1500;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, tud_network_mac_address, ETH_HWADDR_LEN);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
                   NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;

#if LWIP_NETIF_HOSTNAME
#ifndef A2PICO_NET_HOSTNAME
#define A2PICO_NET_HOSTNAME "a2pico"
#endif
    netif_set_hostname(netif, A2PICO_NET_HOSTNAME);
#endif

    return ERR_OK;
}

bool usb_ecm_netif_init(void) {
    s_ecm_initialized = false;
    s_link_up = false;

    int led_pin = a2pico_led();
    if (led_pin >= 0) {
        gpio_init((uint)led_pin);
        gpio_set_dir((uint)led_pin, GPIO_OUT);
        gpio_put((uint)led_pin, 0);
    }

    init_mac_addresses();
    tud_init(BOARD_TUD_RHPORT);

    lwip_init();

    ip4_addr_t ip, netmask, gw;
    IP4_ADDR(&ip, 169, 254, 19, 85);
    IP4_ADDR(&netmask, 255, 255, 0, 0);
    IP4_ADDR(&gw, 0, 0, 0, 0);

    if (netif_add(&s_ecm_netif, &ip, &netmask, &gw, NULL,
                  usb_ecm_lwip_init_cb, ethernet_input) == NULL) {
        return false;
    }

    netif_set_default(&s_ecm_netif);
    netif_set_up(&s_ecm_netif);
    netif_set_link_down(&s_ecm_netif);

    s_ecm_initialized = true;
    usb_ecm_update_led();
    return true;
}

void usb_ecm_netif_poll(void) {
    if (!s_ecm_initialized) {
        return;
    }
    tud_task();
    sys_check_timeouts();
}

void usb_ecm_netif_task(void) {
    if (!s_ecm_initialized) {
        return;
    }

    usb_ecm_netif_poll();

    bool cur_up = tud_ready();
    if (cur_up != s_link_up) {
        s_link_up = cur_up;
        if (s_link_up) {
            netif_set_link_up(&s_ecm_netif);
        } else {
            netif_set_link_down(&s_ecm_netif);
        }
        usb_ecm_update_led();
    }
}

bool usb_ecm_netif_is_link_up(void) {
    return s_ecm_initialized && s_link_up;
}

bool usb_ecm_netif_is_connected(void) {
    return s_ecm_initialized && s_link_up;
}

bool usb_ecm_netif_get_ip_str(char *buf, size_t max_len) {
    if (!buf || max_len == 0) {
        return false;
    }
    if (!usb_ecm_netif_is_connected()) {
        buf[0] = '\0';
        return false;
    }
    snprintf(buf, max_len, "%s", USB_ECM_FIXED_IP_STR);
    return true;
}

struct netif *usb_ecm_netif_get_netif(void) {
    return s_ecm_initialized ? &s_ecm_netif : NULL;
}
