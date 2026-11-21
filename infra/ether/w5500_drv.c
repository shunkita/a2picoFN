/*
 * Copyright (c) 2026 a2picoAFP / a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#include "w5500_drv.h"

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

/* Pin definitions from boards/a2pico2.h with safe fallbacks */
#ifndef PICO_ETH_SPI
#define PICO_ETH_SPI 1
#endif
#ifndef PICO_ETH_RST_PIN
#define PICO_ETH_RST_PIN 42
#endif
#ifndef PICO_ETH_INT_PIN
#define PICO_ETH_INT_PIN 43
#endif
#ifndef PICO_ETH_SPI_RX_PIN
#define PICO_ETH_SPI_RX_PIN 44
#endif
#ifndef PICO_ETH_SPI_CSN_PIN
#define PICO_ETH_SPI_CSN_PIN 45
#endif
#ifndef PICO_ETH_SPI_SCK_PIN
#define PICO_ETH_SPI_SCK_PIN 46
#endif
#ifndef PICO_ETH_SPI_TX_PIN
#define PICO_ETH_SPI_TX_PIN 47
#endif

#if PICO_ETH_SPI == 0
#define W5500_SPI_INST spi0
#else
#define W5500_SPI_INST spi1
#endif

#define W5500_SPI_BAUDRATE   (20U * 1000U * 1000U)

/* W5500 Block Select Bits (BSB) */
#define W5500_BSB_COMMON     0x00U
#define W5500_BSB_S0_REG     0x01U
#define W5500_BSB_S0_TX      0x02U
#define W5500_BSB_S0_RX      0x03U

/* W5500 Control Byte flags (VDM mode: OM = 00) */
#define W5500_CTRL_READ      0x00U
#define W5500_CTRL_WRITE     0x04U

/* Common registers */
#define W5500_REG_MR         0x0000U
#define W5500_REG_SHAR       0x0009U
#define W5500_REG_PHYCFGR    0x002EU
#define W5500_REG_VERSIONR   0x0039U

/* Socket 0 registers */
#define W5500_SN_MR          0x0000U
#define W5500_SN_CR          0x0001U
#define W5500_SN_IR          0x0002U
#define W5500_SN_SR          0x0003U
#define W5500_SN_RXBUF_SIZE  0x001EU
#define W5500_SN_TXBUF_SIZE  0x001FU
#define W5500_SN_TX_FSR      0x0020U
#define W5500_SN_TX_WR       0x0024U
#define W5500_SN_RX_RSR      0x0026U
#define W5500_SN_RX_RD       0x0028U

/* Socket mode / command / status / interrupt bits */
#define W5500_SN_MR_MACRAW   0x04U /* MACRAW, MFEN=0 (allow multicast for mDNS) */
#define W5500_SN_CR_OPEN     0x01U
#define W5500_SN_CR_CLOSE    0x10U
#define W5500_SN_CR_SEND     0x20U
#define W5500_SN_CR_RECV     0x40U
#define W5500_SN_SR_MACRAW   0x42U
#define W5500_SN_IR_SENDOK   0x10U
#define W5500_SN_IR_TIMEOUT  0x08U
#define W5500_PHYCFGR_LNK    0x01U
#define W5500_VERSION_VAL    0x04U

static bool s_w5500_ready = false;

static inline void w5500_cs_select(void) {
    gpio_put(PICO_ETH_SPI_CSN_PIN, 0);
}

static inline void w5500_cs_deselect(void) {
    gpio_put(PICO_ETH_SPI_CSN_PIN, 1);
}

static void w5500_write_buf(uint16_t addr, uint8_t bsb,
                            const uint8_t *data, uint16_t len) {
    uint8_t hdr[3];
    hdr[0] = (uint8_t)(addr >> 8);
    hdr[1] = (uint8_t)(addr & 0xFFU);
    hdr[2] = (uint8_t)((bsb << 3) | W5500_CTRL_WRITE);

    w5500_cs_select();
    spi_write_blocking(W5500_SPI_INST, hdr, 3);
    if (len > 0 && data != NULL) {
        spi_write_blocking(W5500_SPI_INST, data, len);
    }
    w5500_cs_deselect();
}

static void w5500_read_buf(uint16_t addr, uint8_t bsb,
                           uint8_t *data, uint16_t len) {
    uint8_t hdr[3];
    hdr[0] = (uint8_t)(addr >> 8);
    hdr[1] = (uint8_t)(addr & 0xFFU);
    hdr[2] = (uint8_t)((bsb << 3) | W5500_CTRL_READ);

    w5500_cs_select();
    spi_write_blocking(W5500_SPI_INST, hdr, 3);
    if (len > 0 && data != NULL) {
        spi_read_blocking(W5500_SPI_INST, 0x00, data, len);
    }
    w5500_cs_deselect();
}

static void w5500_write_u8(uint16_t addr, uint8_t bsb, uint8_t val) {
    w5500_write_buf(addr, bsb, &val, 1);
}

static uint8_t w5500_read_u8(uint16_t addr, uint8_t bsb) {
    uint8_t val = 0;
    w5500_read_buf(addr, bsb, &val, 1);
    return val;
}

static void w5500_write_u16(uint16_t addr, uint8_t bsb, uint16_t val) {
    uint8_t b[2] = {(uint8_t)(val >> 8), (uint8_t)(val & 0xFFU)};
    w5500_write_buf(addr, bsb, b, 2);
}

static uint16_t w5500_read_u16_stable(uint16_t addr, uint8_t bsb) {
    uint16_t v1 = 0;
    uint16_t v2 = 0;
    uint8_t b[2];
    for (int i = 0; i < 8; i++) {
        w5500_read_buf(addr, bsb, b, 2);
        v1 = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
        w5500_read_buf(addr, bsb, b, 2);
        v2 = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
        if (v1 == v2) {
            return v1;
        }
    }
    return v2;
}

static bool w5500_exec_s0_cmd(uint8_t cmd) {
    w5500_write_u8(W5500_SN_CR, W5500_BSB_S0_REG, cmd);
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (w5500_read_u8(W5500_SN_CR, W5500_BSB_S0_REG) != 0x00U) {
        if ((uint32_t)(to_ms_since_boot(get_absolute_time()) - start) > 50U) {
            return false;
        }
    }
    return true;
}

bool w5500_init(const uint8_t mac[6]) {
    s_w5500_ready = false;
    if (!mac) {
        return false;
    }

    /* Configure SPI1 at 20 MHz, Mode 0 */
    spi_init(W5500_SPI_INST, W5500_SPI_BAUDRATE);
    spi_set_format(W5500_SPI_INST, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PICO_ETH_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_ETH_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_ETH_SPI_TX_PIN, GPIO_FUNC_SPI);

    /* Manual CS pin */
    gpio_init(PICO_ETH_SPI_CSN_PIN);
    gpio_set_dir(PICO_ETH_SPI_CSN_PIN, GPIO_OUT);
    w5500_cs_deselect();

    /* INT pin as input (unused in polling mode) */
    gpio_init(PICO_ETH_INT_PIN);
    gpio_set_dir(PICO_ETH_INT_PIN, GPIO_IN);
    gpio_pull_up(PICO_ETH_INT_PIN);

    /* Hardware reset pulse (active low >= 500 us, wait >= 2 ms) */
    gpio_init(PICO_ETH_RST_PIN);
    gpio_set_dir(PICO_ETH_RST_PIN, GPIO_OUT);
    gpio_put(PICO_ETH_RST_PIN, 0);
    sleep_ms(5);
    gpio_put(PICO_ETH_RST_PIN, 1);
    sleep_ms(20);

    /* Verify W5500 chip version (0x04) */
    uint8_t ver = w5500_read_u8(W5500_REG_VERSIONR, W5500_BSB_COMMON);
    if (ver != W5500_VERSION_VAL) {
        printf("[W5500] Unexpected VERSIONR=0x%02X (expected 0x04)\n", (unsigned)ver);
        return false;
    }

    /* Software reset */
    w5500_write_u8(W5500_REG_MR, W5500_BSB_COMMON, 0x80U);
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while ((w5500_read_u8(W5500_REG_MR, W5500_BSB_COMMON) & 0x80U) != 0U) {
        if ((uint32_t)(to_ms_since_boot(get_absolute_time()) - start) > 100U) {
            printf("[W5500] Software reset timed out\n");
            return false;
        }
    }

    /* Set Source Hardware Address (MAC) */
    w5500_write_buf(W5500_REG_SHAR, W5500_BSB_COMMON, mac, 6);

    /* Allocate all 16 KB TX and 16 KB RX buffer memory to Socket 0 (Sockets 1..7 = 0 KB) */
    w5500_write_u8(W5500_SN_RXBUF_SIZE, W5500_BSB_S0_REG, 16U);
    w5500_write_u8(W5500_SN_TXBUF_SIZE, W5500_BSB_S0_REG, 16U);
    for (uint8_t s = 1; s < 8; s++) {
        uint8_t s_bsb = (uint8_t)((s << 2) | 0x01U);
        w5500_write_u8(W5500_SN_RXBUF_SIZE, s_bsb, 0U);
        w5500_write_u8(W5500_SN_TXBUF_SIZE, s_bsb, 0U);
    }

    /* Open Socket 0 in MACRAW mode (MFEN=0 allows multicast mDNS 224.0.0.251) */
    w5500_write_u8(W5500_SN_MR, W5500_BSB_S0_REG, W5500_SN_MR_MACRAW);
    if (!w5500_exec_s0_cmd(W5500_SN_CR_OPEN)) {
        printf("[W5500] Socket 0 OPEN command timed out\n");
        return false;
    }

    uint8_t sr = w5500_read_u8(W5500_SN_SR, W5500_BSB_S0_REG);
    if (sr != W5500_SN_SR_MACRAW) {
        printf("[W5500] Socket 0 SR=0x%02X (expected 0x42 MACRAW)\n", (unsigned)sr);
        return false;
    }

    s_w5500_ready = true;
    printf("[W5500] Initialized SPI%d @20MHz MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
           PICO_ETH_SPI, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

bool w5500_is_link_up(void) {
    if (!s_w5500_ready) {
        return false;
    }
    uint8_t phy = w5500_read_u8(W5500_REG_PHYCFGR, W5500_BSB_COMMON);
    return (phy & W5500_PHYCFGR_LNK) != 0U;
}

bool w5500_send_frame(const uint8_t *frame, uint16_t len) {
    if (!s_w5500_ready || !frame || len == 0 || len > 1536U) {
        return false;
    }

    /* Wait for sufficient TX free buffer space */
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (w5500_read_u16_stable(W5500_SN_TX_FSR, W5500_BSB_S0_REG) < len) {
        if ((uint32_t)(to_ms_since_boot(get_absolute_time()) - start) > 50U) {
            return false;
        }
    }

    /* Write frame payload to Socket 0 TX buffer at Sn_TX_WR */
    uint16_t ptr = w5500_read_u16_stable(W5500_SN_TX_WR, W5500_BSB_S0_REG);
    w5500_write_buf(ptr, W5500_BSB_S0_TX, frame, len);
    ptr = (uint16_t)(ptr + len);
    w5500_write_u16(W5500_SN_TX_WR, W5500_BSB_S0_REG, ptr);

    /* Clear previous SEND_OK / TIMEOUT flags and trigger SEND */
    w5500_write_u8(W5500_SN_IR, W5500_BSB_S0_REG,
                   (uint8_t)(W5500_SN_IR_SENDOK | W5500_SN_IR_TIMEOUT));
    if (!w5500_exec_s0_cmd(W5500_SN_CR_SEND)) {
        return false;
    }

    start = to_ms_since_boot(get_absolute_time());
    while (true) {
        uint8_t ir = w5500_read_u8(W5500_SN_IR, W5500_BSB_S0_REG);
        if ((ir & W5500_SN_IR_SENDOK) != 0U) {
            w5500_write_u8(W5500_SN_IR, W5500_BSB_S0_REG, W5500_SN_IR_SENDOK);
            return true;
        }
        if ((ir & W5500_SN_IR_TIMEOUT) != 0U) {
            w5500_write_u8(W5500_SN_IR, W5500_BSB_S0_REG, W5500_SN_IR_TIMEOUT);
            return false;
        }
        if ((uint32_t)(to_ms_since_boot(get_absolute_time()) - start) > 50U) {
            return false;
        }
    }
}

uint16_t w5500_recv_frame(uint8_t *buf, uint16_t buf_size) {
    if (!s_w5500_ready || !buf || buf_size == 0) {
        return 0;
    }

    uint16_t rsr = w5500_read_u16_stable(W5500_SN_RX_RSR, W5500_BSB_S0_REG);
    if (rsr < 2U) {
        return 0;
    }

    /* MACRAW RX packets begin with a 2-byte big-endian length (includes the 2-byte header) */
    uint16_t ptr = w5500_read_u16_stable(W5500_SN_RX_RD, W5500_BSB_S0_REG);
    uint8_t len_hdr[2];
    w5500_read_buf(ptr, W5500_BSB_S0_RX, len_hdr, 2);
    uint16_t pkt_len = (uint16_t)(((uint16_t)len_hdr[0] << 8) | len_hdr[1]);

    if (pkt_len < 2U || pkt_len > rsr) {
        /* Corrupt length: discard all queued bytes to recover RX ring */
        ptr = (uint16_t)(ptr + rsr);
        w5500_write_u16(W5500_SN_RX_RD, W5500_BSB_S0_REG, ptr);
        (void)w5500_exec_s0_cmd(W5500_SN_CR_RECV);
        return 0;
    }

    uint16_t frame_len = (uint16_t)(pkt_len - 2U);
    uint16_t data_ptr = (uint16_t)(ptr + 2U);
    uint16_t ret_len = 0;

    if (frame_len > 0 && frame_len <= buf_size) {
        w5500_read_buf(data_ptr, W5500_BSB_S0_RX, buf, frame_len);
        ret_len = frame_len;
    }

    /* Advance RX read pointer past the entire MACRAW record */
    ptr = (uint16_t)(ptr + pkt_len);
    w5500_write_u16(W5500_SN_RX_RD, W5500_BSB_S0_REG, ptr);
    (void)w5500_exec_s0_cmd(W5500_SN_CR_RECV);

    return ret_len;
}
