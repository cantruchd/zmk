/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/events/split_wpm_state_changed.h>
#include <zmk/split/bluetooth/service.h>  // ← THÊM ĐỂ ACCESS SPLIT SERVICE
#include <zmk/wpm.h>

// Define custom message ID for WPM
#define ZMK_SPLIT_RUN_BEHAVIOR_PAYLOAD_WPM 0xF0  // Custom ID, chọn số chưa dùng

// CENTRAL: Gửi WPM qua BLE
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static int wpm_state_changed_listener(const zmk_event_t *eh) {
    uint8_t wpm = zmk_wpm_get_state();
    
    LOG_INF("Central broadcasting WPM: %d", wpm);
    
    // Gửi qua split service
    uint8_t data[2] = {ZMK_SPLIT_RUN_BEHAVIOR_PAYLOAD_WPM, wpm};
    
    int err = zmk_split_bt_position_pressed(data, sizeof(data));
    if (err) {
        LOG_ERR("Failed to send WPM to peripheral: %d", err);
    }
    
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(wpm_split_central, wpm_state_changed_listener);
ZMK_SUBSCRIPTION(wpm_split_central, zmk_wpm_state_changed);

#endif // CONFIG_ZMK_SPLIT_ROLE_CENTRAL

// PERIPHERAL: Nhận WPM từ central
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_PERIPHERAL)

// Handler để nhận data từ central
int zmk_split_wpm_position_handler(uint8_t *data, uint8_t len) {
    if (len < 2) return -EINVAL;
    if (data[0] != ZMK_SPLIT_RUN_BEHAVIOR_PAYLOAD_WPM) return 0;
    
    uint8_t wpm = data[1];
    LOG_INF("Peripheral received WPM from central: %d", wpm);
    
    // Raise local event để update display
    raise_zmk_split_wpm_state_changed(
        (struct zmk_split_wpm_state_changed){.wpm = wpm}
    );
    
    return 0;
}

#endif // CONFIG_ZMK_SPLIT_ROLE_PERIPHERAL
