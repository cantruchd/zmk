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
#include <zmk/wpm.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT)

// ============================================================================
// CENTRAL: Gửi WPM + Total Keystrokes tới peripherals
// ============================================================================
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include <zmk/split/central.h>

static int wpm_state_changed_listener(const zmk_event_t *eh) {
    uint8_t wpm = zmk_wpm_get_state();
    
    LOG_INF("Central WPM changed: %d - Broadcasting to peripherals", wpm);
    
    // Gửi WPM tới tất cả peripherals qua split transport
    int err = zmk_split_central_send_wpm(wpm);
    if (err < 0) {
        LOG_ERR("Failed to send WPM to peripherals: %d", err);
    }
    
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(wpm_split_central, wpm_state_changed_listener);
ZMK_SUBSCRIPTION(wpm_split_central, zmk_wpm_state_changed);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

// ============================================================================
// PERIPHERAL: Nhận WPM + Total Keystrokes từ central
// ============================================================================
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_PERIPHERAL)

static int wpm_split_peripheral_listener(const zmk_event_t *eh) {
    const struct zmk_split_wpm_state_changed *ev = as_zmk_split_wpm_state_changed(eh);
    
    if (ev) {
        LOG_INF("Peripheral received - WPM: %d, Total Keystrokes: %d", 
                ev->wpm, ev->total_keystrokes);
    }
    
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(wpm_split_peripheral, wpm_split_peripheral_listener);
ZMK_SUBSCRIPTION(wpm_split_peripheral, zmk_split_wpm_state_changed);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_PERIPHERAL)

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT)
