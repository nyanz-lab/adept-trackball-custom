/* Copyright 2023 Colin Lam (Ploopy Corporation)
 * Copyright 2020 Christopher Courtney, aka Drashna Jael're  (@drashna) <drashna@live.com>
 * Copyright 2019 Sunjun Kim
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include QMK_KEYBOARD_H
#include <math.h>
#include <stdlib.h>
#include <eeprom.h>
#include "print.h"

// PMW3360 制御用関数のプロトタイプ宣言
extern void pmw3360_set_cpi(uint16_t cpi);
extern bool pmw3360_write(uint8_t reg_addr, uint8_t data);

// PMW3360 診断用：QMK の pmw33xx ドライバが公開しているレジスタ読み出しを使用
extern uint8_t pmw33xx_read(uint8_t sensor, uint8_t reg_addr);
extern bool pmw33xx_write(uint8_t sensor, uint8_t reg_addr, uint8_t data);

// PMW3360 diagnostic registers
#define REG_SQUAL               0x07
#define REG_RAW_DATA_SUM        0x08
#define REG_MAXIMUM_RAW_DATA    0x09
#define REG_MINIMUM_RAW_DATA    0x0A
#define REG_SHUTTER_LOWER       0x0B
#define REG_SHUTTER_UPPER       0x0C
#define REG_OBSERVATION         0x24
#define REG_CONFIG2             0x10
#define REG_MIN_SQ_RUN          0x2B
#define REG_RAW_DATA_THRESHOLD  0x2C

#define PMW3360_DIAG_INTERVAL_MS 100
#define MY_EECONFIG_ADDR ((void*)1000)
#define REG_LIFT_CONFIG 0x63

enum custom_keycodes {
    MY_DRAG_BUTTON = SAFE_RANGE,
    MY_DPI_SWITCH
};

// ==================================================================
// 1. キーマップ配列の定義
// ==================================================================
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        MS_BTN1, MY_DPI_SWITCH, MS_BTN2,
        MS_BTN4, MS_BTN5,       MY_DRAG_BUTTON
    )
};


// ==================================================================
// 2. 設定値とステート（状態）の管理
// ==================================================================
typedef struct {
    uint8_t  lod_level;           // 2: 2mm (標準), 3: 3mm (高)
    int16_t  rotation_angle;      // -180 ~ +180
    uint8_t  scroll_speed_div;    // 減速比 (初期値: 8)
    bool     drag_toggle_enable;    
    uint8_t  dpi_steps;       
    uint8_t  current_dpi_idx; 
    uint16_t dpi_values[3];  
    uint8_t  polling_interval_ms; // 1: 1000Hz, 2: 500Hz, 4: 250Hz, 8: 125Hz
    // デモモード（無操作タイマー）設定
    uint8_t  demo_enable;         // 0: 無効, 1: 有効
    uint16_t demo_timeout_s;      // 移行までの無操作時間（秒）
    uint8_t  demo_effect;         // エフェクトの種類
    // スクロール上下反転設定
    bool     scroll_invert_v;     // false: 通常 / true: 反転

} my_config_t;

my_config_t g_my_config = {
    2, 0, 8, false,
    3, 0, {800, 1200, 1600},
    1,
    1, 60,                        
    RGBLIGHT_MODE_RAINBOW_SWIRL,
    false
};

bool g_drag_scroll_active = false;
uint16_t g_last_polling_timer = 0;

// PMW3360 診断データ（EEPROMには保存しない）
typedef struct {
    uint8_t  squal;
    uint8_t  raw_data_sum;
    uint8_t  maximum_raw_data;
    uint8_t  minimum_raw_data;
    uint16_t shutter;
    uint8_t  observation;
    bool     enabled;
} pmw3360_diag_t;

static pmw3360_diag_t g_pmw3360_diag = {0};
static float accumulated_raw_x = 0.0f;
static float accumulated_raw_y = 0.0f;
static float scroll_remainder_x = 0.0f;
static float scroll_remainder_y = 0.0f;
static bool dpi_flash_active = false;
static uint16_t dpi_flash_timer = 0;
static uint8_t dpi_flash_count = 0;
static bool dpi_flash_led_on = false;
static uint32_t g_last_activity_timer = 0;
static bool g_is_demo_active = false;
static float g_sin_a = 0.0f;
static float g_cos_a = 1.0f;

// ------------------------------------------------------------------
// ヘルパー関数
// ------------------------------------------------------------------
static inline int8_t clamp_int8(int16_t val) {
    if (val > 127) return 127;
    if (val < -127) return -127;
    return (int8_t)val;
}

void update_rotation_trig(void) {

    if (g_my_config.rotation_angle != 0) {
        double rad = (double)g_my_config.rotation_angle * M_PI / 180.0;
        g_sin_a = (float)sin(rad);
        g_cos_a = (float)cos(rad);

    } else {
        g_sin_a = 0.0f;
        g_cos_a = 1.0f;
    }
}

void update_rgb_by_state(layer_state_t state) {

    if (g_is_demo_active || dpi_flash_active) return;

    rgblight_mode_noeeprom(RGBLIGHT_MODE_STATIC_LIGHT);

    if (g_drag_scroll_active) {
        rgblight_sethsv_noeeprom(0, 255, 64);
        return;
    }

    switch (get_highest_layer(state)) {
        case 1: rgblight_sethsv(0,   0,   64); break;
        case 2: rgblight_sethsv(36,  255, 64); break;
        case 3: rgblight_sethsv(72,  255, 64); break;
        case 4: rgblight_sethsv(108, 255, 64); break;
        case 5: rgblight_sethsv(144, 255, 64); break;
        case 6: rgblight_sethsv(180, 255, 64); break;
        case 7: rgblight_sethsv(216, 255, 64); break;
        default:
            rgblight_sethsv(0, 0, 0);
            break;
    }
}

void notify_user_activity(void) {
    g_last_activity_timer = timer_read32();

    if (g_is_demo_active) {
        g_is_demo_active = false;
        update_rgb_by_state(layer_state);
    }
}

layer_state_t layer_state_set_user(layer_state_t state) {
    update_rgb_by_state(state);
    return state;
}

void trigger_dpi_flash(void) {
    // DPI段階に応じて 1～3 回点滅する
    dpi_flash_active = true;
    dpi_flash_timer = timer_read();
    dpi_flash_count = g_my_config.current_dpi_idx + 1;

    if (dpi_flash_count > 3) {
        dpi_flash_count = 3;
    }

    dpi_flash_led_on = true;
    rgblight_mode_noeeprom(RGBLIGHT_MODE_STATIC_LIGHT);
    rgblight_sethsv(170, 255, 40);
}

// PMW3360 の診断レジスタを読み取る
// 診断読み出し後に Config2 を同値(0x00)で書き戻して、QMK側の
// pmw33xx burst 状態も通常の読み出しに戻す

static void update_pmw3360_diagnostic(void) {

    if (!g_pmw3360_diag.enabled) return;

    g_pmw3360_diag.squal            = pmw33xx_read(0, REG_SQUAL);
    g_pmw3360_diag.raw_data_sum     = pmw33xx_read(0, REG_RAW_DATA_SUM);
    g_pmw3360_diag.maximum_raw_data = pmw33xx_read(0, REG_MAXIMUM_RAW_DATA);
    g_pmw3360_diag.minimum_raw_data = pmw33xx_read(0, REG_MINIMUM_RAW_DATA);
    uint8_t shutter_lower = pmw33xx_read(0, REG_SHUTTER_LOWER);
    uint8_t shutter_upper = pmw33xx_read(0, REG_SHUTTER_UPPER);
    g_pmw3360_diag.shutter = ((uint16_t)shutter_upper << 8) | shutter_lower;
    g_pmw3360_diag.observation = pmw33xx_read(0, REG_OBSERVATION);

    // pmw33xx_read() 後に通常の Motion Burst を確実に再開させるため、
    // QMKドライバ側の burst 状態をリセットする
    pmw33xx_write(0, REG_CONFIG2, 0x00);
}

void sync_sensor_dpi(void) {
    uint8_t idx = g_my_config.current_dpi_idx;

    if (idx >= g_my_config.dpi_steps) {
        idx = 0;
        g_my_config.current_dpi_idx = 0;
    }

    uint16_t target_dpi = g_my_config.dpi_values[idx];
    pmw33xx_set_cpi(0, target_dpi);
}

// PMW3360 センサーへの LOD 反映処理

bool sync_sensor_lod(void) {
    // 0x02 = ~2mm (標準), 0x03 = ~3mm (高)
    uint8_t reg_val = (g_my_config.lod_level == 3) ? 0x03 : 0x02;
    return pmw33xx_write(0, REG_LIFT_CONFIG, reg_val);
}

// ==================================================================
// 3. 起動時の処理
// ==================================================================

void keyboard_post_init_user(void) {
    eeprom_read_block(&g_my_config, MY_EECONFIG_ADDR, sizeof(my_config_t));

    if (g_my_config.lod_level == 0xFF || g_my_config.dpi_steps == 0xFF || 
        g_my_config.polling_interval_ms == 0xFF || g_my_config.polling_interval_ms == 0) {
        g_my_config.lod_level           = 2;
        g_my_config.rotation_angle      = 0;
        g_my_config.scroll_speed_div    = 8;
        g_my_config.drag_toggle_enable  = false;
        g_my_config.dpi_steps           = 3;
        g_my_config.current_dpi_idx     = 0;
        g_my_config.dpi_values[0]       = 800;
        g_my_config.dpi_values[1]       = 1200;
        g_my_config.dpi_values[2]       = 1600;
        g_my_config.polling_interval_ms = 1; 
        g_my_config.demo_enable         = 1;  
        g_my_config.demo_timeout_s      = 60; 
        g_my_config.demo_effect         = RGBLIGHT_MODE_RAINBOW_SWIRL;
        g_my_config.scroll_invert_v     = false;
    }
    sync_sensor_dpi();
    sync_sensor_lod(); // 起動時にセンサーへ LOD を反映
    update_rotation_trig(); 
    g_last_polling_timer  = timer_read();
    g_last_activity_timer = timer_read32();
    rgblight_mode_noeeprom(RGBLIGHT_MODE_STATIC_LIGHT);
    rgblight_sethsv(0, 0, 0);
}

// ==================================================================
// 4. Raw HID パケット処理
// ==================================================================
void my_raw_hid_handler(uint8_t *data, uint8_t length) {

    if (length > 0 && data[0] == 0x3A) {
        // 0x01: 設定の書き込み（保存）
        if (length >= 2 && data[1] == 0x01) {
            g_my_config.lod_level          = data[2];
            g_my_config.rotation_angle     = (int16_t)(((uint16_t)data[3] << 8) | data[4]);
            g_my_config.scroll_speed_div   = data[5];
            g_my_config.drag_toggle_enable = (data[6] == 1);
            g_my_config.dpi_steps          = data[7];
            g_my_config.dpi_values[0]      = ((uint16_t)data[8] << 8) | data[9];
            g_my_config.dpi_values[1]      = ((uint16_t)data[10] << 8) | data[11];
            g_my_config.dpi_values[2]      = ((uint16_t)data[12] << 8) | data[13];
            g_my_config.polling_interval_ms = data[14]; 
            g_my_config.demo_enable        = data[15];
            g_my_config.demo_timeout_s     = ((uint16_t)data[16] << 8) | data[17];
            g_my_config.demo_effect        = data[18];
            g_my_config.scroll_invert_v    = (data[19] == 1);

            eeprom_update_block(&g_my_config, MY_EECONFIG_ADDR, sizeof(my_config_t));
            sync_sensor_dpi();
            sync_sensor_lod();
            update_rotation_trig(); 

            g_drag_scroll_active = false;
            accumulated_raw_x  = 0.0f;
            accumulated_raw_y  = 0.0f;
            scroll_remainder_x = 0.0f;
            scroll_remainder_y = 0.0f;
            g_is_demo_active = false;
            update_rgb_by_state(layer_state);

            // 書き込み成功時に現在の設定を返す (0x99)
            uint8_t response[32] = {0};
            response[0] = 0x3A;
            response[1] = 0x99;
            response[2] = g_my_config.lod_level;
            response[3] = (uint8_t)((uint16_t)g_my_config.rotation_angle >> 8);
            response[4] = (uint8_t)(g_my_config.rotation_angle & 0xFF);
            response[5] = g_my_config.scroll_speed_div;
            response[6] = g_my_config.drag_toggle_enable ? 1 : 0;
            response[7] = g_my_config.dpi_steps;
            response[8] = (uint8_t)(g_my_config.dpi_values[0] >> 8);
            response[9] = (uint8_t)(g_my_config.dpi_values[0] & 0xFF);
            response[10] = (uint8_t)(g_my_config.dpi_values[1] >> 8);
            response[11] = (uint8_t)(g_my_config.dpi_values[1] & 0xFF);
            response[12] = (uint8_t)(g_my_config.dpi_values[2] >> 8);
            response[13] = (uint8_t)(g_my_config.dpi_values[2] & 0xFF);
            response[14] = g_my_config.polling_interval_ms; 
            response[15] = g_my_config.demo_enable;
            response[16] = (uint8_t)(g_my_config.demo_timeout_s >> 8);
            response[17] = (uint8_t)(g_my_config.demo_timeout_s & 0xFF);
            response[18] = g_my_config.demo_effect;
            response[19] = g_my_config.scroll_invert_v ? 1 : 0;

            host_raw_hid_send(response, 32);
            return;
        }

        // 0x02: 設定の読み出し要求
        if (length >= 2 && data[1] == 0x02) {
            uint8_t response[32] = {0};
            response[0] = 0x3A;
            response[1] = 0x99; // 読み出し応答用の識別子
            response[2] = g_my_config.lod_level;
            response[3] = (uint8_t)((uint16_t)g_my_config.rotation_angle >> 8);
            response[4] = (uint8_t)(g_my_config.rotation_angle & 0xFF);
            response[5] = g_my_config.scroll_speed_div;
            response[6] = g_my_config.drag_toggle_enable ? 1 : 0;
            response[7] = g_my_config.dpi_steps;
            response[8] = (uint8_t)(g_my_config.dpi_values[0] >> 8);
            response[9] = (uint8_t)(g_my_config.dpi_values[0] & 0xFF);
            response[10] = (uint8_t)(g_my_config.dpi_values[1] >> 8);
            response[11] = (uint8_t)(g_my_config.dpi_values[1] & 0xFF);
            response[12] = (uint8_t)(g_my_config.dpi_values[2] >> 8);
            response[13] = (uint8_t)(g_my_config.dpi_values[2] & 0xFF);            
            response[14] = g_my_config.polling_interval_ms;
            response[15] = g_my_config.demo_enable;
            response[16] = (uint8_t)(g_my_config.demo_timeout_s >> 8);
            response[17] = (uint8_t)(g_my_config.demo_timeout_s & 0xFF);
            response[18] = g_my_config.demo_effect;
            response[19] = g_my_config.scroll_invert_v ? 1 : 0;

            host_raw_hid_send(response, 32);
            return;
        }

        // 0x03: PMW3360 診断モード制御（測定開始/停止）
        if (length >= 2 && data[1] == 0x03) {

            if (length >= 3) {
                g_pmw3360_diag.enabled = (data[2] != 0);
            }
            if (g_pmw3360_diag.enabled) {
                update_pmw3360_diagnostic();
            }

            uint8_t response[32] = {0};
            response[0] = 0x3A;
            response[1] = 0x92; // 診断応答用の識別子
            response[2] = g_pmw3360_diag.enabled ? 1 : 0;
            response[3] = g_pmw3360_diag.squal;
            response[4] = g_pmw3360_diag.raw_data_sum;
            response[5] = g_pmw3360_diag.maximum_raw_data;
            response[6] = g_pmw3360_diag.minimum_raw_data;
            response[7] = (uint8_t)(g_pmw3360_diag.shutter >> 8);
            response[8] = (uint8_t)(g_pmw3360_diag.shutter & 0xFF);
            response[9] = g_pmw3360_diag.observation;

            host_raw_hid_send(response, 32);
            return;
        }
    }
}

// ==================================================================
// 5. ボタンイベント処理
// ==================================================================
bool process_record_user(uint16_t keycode, keyrecord_t *record) {

    if (record->event.pressed) {
        notify_user_activity();
    }

    switch (keycode) {
        case MY_DRAG_BUTTON:

            if (g_my_config.drag_toggle_enable) {

                if (record->event.pressed) {
                    g_drag_scroll_active = !g_drag_scroll_active;
                    scroll_remainder_x = 0.0f;
                    scroll_remainder_y = 0.0f;
                    update_rgb_by_state(layer_state);
                }

            } else {

                if (record->event.pressed) {
                    g_drag_scroll_active = true;

                } else {
                    g_drag_scroll_active = false;
                }
                scroll_remainder_x = 0.0f;
                scroll_remainder_y = 0.0f;
                update_rgb_by_state(layer_state);
            }
            return false;
            
        case MY_DPI_SWITCH:

            if (record->event.pressed) {
                g_my_config.current_dpi_idx++;

                if (g_my_config.current_dpi_idx >= g_my_config.dpi_steps) {
                    g_my_config.current_dpi_idx = 0;
                }
                sync_sensor_dpi();
                trigger_dpi_flash();
            }
            return false;            

        default:
            return true;
    }
}

// ==================================================================
// 6. ループ監視
// ==================================================================

void matrix_scan_user(void) {

    if (dpi_flash_active) {
        // 100msごとにON/OFFを切り替える
        if (timer_elapsed(dpi_flash_timer) >= 100) {
            dpi_flash_timer = timer_read();

            if (dpi_flash_led_on) {
                // 点灯 → 消灯
                rgblight_sethsv_noeeprom(0, 0, 0);
                dpi_flash_led_on = false;

            } else {
                // 消灯 → 次の点灯
                if (dpi_flash_count > 0) {
                    dpi_flash_count--;
                }

                if (dpi_flash_count == 0) {
                    // 点滅終了
                    dpi_flash_active = false;
                    update_rgb_by_state(layer_state);

                } else {
                    rgblight_sethsv_noeeprom(170, 255, 40);
                    dpi_flash_led_on = true;
                }
            }
        }
    }

    if (g_my_config.demo_enable && !g_is_demo_active) {
        uint32_t timeout_ms = (uint32_t)g_my_config.demo_timeout_s * 1000;

        if (timer_elapsed32(g_last_activity_timer) > timeout_ms) {
            g_is_demo_active = true;
            rgblight_enable_noeeprom();
            rgblight_sethsv_noeeprom(0, 255, 128);
            rgblight_mode_noeeprom(g_my_config.demo_effect); 
        }
    }
}

// ==================================================================
// 7. ボール動作
// ==================================================================

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    accumulated_raw_x += (float)mouse_report.x;
    accumulated_raw_y += (float)mouse_report.y;

    if (g_my_config.polling_interval_ms > 1) {

        if (timer_elapsed(g_last_polling_timer) < g_my_config.polling_interval_ms) {
            mouse_report.x = 0;
            mouse_report.y = 0;
            return mouse_report; 
        }
        uint16_t elapsed = timer_elapsed(g_last_polling_timer);

        if (elapsed > g_my_config.polling_interval_ms * 2) {
            g_last_polling_timer = timer_read();

        } else {
            g_last_polling_timer += g_my_config.polling_interval_ms;
        }
    }

    if (accumulated_raw_x == 0.0f && accumulated_raw_y == 0.0f) {
        mouse_report.x = 0;
        mouse_report.y = 0;
        return mouse_report;
    }

    if (fabsf(accumulated_raw_x) > 1.0f || fabsf(accumulated_raw_y) > 1.0f) {
        notify_user_activity();
    }
    float raw_x = accumulated_raw_x;
    float raw_y = accumulated_raw_y;

    // A. 角度補正処理
    float rot_x = raw_x;
    float rot_y = raw_y;

    if (g_my_config.rotation_angle != 0) {
        rot_x = raw_x * g_cos_a - raw_y * g_sin_a;
        rot_y = raw_x * g_sin_a + raw_y * g_cos_a;
    }
    // B. ドラッグスクロール処理
    if (g_drag_scroll_active) {
        float div = (g_my_config.scroll_speed_div == 0) ? 1.0f : (float)g_my_config.scroll_speed_div;
        float total_x = rot_x + scroll_remainder_x;
        float total_y = rot_y + scroll_remainder_y;
        float y_dir = g_my_config.scroll_invert_v ? total_y : -total_y;
        int8_t scroll_v = clamp_int8((int16_t)lroundf(y_dir / div));
        int8_t scroll_h = clamp_int8((int16_t)lroundf(total_x / div));
        mouse_report.v = scroll_v;
        mouse_report.h = scroll_h;
        float sent_y = g_my_config.scroll_invert_v ? (float)scroll_v : (float)(-scroll_v);
        scroll_remainder_y = total_y - (sent_y * div);
        scroll_remainder_x = total_x - ((float)scroll_h * div);
        mouse_report.x = 0;
        mouse_report.y = 0;        
        accumulated_raw_x = 0.0f;
        accumulated_raw_y = 0.0f;

    } else {
        // C. 通常カーソル移動
        int8_t send_x = clamp_int8((int16_t)lroundf(rot_x));
        int8_t send_y = clamp_int8((int16_t)lroundf(rot_y));
        mouse_report.x = send_x;
        mouse_report.y = send_y;
        float sent_raw_x = (float)send_x * g_cos_a + (float)send_y * g_sin_a;
        float sent_raw_y = -(float)send_x * g_sin_a + (float)send_y * g_cos_a;
        accumulated_raw_x -= sent_raw_x;
        accumulated_raw_y -= sent_raw_y;
    }
    return mouse_report;
} 
