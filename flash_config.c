/*
 * Copyright (c) 2026 a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pico/stdlib.h>
#include <hardware/sync.h>
#include <hardware/flash.h>

#include "flash_config.h"

static_assert(sizeof(wifi_config_block_t) == FLASH_PAGE_SIZE,
              "wifi_config_block_t size must be exactly FLASH_PAGE_SIZE (256 bytes)");

static wifi_config_block_t cached_config;
static bool config_valid = false;

// IEEE 802.3 CRC32 (polynomial: 0xEDB88320)
uint32_t flash_config_calc_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

static bool validate_block(const wifi_config_block_t *block) {
    if (block->magic != WIFI_CONFIG_MAGIC) {
        return false;
    }
    if (block->version != WIFI_CONFIG_VERSION) {
        return false;
    }
    if (block->length != sizeof(wifi_config_block_t)) {
        return false;
    }

    uint32_t expected_crc = flash_config_calc_crc32(
        (const uint8_t *)block,
        offsetof(wifi_config_block_t, crc32)
    );
    if (block->crc32 != expected_crc) {
        return false;
    }

    // String sanity check (must contain NULL terminator and at least 1 character)
    if (strnlen(block->ssid, sizeof(block->ssid)) == 0 ||
        strnlen(block->ssid, sizeof(block->ssid)) > MAX_SSID_LEN) {
        return false;
    }
    if (strnlen(block->password, sizeof(block->password)) == 0 ||
        strnlen(block->password, sizeof(block->password)) > MAX_PASS_LEN) {
        return false;
    }
    if (strnlen(block->host_ip, sizeof(block->host_ip)) == 0 ||
        strnlen(block->host_ip, sizeof(block->host_ip)) > MAX_HOST_IP_LEN) {
        return false;
    }

    return true;
}

void flash_config_init(void) {
    const uint8_t *flash_target = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

    for (int retry = 0; retry < 3; retry++) {
        // Read 1 page (256 bytes) from end of Flash into SRAM cache
        memcpy(&cached_config, flash_target, sizeof(wifi_config_block_t));

        config_valid = validate_block(&cached_config);
        if (config_valid) {
            return;
        }

        // If validation fails, wait briefly and retry (safeguard for cold boot transient)
        if (retry < 2) {
            sleep_ms(10);
        }
    }
}

bool flash_config_is_valid(void) {
    return config_valid;
}

const wifi_config_block_t* flash_config_get(void) {
    return &cached_config;
}

bool __no_inline_not_in_flash_func(flash_config_save)(const char *ssid, const char *pass, const char *host_ip) {
    if (!ssid || !pass || !host_ip) {
        return false;
    }

    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(pass);
    size_t ip_len = strlen(host_ip);

    if (ssid_len == 0 || ssid_len > MAX_SSID_LEN ||
        pass_len == 0 || pass_len > MAX_PASS_LEN ||
        ip_len == 0 || ip_len > MAX_HOST_IP_LEN) {
        return false;
    }

    wifi_config_block_t new_block;
    memset(&new_block, 0, sizeof(wifi_config_block_t));

    new_block.magic = WIFI_CONFIG_MAGIC;
    new_block.version = WIFI_CONFIG_VERSION;
    new_block.length = sizeof(wifi_config_block_t);
    new_block.flags = 0;
    new_block.host_port = DEFAULT_FUJINET_PORT;

    strncpy(new_block.ssid, ssid, MAX_SSID_LEN);
    strncpy(new_block.password, pass, MAX_PASS_LEN);
    strncpy(new_block.host_ip, host_ip, MAX_HOST_IP_LEN);

    new_block.crc32 = flash_config_calc_crc32(
        (const uint8_t *)&new_block,
        offsetof(wifi_config_block_t, crc32)
    );

    // Approach 1: Keep Core 1 responding to Apple II bus from SRAM without halting.
    // Disable local interrupts on Core 0 only during Flash erase & programming.
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t *)&new_block, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    // Verify written data by reading back
    const uint8_t *flash_target = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    if (memcmp(flash_target, &new_block, sizeof(wifi_config_block_t)) != 0) {
        config_valid = false;
        return false;
    }

    memcpy(&cached_config, &new_block, sizeof(wifi_config_block_t));
    config_valid = true;
    return true;
}
