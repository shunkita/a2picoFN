/*
 * Copyright (c) 2026 a2picoAFP / a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef A2PICO_USB_ECM_NETIF_H
#define A2PICO_USB_ECM_NETIF_H

#include <stdbool.h>
#include <stddef.h>

struct netif;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize TinyUSB CDC-ECM device stack, lwIP, and static link-local netif */
bool usb_ecm_netif_init(void);

/* Service TinyUSB device events and lwIP timers (safe inside sync waits) */
void usb_ecm_netif_poll(void);

/* Periodic Core 0 task: poll TinyUSB/lwIP, track USB ready state, update board LED */
void usb_ecm_netif_task(void);

/* Returns true when USB CDC-ECM host enumeration is active and static IP is ready */
bool usb_ecm_netif_is_connected(void);

/* Returns true when USB device link is up */
bool usb_ecm_netif_is_link_up(void);

/* Copy current IPv4 address string into buf */
bool usb_ecm_netif_get_ip_str(char *buf, size_t max_len);

/* Return active lwIP USB-ECM netif */
struct netif *usb_ecm_netif_get_netif(void);

#ifdef __cplusplus
}
#endif

#endif /* A2PICO_USB_ECM_NETIF_H */
