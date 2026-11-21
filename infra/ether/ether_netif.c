/*
 * Copyright (c) 2026 a2picoAFP / a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#include "ether_netif.h"
#include "w5500_drv.h"

#include <stdio.h>
#include <string.h>
#include <a2pico.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"

#include <lwip/dhcp.h>
#include <lwip/etharp.h>
#include <lwip/init.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/timeouts.h>
#include <netif/ethernet.h>

#define ETHER_FRAME_MAX_LEN     1536U
#define ETHER_RX_BUDGET_PER_POLL 8
#define ETHER_LINK_POLL_MS      100U

static struct netif s_eth_netif;
static uint8_t s_mac_addr[6];
static uint8_t s_tx_frame[ETHER_FRAME_MAX_LEN];
static uint8_t s_rx_frame[ETHER_FRAME_MAX_LEN];

static bool s_eth_initialized = false;
static bool s_link_up = false;
static uint32_t s_last_link_poll_ms = 0;

static void ether_set_led(bool on) {
    int pin = a2pico_led();
    if (pin >= 0) {
        gpio_put((uint)pin, on ? 1 : 0);
    }
}

static void ether_update_led(void) {
    if (ether_netif_is_connected()) {
        ether_set_led(true);
    } else if (s_link_up) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        ether_set_led((now % 1000U) < 150U);
    } else {
        ether_set_led(false);
    }
}

static void ether_generate_mac(uint8_t out_mac[6]) {
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);

    /* Locally administered unicast MAC: 02:xx:xx:xx:xx:xx */
    out_mac[0] = 0x02U;
    out_mac[1] = uid.id[3];
    out_mac[2] = uid.id[4];
    out_mac[3] = uid.id[5];
    out_mac[4] = uid.id[6];
    out_mac[5] = uid.id[7];
}

static err_t ether_linkoutput(struct netif *netif, struct pbuf *p) {
    (void)netif;
    if (!p || p->tot_len == 0 || p->tot_len > ETHER_FRAME_MAX_LEN) {
        return ERR_VAL;
    }

    uint16_t copied = pbuf_copy_partial(p, s_tx_frame, p->tot_len, 0);
    if (copied != p->tot_len) {
        return ERR_BUF;
    }

    return w5500_send_frame(s_tx_frame, copied) ? ERR_OK : ERR_IF;
}

static err_t ether_lwip_init_cb(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->output = etharp_output;
    netif->linkoutput = ether_linkoutput;
    netif->mtu = 1500;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, s_mac_addr, ETH_HWADDR_LEN);
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

bool ether_netif_init(void) {
    s_eth_initialized = false;
    s_link_up = false;
    s_last_link_poll_ms = 0;

    int led_pin = a2pico_led();
    if (led_pin >= 0) {
        gpio_init((uint)led_pin);
        gpio_set_dir((uint)led_pin, GPIO_OUT);
        gpio_put((uint)led_pin, 0);
    }

    ether_generate_mac(s_mac_addr);
    if (!w5500_init(s_mac_addr)) {
        printf("[Ether] W5500 initialization failed!\n");
        return false;
    }

    lwip_init();

    ip4_addr_t zero = IPADDR4_INIT(0);
    if (netif_add(&s_eth_netif, &zero, &zero, &zero, NULL,
                  ether_lwip_init_cb, ethernet_input) == NULL) {
        printf("[Ether] netif_add failed!\n");
        return false;
    }

    netif_set_default(&s_eth_netif);
    netif_set_up(&s_eth_netif);

    s_link_up = w5500_is_link_up();
    if (s_link_up) {
        netif_set_link_up(&s_eth_netif);
#if LWIP_DHCP
        dhcp_start(&s_eth_netif);
#endif
        printf("[Ether] PHY link UP, DHCP started\n");
    } else {
        netif_set_link_down(&s_eth_netif);
        printf("[Ether] PHY link DOWN, waiting for cable...\n");
    }

    s_eth_initialized = true;
    ether_update_led();
    return true;
}

void ether_netif_poll(void) {
    if (!s_eth_initialized) {
        return;
    }

    for (int i = 0; i < ETHER_RX_BUDGET_PER_POLL; i++) {
        uint16_t flen = w5500_recv_frame(s_rx_frame, ETHER_FRAME_MAX_LEN);
        if (flen == 0) {
            break;
        }
        struct pbuf *p = pbuf_alloc(PBUF_RAW, flen, PBUF_POOL);
        if (!p) {
            break;
        }
        if (pbuf_take(p, s_rx_frame, flen) != ERR_OK) {
            pbuf_free(p);
            continue;
        }
        if (s_eth_netif.input(p, &s_eth_netif) != ERR_OK) {
            pbuf_free(p);
        }
    }

    sys_check_timeouts();
}

void ether_netif_task(void) {
    if (!s_eth_initialized) {
        return;
    }

    ether_netif_poll();

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now - s_last_link_poll_ms) >= ETHER_LINK_POLL_MS) {
        s_last_link_poll_ms = now;
        bool cur_up = w5500_is_link_up();
        if (cur_up != s_link_up) {
            s_link_up = cur_up;
            if (s_link_up) {
                printf("[Ether] PHY link UP, starting DHCP\n");
                netif_set_link_up(&s_eth_netif);
#if LWIP_DHCP
                dhcp_start(&s_eth_netif);
#endif
            } else {
                printf("[Ether] PHY link DOWN\n");
#if LWIP_DHCP
                dhcp_stop(&s_eth_netif);
#endif
                ip4_addr_t zero = IPADDR4_INIT(0);
                netif_set_addr(&s_eth_netif, &zero, &zero, &zero);
                netif_set_link_down(&s_eth_netif);
            }
        }
    }

    ether_update_led();
}

bool ether_netif_is_link_up(void) {
    return s_eth_initialized && s_link_up;
}

bool ether_netif_is_connected(void) {
    if (!s_eth_initialized || !s_link_up) {
        return false;
    }
    return !ip4_addr_isany_val(*netif_ip4_addr(&s_eth_netif));
}

bool ether_netif_get_ip_str(char *buf, size_t max_len) {
    if (!buf || max_len == 0) {
        return false;
    }
    if (!ether_netif_is_connected()) {
        buf[0] = '\0';
        return false;
    }
    const char *s = ip4addr_ntoa(netif_ip4_addr(&s_eth_netif));
    if (!s) {
        buf[0] = '\0';
        return false;
    }
    snprintf(buf, max_len, "%s", s);
    return true;
}

struct netif *ether_netif_get_netif(void) {
    return s_eth_initialized ? &s_eth_netif : NULL;
}
