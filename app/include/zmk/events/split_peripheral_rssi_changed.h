/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct zmk_split_peripheral_rssi_changed {
    uint8_t source;  // Index của peripheral (0, 1, 2...)
    int8_t rssi;     // Giá trị RSSI (-30 đến -90 dBm)
};

ZMK_EVENT_DECLARE(zmk_split_peripheral_rssi_changed);
