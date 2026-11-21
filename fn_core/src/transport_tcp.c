/*
 * Copyright (c) 2026 a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <pico/stdlib.h>
#include <hardware/sync.h>
#include <lwip/tcp.h>

#include "fn_config.h"
#include "net_mgr.h"
#include "transport.h"

#define RX_BUF_SIZE 4096
#define TX_BUF_SIZE 2048

typedef enum {
    STATE_DISCONNECTED = 0,
    STATE_NET_CONNECTED_LISTENING,
    STATE_CLIENT_CONNECTED,
} transport_state_t;

volatile bool transport_connected = false;
volatile bool config_required = false;
char net_ip_address_str[16] = "";
volatile bool net_ip_ready = false;

static volatile transport_state_t state = STATE_DISCONNECTED;
static struct tcp_pcb *server_pcb = NULL;
static struct tcp_pcb *client_pcb = NULL;

// RX FIFO (SPSC: lwIP callback writes head, Core 0 main loop reads tail)
static uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

// TX Buffer (Core 0 main loop only)
static uint8_t tx_buf[TX_BUF_SIZE];
static uint16_t tx_len = 0;

static void rx_push_byte(uint8_t byte) {
    uint16_t head = rx_head;
    uint16_t next_head = (head + 1) & (RX_BUF_SIZE - 1);
    if (next_head != rx_tail) {
        rx_buf[head] = byte;
        __dmb();
        rx_head = next_head;
    }
}

static void on_tcp_error(void *arg, err_t err) {
    (void)arg;
    (void)err;
    client_pcb = NULL; // lwIP automatically frees the PCB on error
    transport_connected = false;
    __dmb();
    state = STATE_NET_CONNECTED_LISTENING;
}

static err_t on_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)arg;
    if (p == NULL) {
        // Client closed connection (FIN)
        if (client_pcb) {
            tcp_arg(client_pcb, NULL);
            tcp_recv(client_pcb, NULL);
            tcp_err(client_pcb, NULL);
            tcp_close(client_pcb);
            client_pcb = NULL;
        }
        transport_connected = false;
        __dmb();
        state = STATE_NET_CONNECTED_LISTENING;
        return ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

    // Push payload into RX FIFO
    struct pbuf *curr = p;
    while (curr != NULL) {
        const uint8_t *payload = (const uint8_t *)curr->payload;
        for (u16_t i = 0; i < curr->len; i++) {
            rx_push_byte(payload[i]);
        }
        curr = curr->next;
    }

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t on_tcp_accept(void *arg, struct tcp_pcb *new_pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || new_pcb == NULL) {
        return ERR_VAL;
    }

    // Close any previous client PCB
    if (client_pcb) {
        tcp_arg(client_pcb, NULL);
        tcp_recv(client_pcb, NULL);
        tcp_err(client_pcb, NULL);
        tcp_close(client_pcb);
        client_pcb = NULL;
    }

    client_pcb = new_pcb;
    tcp_arg(client_pcb, NULL);
    tcp_recv(client_pcb, on_tcp_recv);
    tcp_err(client_pcb, on_tcp_error);

    // Disable Nagle algorithm to minimize latency
    tcp_nagle_disable(client_pcb);

    state = STATE_CLIENT_CONNECTED;
    transport_connected = true;
    __dmb();

    return ERR_OK;
}

static void start_tcp_listener(void) {
    net_mgr_lwip_lock();
    if (server_pcb) {
        net_mgr_lwip_unlock();
        return;
    }

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        net_mgr_lwip_unlock();
        return;
    }

    err_t err = tcp_bind(pcb, IP_ADDR_ANY, FUJINET_PC_PORT);
    if (err != ERR_OK) {
        tcp_close(pcb);
        net_mgr_lwip_unlock();
        return;
    }

    pcb = tcp_listen_with_backlog(pcb, 1);
    if (!pcb) {
        net_mgr_lwip_unlock();
        return;
    }

    server_pcb = pcb;
    tcp_accept(server_pcb, on_tcp_accept);
    state = STATE_NET_CONNECTED_LISTENING;
    net_mgr_lwip_unlock();
}

void transport_init(void) {
    rx_head = rx_tail = 0;
    tx_len = 0;
    server_pcb = NULL;
    client_pcb = NULL;
    transport_connected = false;
    config_required = false;
    net_ip_ready = false;
    net_ip_address_str[0] = '\0';
    state = STATE_DISCONNECTED;
    __dmb();

    if (!net_mgr_init()) {
        return;
    }

    if (net_mgr_get_state() == NET_MGR_STATE_CONFIG_WAIT) {
        config_required = true;
        net_mgr_wifi_set_config_active(true);
        __dmb();
    }
}

bool transport_start_wifi(void) {
    config_required = false;
    __dmb();

    if (!net_mgr_supports_wifi()) {
        return true;
    }

    net_mgr_wifi_set_config_active(false);
    if (!net_mgr_wifi_connect()) {
        if (net_mgr_get_state() == NET_MGR_STATE_CONFIG_WAIT) {
            config_required = true;
            net_mgr_wifi_set_config_active(true);
            __dmb();
        }
        return false;
    }
    return true;
}

void transport_task(void) {
    net_mgr_task();

    if (config_required) {
        return;
    }

    if (net_mgr_is_connected()) {
        if (!net_ip_ready) {
            if (net_mgr_get_ip_str(net_ip_address_str, sizeof(net_ip_address_str))) {
                __dmb();
                net_ip_ready = true;
                __dmb();
                if (!server_pcb) {
                    start_tcp_listener();
                } else {
                    state = STATE_NET_CONNECTED_LISTENING;
                }
            }
        }
    } else {
        if (net_ip_ready || client_pcb || state != STATE_DISCONNECTED) {
            net_mgr_lwip_lock();
            if (client_pcb) {
                tcp_arg(client_pcb, NULL);
                tcp_recv(client_pcb, NULL);
                tcp_err(client_pcb, NULL);
                tcp_abort(client_pcb);
                client_pcb = NULL;
            }
            net_mgr_lwip_unlock();

            transport_connected = false;
            state = STATE_DISCONNECTED;
            net_ip_ready = false;
            net_ip_address_str[0] = '\0';
            __dmb();
        }
    }
}

bool transport_is_connected(void) {
    return transport_connected;
}

int transport_read_byte(void) {
    uint16_t tail = rx_tail;
    if (rx_head == tail) {
        return -1;
    }
    __dmb();
    uint8_t byte = rx_buf[tail];
    __dmb();
    rx_tail = (tail + 1) & (RX_BUF_SIZE - 1);
    return byte;
}

void transport_write_byte(uint8_t data) {
    if (tx_len < TX_BUF_SIZE) {
        tx_buf[tx_len++] = data;
    } else {
        transport_flush();
        if (tx_len < TX_BUF_SIZE) {
            tx_buf[tx_len++] = data;
        }
    }
}

void transport_flush(void) {
    if (tx_len == 0) {
        return;
    }

    if (!transport_is_connected()) {
        tx_len = 0;
        return;
    }

    uint16_t sent = 0;
    while (sent < tx_len && transport_is_connected()) {
        bool need_wait = false;

        net_mgr_lwip_lock();
        if (!client_pcb || !transport_connected) {
            net_mgr_lwip_unlock();
            break;
        }

        u16_t to_send = tx_len - sent;
        u16_t snd_buf = tcp_sndbuf(client_pcb);
        if (to_send > snd_buf) {
            to_send = snd_buf;
        }

        if (to_send > 0) {
            err_t err = tcp_write(client_pcb, &tx_buf[sent], to_send, TCP_WRITE_FLAG_COPY);
            if (err == ERR_OK) {
                sent += to_send;
                tcp_output(client_pcb);
            } else if (err == ERR_MEM) {
                need_wait = true;
            } else {
                net_mgr_lwip_unlock();
                break;
            }
        } else {
            need_wait = true;
        }
        net_mgr_lwip_unlock();

        if (need_wait) {
            net_mgr_poll();
            tight_loop_contents();
        }
    }

    tx_len = 0;
}
