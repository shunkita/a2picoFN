/*
 * Copyright (c) 2026 a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FLASH_CONFIG_H
#define FLASH_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <hardware/flash.h>

#define WIFI_CONFIG_MAGIC       0x464E4346  // ASCII: 'FNCF' (FujiNet Config)
#define WIFI_CONFIG_VERSION     0x00010000  // v1.0

#define MAX_SSID_LEN            32
#define MAX_PASS_LEN            64
#define MAX_HOST_IP_LEN         16

#define DEFAULT_FUJINET_PORT    1985

// Target Flash Offset: Last 4KB sector of on-board flash
#define FLASH_TARGET_OFFSET     (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct __attribute__((packed)) {
    uint32_t magic;                     // 0x00: Magic number (0x464E4346: "FNCF")
    uint32_t version;                   // 0x04: Data structure version (0x00010000)
    uint16_t length;                    // 0x08: Structure size (256)
    uint16_t flags;                     // 0x0A: Configuration flags (reserved)
    
    char     ssid[MAX_SSID_LEN + 1];    // 0x0C: SSID (up to 32 chars + NUL = 33B)
    char     password[MAX_PASS_LEN + 1];// 0x2D: Password (up to 64 chars + NUL = 65B)
    char     host_ip[MAX_HOST_IP_LEN + 1]; // 0x6E: Host IP (up to 16 chars + NUL = 17B)
    
    uint16_t host_port;                 // 0x7F: Port number (default 1985)
    uint8_t  reserved[123];             // 0x81: Padding (total 252 bytes up to CRC)
    
    uint32_t crc32;                     // 0xFC: CRC32 from magic through reserved
} wifi_config_block_t;                  // Total 256 bytes (exactly 1 flash page)

#ifdef __cplusplus
extern "C" {
#endif

// Initialize and read configuration from Flash with CRC32 verification
void flash_config_init(void);

// Determine if valid configuration exists (Magic match, CRC match, required fields non-empty)
bool flash_config_is_valid(void);

// Get pointer to currently loaded configuration structure (SRAM cache)
const wifi_config_block_t* flash_config_get(void);

// Update SRAM structure, recalculate CRC32, and save to Flash with Core 1 active
bool flash_config_save(const char *ssid, const char *pass, const char *host_ip);

// CRC32 calculation function (IEEE 802.3 polynomial 0xEDB88320)
uint32_t flash_config_calc_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // FLASH_CONFIG_H
