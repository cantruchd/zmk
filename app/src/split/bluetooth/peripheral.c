/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/wpm.h>

#include "wpm_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct wpm_status_state {
    uint8_t wpm;
    uint8_t max_wpm;
    uint32_t avg_wpm_sum;
    uint16_t avg_wpm_count;
};

// Lưu lịch sử WPM cho graph (60 điểm = 60 giây)
#define WPM_HISTORY_SIZE 60
static uint8_t wpm_history[WPM_HISTORY_SIZE] = {0};
static uint8_t wpm_history_index = 0;

static void set_wpm_status_state(lv_obj_t *widget, struct wpm_status_state state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);
    if (canvas == NULL) {
        return;
    }

    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

    // Kích thước canvas
    const int width = 128;
    const int height = 32;
    
    // Vùng vẽ graph (để chỗ cho text bên phải)
    const int graph_width = 85;
    const int graph_height = height - 2; // -2 cho border
    const int graph_x = 1; // Bắt đầu sau border trái
    const int graph_y = 1; // Bắt đầu sau border trên

    // === VẼ BORDER ===
    // Border ngoài (toàn bộ canvas)
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_white();
    line_dsc.width = 1;
    
    // Top border
    lv_point_t top_line[] = {{0, 0}, {width - 1, 0}};
    lv_canvas_draw_line(canvas, top_line, 2, &line_dsc);
    
    // Bottom border
    lv_point_t bottom_line[] = {{0, height - 1}, {width - 1, height - 1}};
    lv_canvas_draw_line(canvas, bottom_line, 2, &line_dsc);
    
    // Left border
    lv_point_t left_line[] = {{0, 0}, {0, height - 1}};
    lv_canvas_draw_line(canvas, left_line, 2, &line_dsc);
    
    // Right border
    lv_point_t right_line[] = {{width - 1, 0}, {width - 1, height - 1}};
    lv_canvas_draw_line(canvas, right_line, 2, &line_dsc);

    // === VẼ GRID LINES (25%, 50%, 75%) ===
    lv_draw_line_dsc_t grid_dsc;
    lv_draw_line_dsc_init(&grid_dsc);
    grid_dsc.color = lv_color_make(80, 80, 80); // Màu xám nhạt
    grid_dsc.width = 1;
    
    // Tìm max để scale
    uint8_t max_for_scale = state.max_wpm;
    if (max_for_scale < 20) max_for_scale = 20; // Min scale
    
    // Vẽ 3 vạch ngang: 25%, 50%, 75% của max
    for (int i = 1; i <= 3; i++) {
        int y_pos = graph_y + (graph_height * i / 4);
        lv_point_t grid_line[] = {
            {graph_x, y_pos},
            {graph_x + graph_width - 1, y_pos}
        };
        lv_canvas_draw_line(canvas, grid_line, 2, &grid_dsc);
    }

    // === VẼ GRAPH ===
    lv_draw_line_dsc_t graph_line_dsc;
    lv_draw_line_dsc_init(&graph_line_dsc);
    graph_line_dsc.color = lv_color_white();
    graph_line_dsc.width = 1;

    // Vẽ lịch sử WPM
    for (int i = 1; i < WPM_HISTORY_SIZE; i++) {
        int prev_idx = (wpm_history_index + i - 1) % WPM_HISTORY_SIZE;
        int curr_idx = (wpm_history_index + i) % WPM_HISTORY_SIZE;
        
        uint8_t prev_wpm = wpm_history[prev_idx];
        uint8_t curr_wpm = wpm_history[curr_idx];
        
        if (prev_wpm == 0 && curr_wpm == 0) continue;
        
        // Scale theo max_wpm
        int prev_y = graph_y + graph_height - 
                     (prev_wpm * graph_height / (max_for_scale + 5));
        int curr_y = graph_y + graph_height - 
                     (curr_wpm * graph_height / (max_for_scale + 5));
        
        // Map index to x position (từ trái sang phải, mới nhất ở phải)
        int prev_x = graph_x + ((i - 1) * (graph_width - 1) / (WPM_HISTORY_SIZE - 1));
        int curr_x = graph_x + (i * (graph_width - 1) / (WPM_HISTORY_SIZE - 1));
        
        lv_point_t line[] = {{prev_x, prev_y}, {curr_x, curr_y}};
        lv_canvas_draw_line(canvas, line, 2, &graph_line_dsc);
    }

    // === VẼ TEXT (Max và Avg) ===
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_white();
    label_dsc.font = &lv_font_montserrat_10;

    char text_buf[16];
    
    // Vẽ Max WPM (góc trên bên phải)
    snprintf(text_buf, sizeof(text_buf), "M%3d", state.max_wpm);
    lv_canvas_draw_text(canvas, graph_x + graph_width + 3, 3, 40, &label_dsc, text_buf);
    
    // Tính Average WPM
    uint8_t avg_wpm = 0;
    if (state.avg_wpm_count > 0) {
        avg_wpm = state.avg_wpm_sum / state.avg_wpm_count;
    }
    
    // Vẽ Avg WPM (góc dưới bên phải)
    snprintf(text_buf, sizeof(text_buf), "A%3d", avg_wpm);
    lv_canvas_draw_text(canvas, graph_x + graph_width + 3, height - 13, 40, &label_dsc, text_buf);

    // === VẼ CURRENT WPM (số lớn ở giữa text area) ===
    label_dsc.font = &lv_font_montserrat_14;
    snprintf(text_buf, sizeof(text_buf), "%3d", state.wpm);
    lv_canvas_draw_text(canvas, graph_x + graph_width + 5, height / 2 - 7, 40, &label_dsc, text_buf);
}

static void wpm_status_update_cb(struct wpm_status_state state) {
    struct zmk_widget_wpm_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_wpm_status_state(widget->obj, state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_wpm_status, struct wpm_status_state,
                             wpm_status_update_cb, NULL)

ZMK_SUBSCRIPTION(widget_wpm_status, zmk_wpm_state_changed);

int zmk_widget_wpm_status_init(struct zmk_widget_wpm_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 128, 32);

    // Canvas buffer (128x32 = 4096 pixels, 1 bit per pixel = 512 bytes)
    static lv_color_t cbuf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(128, 32)];
    
    lv_obj_t *canvas = lv_canvas_create(widget->obj);
    lv_canvas_set_buffer(canvas, cbuf, 128, 32, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    
    sys_slist_append(&widgets, &widget->node);

    widget_wpm_status_init();
    
    struct wpm_status_state state = {
        .wpm = zmk_wpm_get_state(),
        .max_wpm = 0,
        .avg_wpm_sum = 0,
        .avg_wpm_count = 0
    };
    set_wpm_status_state(widget->obj, state);

    return 0;
}

lv_obj_t *zmk_widget_wpm_status_obj(struct zmk_widget_wpm_status *widget) {
    return widget->obj;
}

// Update handler khi nhận event WPM changed
static int widget_wpm_status_event_listener(const zmk_event_t *eh) {
    const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    static struct wpm_status_state state = {0};
    
    state.wpm = ev->state;
    
    // Update max
    if (state.wpm > state.max_wpm) {
        state.max_wpm = state.wpm;
    }
    
    // Update average (chỉ tính WPM > 0)
    if (state.wpm > 0) {
        state.avg_wpm_sum += state.wpm;
        state.avg_wpm_count++;
    }
    
    // Thêm vào history
    wpm_history[wpm_history_index] = state.wpm;
    wpm_history_index = (wpm_history_index + 1) % WPM_HISTORY_SIZE;
    
    wpm_status_update_cb(state);
    
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_wpm_status_event, widget_wpm_status_event_listener);
ZMK_SUBSCRIPTION(widget_wpm_status_event, zmk_wpm_state_changed);
