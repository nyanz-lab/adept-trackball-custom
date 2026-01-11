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

static bool drag_scroll_active = false;
static bool dpi_flash_active = false;
static uint16_t dpi_flash_timer = 0;
static uint8_t dpi_level = 0;

enum custom_keycodes {
    NEXT_LAYER = SAFE_RANGE,
};

static const uint8_t dpi_sat_table[5] = {
    40,
    80,
    130,
    180,
    255
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT( MS_BTN1, MS_BTN2, MS_BTN3, DRAG_SCROLL, DPI_CONFIG, NEXT_LAYER )
};

void update_rgb_by_state(layer_state_t state) {

    if (drag_scroll_active) {
        rgblight_sethsv_noeeprom(0, 255, 64);
        return;
    }

    switch (get_highest_layer(state)) {
        case 1: rgblight_sethsv(0,   0,   64);
                break;
        case 2: rgblight_sethsv(36,  255, 64);
                break;
        case 3: rgblight_sethsv(72,  255, 64);
                break;
        case 4: rgblight_sethsv(108, 255, 64);
                break;
        case 5: rgblight_sethsv(144, 255, 64);
                break;
        case 6: rgblight_sethsv(180, 255, 64);
                break;
        case 7: rgblight_sethsv(216, 255, 64);
                break;
        default:
            rgblight_sethsv(0, 0, 0);
            break;
    }
}

void keyboard_post_init_user(void) {
    rgblight_sethsv(0, 0, 0);
}

layer_state_t layer_state_set_user(layer_state_t state) {
    update_rgb_by_state(state);
    return state;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {

    if (!record->event.pressed) return true;

    switch (keycode) {
        case NEXT_LAYER:
       {
            if (!drag_scroll_active) {
                uint8_t layer = get_highest_layer(layer_state);
                layer = (layer + 1) % 8;
                layer_move(layer);
            }
            return false;
        }
        case DRAG_SCROLL:
        {
            drag_scroll_active = !drag_scroll_active;
            update_rgb_by_state(layer_state);
            return true;
        }
        case DPI_CONFIG:
        {
            dpi_level = (dpi_level + 1) % 5;

            dpi_flash_active = true;
            dpi_flash_timer = timer_read();

            rgblight_sethsv(
                0,
                dpi_sat_table[dpi_level],
                40
            );
            return true;
        }
    }
    return true;
}

void matrix_scan_user(void) {

    if (!dpi_flash_active) return;

    if (timer_elapsed(dpi_flash_timer) > 200) {
        dpi_flash_active = false;
        update_rgb_by_state(layer_state);
    }
}
