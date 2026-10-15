/*
 * Copyright (c) 2026 a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <pico/stdlib.h>
#include <hardware/sync.h>
#include <pico/cyw43_arch.h>
#include <lwip/tcp.h>
#include <lwip/ip_addr.h>

#include "wifi_config.h"
#include "flash_config.h"
#include "transport.h"

#define RX_BUF_SIZE 4096
#define TX_BUF_SIZE 2048

typedef enum {
    STATE_DISCONNECTED = 0,
    STATE_WIFI_CONNECTED_LISTENING,
    STATE_CLIENT_CONNECTED,
} transport_state_t;

volatile bool transport_connected = false;
volatile bool config_required = false;
static volatile transport_state_t state = STATE_DISCONNECTED;
static struct tcp_pcb *server_pcb = NULL;
static struct tcp_pcb *client_pcb = NULL;

// RX FIFO
static uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

// TX Buffer
static uint8_t tx_buf[TX_BUF_SIZE];
static uint16_t tx_len = 0;

static int last_led_state = -1;

static inline uint16_t rx_available(void) {
    return (rx_head - rx_tail) & (RX_BUF_SIZE - 1);
}

static void rx_push_byte(uint8_t byte) {
    uint16_t next_head = (rx_head + 1) & (RX_BUF_SIZE - 1);
    if (next_head != rx_tail) {
        rx_buf[rx_head] = byte;
        rx_head = next_head;
    }
}

// Visual error indication loop (blinks N times per period)
static void show_error_blink_forever(int blinks, uint32_t period_ms) {
    while (true) {
        for (int i = 0; i < blinks; i++) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(150);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            sleep_ms(150);
        }
        uint32_t elapsed = blinks * 300;
        if (period_ms > elapsed) {
            sleep_ms(period_ms - elapsed);
        }
    }
}

// Fast continuous blink loop (100ms ON / 100ms OFF)
static void show_fast_blink_forever(void) {
    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(100);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(100);
    }
}

static void update_led(void) {
    int led_on = 0;

    switch (state) {
        case STATE_CLIENT_CONNECTED:
            // fujinet-pc connected: solid ON
            led_on = 1;
            break;

        case STATE_WIFI_CONNECTED_LISTENING: {
            // Wi-Fi connected, listening for TCP client: 1 pulse per second (150ms ON)
            uint32_t phase = to_ms_since_boot(get_absolute_time()) % 1000;
            led_on = (phase < 150) ? 1 : 0;
            break;
        }

        case STATE_DISCONNECTED:
        default:
            led_on = 0;
            break;
    }

    if (led_on != last_led_state) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
        last_led_state = led_on;
    }
}

static void on_tcp_error(void *arg, err_t err) {
    client_pcb = NULL; // lwIP automatically frees the PCB on error
    transport_connected = false;
    __dmb();
    state = STATE_WIFI_CONNECTED_LISTENING;
}

static err_t on_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
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
        state = STATE_WIFI_CONNECTED_LISTENING;
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
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    last_led_state = 1;

    return ERR_OK;
}

static void start_tcp_listener(void) {
    server_pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!server_pcb) {
        return;
    }

    err_t err = tcp_bind(server_pcb, IP_ADDR_ANY, FUJINET_PC_PORT);
    if (err != ERR_OK) {
        tcp_close(server_pcb);
        server_pcb = NULL;
        return;
    }

    server_pcb = tcp_listen_with_backlog(server_pcb, 1);
    if (!server_pcb) {
        return;
    }

    tcp_accept(server_pcb, on_tcp_accept);
    state = STATE_WIFI_CONNECTED_LISTENING;
}

void transport_init(void) {
    rx_head = rx_tail = 0;
    tx_len = 0;
    server_pcb = NULL;
    client_pcb = NULL;
    transport_connected = false;
    __dmb();
    last_led_state = -1;

    // ----------------------------------------------------
    // Step 1: Execute cyw43_arch_init()
    // ----------------------------------------------------
    int init_rc = cyw43_arch_init();
    if (init_rc != 0) {
        show_error_blink_forever(1, 1000);
        return;
    }

    cyw43_arch_enable_sta_mode();

    // If not configured, skip normal Wi-Fi connection and enter CONFIG wait state (Bit 6)
    if (!flash_config_is_valid()) {
        config_required = true;
        __dmb();
        return;
    }

    // If configured, start normal connection
    transport_start_wifi();
}

bool transport_start_wifi(void) {
    config_required = false;
    __dmb();

    const char *target_ssid = WIFI_SSID;
    const char *target_password = WIFI_PASSWORD;

    if (flash_config_is_valid()) {
        const wifi_config_block_t *cfg = flash_config_get();
        target_ssid = cfg->ssid;
        target_password = cfg->password;
    }

    // Turn on onboard LED before connection attempt (indicating connecting state)
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    last_led_state = 1;

    // Attempt Wi-Fi connection using synchronous connect function (20s timeout)
    int rc = cyw43_arch_wifi_connect_timeout_ms(
        target_ssid,
        target_password,
        CYW43_AUTH_WPA2_MIXED_PSK,
        20000
    );

    if (rc != 0) {
        // On failure, do not re-set config_required; stay in disconnected state (transport_connected == false)
        return false;
    }

    // Wi-Fi connection successful -> start TCP server (Listener)
    start_tcp_listener();
    return true;
}

void transport_task(void) {
    cyw43_arch_poll();
    update_led();

    // While CONFIG is running (config_required == true), suppress Wi-Fi reconnection,
    // eliminating Core 0 blocking and freeing CYW43 exclusively for AP scanning
    if (config_required) {
        return;
    }

    // Check Wi-Fi link status
    if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
        if (client_pcb) {
            tcp_abort(client_pcb);
            client_pcb = NULL;
        }
        state = STATE_DISCONNECTED;

        const char *target_ssid = WIFI_SSID;
        const char *target_password = WIFI_PASSWORD;
        if (flash_config_is_valid()) {
            const wifi_config_block_t *cfg = flash_config_get();
            target_ssid = cfg->ssid;
            target_password = cfg->password;
        }

        // Attempt Wi-Fi reconnection
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        last_led_state = 1;
        int rc = cyw43_arch_wifi_connect_timeout_ms(
            target_ssid,
            target_password,
            CYW43_AUTH_WPA2_MIXED_PSK,
            10000
        );
        if (rc == 0) {
            if (!server_pcb) {
                start_tcp_listener();
            } else {
                state = STATE_WIFI_CONNECTED_LISTENING;
            }
        }
    }
}

bool transport_is_connected(void) {
    return transport_connected;
}

int transport_read_byte(void) {
    if (rx_head == rx_tail) {
        return -1;
    }
    uint8_t byte = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) & (RX_BUF_SIZE - 1);
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
                cyw43_arch_poll();
                update_led();
            } else {
                break;
            }
        } else {
            cyw43_arch_poll();
            update_led();
        }
    }

    tx_len = 0;
}
