/*
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/split_wpm_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>

#include "peripheral_status.h"

#define MAX_WPM_POINTS 60
#define CANVAS_SIZE 68
#define WPM_GRAPH_WIDTH 68
#define WPM_GRAPH_HEIGHT 68

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

struct wpm_status_state {
    uint8_t wpm;
};

// Thống kê WPM
static uint8_t max_wpm = 0;
static uint32_t avg_wpm_sum = 0;
static uint16_t avg_wpm_count = 0;

// === Helper functions ===

static void draw_background(lv_obj_t *canvas) {
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);
}

static void draw_wpm_graph(lv_obj_t *canvas, uint8_t *values) {
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_t grid_dsc;
    lv_draw_label_dsc_t text_dsc;

    init_line_dsc(&line_dsc, LVGL_FOREGROUND, 1);
    init_line_dsc(&grid_dsc, LVGL_FOREGROUND, 1);
    init_label_dsc(&text_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);

    // Vẽ nền đen
    draw_background(canvas);

    // === VẼ BORDER ===
    lv_point_t border_points[5];
    // Top
    border_points[0] = (lv_point_t){0, 0};
    border_points[1] = (lv_point_t){WPM_GRAPH_WIDTH - 1, 0};
    lv_canvas_draw_line(canvas, border_points, 2, &line_dsc);
    
    // Right
    border_points[0] = (lv_point_t){WPM_GRAPH_WIDTH - 1, 0};
    border_points[1] = (lv_point_t){WPM_GRAPH_WIDTH - 1, WPM_GRAPH_HEIGHT - 1};
    lv_canvas_draw_line(canvas, border_points, 2, &line_dsc);
    
    // Bottom
    border_points[0] = (lv_point_t){WPM_GRAPH_WIDTH - 1, WPM_GRAPH_HEIGHT - 1};
    border_points[1] = (lv_point_t){0, WPM_GRAPH_HEIGHT - 1};
    lv_canvas_draw_line(canvas, border_points, 2, &line_dsc);
    
    // Left
    border_points[0] = (lv_point_t){0, WPM_GRAPH_HEIGHT - 1};
    border_points[1] = (lv_point_t){0, 0};
    lv_canvas_draw_line(canvas, border_points, 2, &line_dsc);

    // Tìm giá trị max để scale
    uint8_t scale_max = max_wpm > 20 ? max_wpm : 20; // Tối thiểu 20

    // === VẼ GRID LINES (25%, 50%, 75%) ===
    // Grid line màu xám nhạt hơn
    lv_draw_line_dsc_t grid_line_dsc;
    init_line_dsc(&grid_line_dsc, LVGL_FOREGROUND, 1);
    // Để tạo màu xám nhạt, ta vẽ đứt nét hoặc chấm chấm
    // Vì LVGL không hỗ trợ dash trực tiếp trên canvas, ta vẽ các chấm nhỏ
    
    const int inner_height = WPM_GRAPH_HEIGHT - 4; // Trừ border + padding
    const int inner_top = 2;
    
    for (int grid = 1; grid <= 3; grid++) {
        int y = inner_top + (inner_height * grid / 4);
        
        // Vẽ đường đứt nét
        for (int x = 2; x < WPM_GRAPH_WIDTH - 2; x += 3) {
            lv_draw_rect_dsc_t dot_dsc;
            init_rect_dsc(&dot_dsc, LVGL_FOREGROUND);
            lv_canvas_draw_rect(canvas, x, y, 1, 1, &dot_dsc);
        }
    }

    // === VẼ GRAPH LINE ===
    const int graph_width = WPM_GRAPH_WIDTH - 4; // Trừ border + padding
    const int graph_height = WPM_GRAPH_HEIGHT - 4;
    const int graph_left = 2;
    const int graph_top = 2;

    lv_point_t graph_points[MAX_WPM_POINTS];
    for (int i = 0; i < MAX_WPM_POINTS; i++) {
        int x = graph_left + (i * graph_width / (MAX_WPM_POINTS - 1));
        int y = graph_top + graph_height - (values[i] * graph_height / (scale_max + 5));
        
        // Clamp y trong vùng vẽ
        if (y < graph_top) y = graph_top;
        if (y > graph_top + graph_height) y = graph_top + graph_height;
        
        graph_points[i] = (lv_point_t){x, y};
    }

    // Vẽ đường graph
    for (int i = 0; i < MAX_WPM_POINTS - 1; i++) {
        lv_point_t line_pts[2] = {graph_points[i], graph_points[i + 1]};
        lv_canvas_draw_line(canvas, line_pts, 2, &line_dsc);
    }

    // === VẼ TEXT (Max và Avg) ===
    char text_buf[12];
    
    // Tính Average (chỉ từ WPM > 0)
    uint8_t avg_wpm = 0;
    if (avg_wpm_count > 0) {
        avg_wpm = avg_wpm_sum / avg_wpm_count;
    }

    // Max WPM - góc trên phải
    snprintf(text_buf, sizeof(text_buf), "M%d", max_wpm);
    lv_canvas_draw_text(canvas, 0, 1, CANVAS_SIZE, &text_dsc, text_buf);
    
    // Avg WPM - góc dưới phải
    snprintf(text_buf, sizeof(text_buf), "A%d", avg_wpm);
    lv_canvas_draw_text(canvas, 0, WPM_GRAPH_HEIGHT - 16, CANVAS_SIZE, &text_dsc, text_buf);

    // Current WPM - số lớn ở giữa bên trái
    lv_draw_label_dsc_t large_text_dsc;
    init_label_dsc(&large_text_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    snprintf(text_buf, sizeof(text_buf), "%d", values[MAX_WPM_POINTS - 1]);
    lv_canvas_draw_text(canvas, 4, WPM_GRAPH_HEIGHT / 2 - 7, 30, &large_text_dsc, text_buf);
}

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    // Nền đen
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    draw_battery(canvas, state);
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                        state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    rotate_canvas(canvas, cbuf);
}

static void draw_wpm(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 1);
    if (!canvas) {
        LOG_ERR("WPM canvas not found!");
        return;
    }

    draw_wpm_graph(canvas, state->wpm);
    rotate_canvas(canvas, cbuf);
}

// === Battery status ===

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif
    widget->state.battery = state.level;
    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_battery_status(widget, state);
    }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

// === Peripheral connection ===

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;
    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_connection_status(widget, state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

// === WPM ===

static void set_wpm_status(struct zmk_widget_status *widget, struct wpm_status_state state) {
    LOG_INF("WPM received from central: %d", state.wpm);
    
    // Shift array sang trái
    for (int i = 0; i < MAX_WPM_POINTS - 1; i++) {
        widget->state.wpm[i] = widget->state.wpm[i + 1];
    }
    widget->state.wpm[MAX_WPM_POINTS - 1] = state.wpm;

    // Cập nhật max
    if (state.wpm > max_wpm) {
        max_wpm = state.wpm;
        LOG_INF("New max WPM: %d", max_wpm);
    }

    // Cập nhật average (chỉ tính WPM > 0)
    if (state.wpm > 0) {
        avg_wpm_sum += state.wpm;
        avg_wpm_count++;
    }

    LOG_INF("Drawing WPM graph - Current: %d, Max: %d, Avg: %d", 
            state.wpm, max_wpm, avg_wpm_count > 0 ? avg_wpm_sum / avg_wpm_count : 0);
    
    draw_wpm(widget->obj, widget->cbuf2, &widget->state);
}

static void wpm_status_update_cb(struct wpm_status_state state) {
    LOG_INF("WPM update callback triggered: %d", state.wpm);
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_wpm_status(widget, state);
    }
}

static struct wpm_status_state wpm_status_get_state(const zmk_event_t *eh) {
    const struct zmk_split_wpm_state_changed *ev = as_zmk_split_wpm_state_changed(eh);
    uint8_t wpm_value = (ev != NULL) ? ev->wpm : 0;
    LOG_INF("Getting WPM state from event: %d", wpm_value);
    return (struct wpm_status_state){.wpm = wpm_value};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_wpm_status, struct wpm_status_state, wpm_status_update_cb,
                            wpm_status_get_state)
ZMK_SUBSCRIPTION(widget_wpm_status, zmk_split_wpm_state_changed);

// === Init ===

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);

    

    // Top canvas
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    // WPM canvas
    lv_obj_t *wpm_canvas = lv_canvas_create(widget->obj);
    lv_obj_align(wpm_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_set_buffer(wpm_canvas, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    

    widget->state.battery = 0;
    widget->state.charging = false;
    widget->state.connected = false;

    // Khởi tạo WPM array với giá trị 0
    for (int i = 0; i < MAX_WPM_POINTS; i++) {
        widget->state.wpm[i] = 0;
    }

    // Reset thống kê
    max_wpm = 0;
    avg_wpm_sum = 0;
    avg_wpm_count = 0;

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();
    widget_wpm_status_init();

    
    draw_top(widget->obj, widget->cbuf2, &widget->state);   
    draw_wpm(widget->obj, widget->cbuf, &widget->state);
    
    LOG_INF("Peripheral WPM widget initialized with border, grid, max/avg display");
    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) {
    return widget->obj;
}
