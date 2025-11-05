/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

int zmk_wpm_get_state();

uint32_t zmk_wpm_get_total_keystrokes(void);  // ← THÊM
void zmk_wpm_reset_total_keystrokes(void);    // ← THÊM
