/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */
#pragma once
#include <lvgl.h>
#include <zephyr/kernel.h>
#include "util.h"

#define MAX_WPM_POINTS 60
#define CANVAS_SIZE 68

struct status_state {
    uint8_t battery;
    bool charging;
    bool connected;
    uint8_t wpm[MAX_WPM_POINTS];
};

struct zmk_widget_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_color_t cbuf[CANVAS_SIZE * CANVAS_SIZE];
    lv_color_t cbuf2[CANVAS_SIZE * CANVAS_SIZE];
    struct status_state state;
};

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget);
