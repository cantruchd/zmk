/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/split/bluetooth/uuid.h>

// Peripheral tiếp tục advertise sau khi connected để Central đọc RSSI

static struct bt_le_adv_param adv_params = {
    .id = BT_ID_DEFAULT,
    .options = BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_IDENTITY,
    .interval_min = BT_GAP_ADV_SLOW_INT_MIN,  // 1s interval
    .interval_max = BT_GAP_ADV_SLOW_INT_MAX,  // 1.2s interval
};

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, ZMK_SPLIT_BT_SERVICE_UUID),
};

static void restart_advertising_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(restart_adv_work, restart_advertising_work_handler);

static void restart_advertising_work_handler(struct k_work *work) {
    int err = bt_le_adv_start(&adv_params, ad, ARRAY_SIZE(ad), NULL, 0);
    
    if (err == 0) {
        LOG_INF("Restarted advertising for RSSI monitoring");
    } else if (err == -EALREADY) {
        LOG_DBG("Advertising already running");
    } else {
        LOG_WRN("Failed to restart advertising (err %d)", err);
        // Retry sau 5s
        k_work_schedule(&restart_adv_work, K_MSEC(5000));
    }
}

static void peripheral_connected_cb(struct bt_conn *conn, uint8_t err) {
    if (err) {
        LOG_ERR("Connection failed (err %d)", err);
        return;
    }
    
    struct bt_conn_info info;
    bt_conn_get_info(conn, &info);
    
    if (info.role == BT_CONN_ROLE_PERIPHERAL) {
        char addr[BT_ADDR_LE_STR_LEN];
        bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
        
        LOG_INF("Peripheral connected to %s, scheduling advertising restart", addr);
        
        // Restart advertising sau 2s để connection ổn định
        k_work_schedule(&restart_adv_work, K_MSEC(2000));
    }
}

static void peripheral_disconnected_cb(struct bt_conn *conn, uint8_t reason) {
    struct bt_conn_info info;
    bt_conn_get_info(conn, &info);
    
    if (info.role == BT_CONN_ROLE_PERIPHERAL) {
        LOG_INF("Peripheral disconnected (reason %d)", reason);
        // Advertising sẽ tự động restart bởi ZMK peripheral service
    }
}

static struct bt_conn_cb peripheral_conn_callbacks = {
    .connected = peripheral_connected_cb,
    .disconnected = peripheral_disconnected_cb,
};

static int peripheral_advertising_for_rssi_init(void) {
    bt_conn_cb_register(&peripheral_conn_callbacks);
    LOG_INF("Peripheral RSSI advertising support initialized");
    return 0;
}

SYS_INIT(peripheral_advertising_for_rssi_init, APPLICATION, 
         CONFIG_APPLICATION_INIT_PRIORITY);
