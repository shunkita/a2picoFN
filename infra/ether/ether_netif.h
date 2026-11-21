/*
 * Copyright (c) 2026 a2picoAFP / a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef A2PICO_ETHER_NETIF_H
#define A2PICO_ETHER_NETIF_H

#include <stdbool.h>
#include <stddef.h>

struct netif;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize W5500, lwIP stack, Ethernet netif, and DHCP */
bool ether_netif_init(void);

/* Drain W5500 RX frames into lwIP and run lwIP timers (safe inside sync waits) */
void ether_netif_poll(void);

/* Periodic Core 0 task: poll RX/timers, track PHY link state, update board LED */
void ether_netif_task(void);

/* Returns true when PHY link is up and a non-zero IPv4 address is assigned */
bool ether_netif_is_connected(void);

/* Returns true when PHY link is up (even if DHCP lease is still pending) */
bool ether_netif_is_link_up(void);

/* Copy current IPv4 address string into buf */
bool ether_netif_get_ip_str(char *buf, size_t max_len);

/* Return active lwIP Ethernet netif */
struct netif *ether_netif_get_netif(void);

#ifdef __cplusplus
}
#endif

#endif /* A2PICO_ETHER_NETIF_H */
