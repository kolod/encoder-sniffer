// Encoder sniffer for Raspberry Pi Pico
// Copyright (C) 2026-...  Oleksandr Kolodkin <oleksandr.kolodkin@ukr.net>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "u8g2.h"
#include "board.h"
#include "encoder.h"
#include "display.h"

// ---- u8g2 I2C callbacks -----------------------------------------------

static uint8_t  i2c_buf[256];
static uint16_t i2c_buf_len;

static uint8_t u8x8_pico_i2c_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg, void *argp) {
    switch (msg) {
        case U8X8_MSG_BYTE_START_TRANSFER:
            i2c_buf_len = 0;
            break;
        case U8X8_MSG_BYTE_SEND: {
            const uint8_t *p = (const uint8_t *)argp;
            while (arg-- > 0)
                i2c_buf[i2c_buf_len++] = *p++;
            break;
        }
        case U8X8_MSG_BYTE_END_TRANSFER:
            i2c_write_blocking(OLED_I2C_PORT,
                               u8x8_GetI2CAddress(u8x8) >> 1,
                               i2c_buf, i2c_buf_len, false);
            break;
        case U8X8_MSG_BYTE_INIT:
        case U8X8_MSG_BYTE_SET_DC:
            break;
        default:
            return 0;
    }
    return 1;
}

static uint8_t u8x8_pico_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg, void *argp) {
    (void)u8x8; (void)argp;
    switch (msg) {
        case U8X8_MSG_DELAY_100NANO: __asm volatile("nop");  break;
        case U8X8_MSG_DELAY_10MICRO: sleep_us(10);           break;
        case U8X8_MSG_DELAY_MILLI:   sleep_ms(arg);          break;
        case U8X8_MSG_DELAY_I2C:     sleep_us(5);            break;
        case U8X8_MSG_GPIO_I2C_CLOCK:
        case U8X8_MSG_GPIO_I2C_DATA: break;
        default: return 0;
    }
    return 1;
}

// ---- Display ----------------------------------------------------------

static u8g2_t u8g2;

void display_init(void) {
    i2c_init(OLED_I2C_PORT, OLED_I2C_FREQ);
    gpio_set_function(OLED_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(OLED_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_SDA_PIN);
    gpio_pull_up(OLED_SCL_PIN);

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &u8g2, U8G2_R0, u8x8_pico_i2c_cb, u8x8_pico_delay_cb);
    u8g2_SetI2CAddress(&u8g2, (uint8_t)(OLED_I2C_ADDR << 1));
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
}

// 128×64, font 6×10: up to 21 chars per line, 5 lines with 12px spacing.
void display_update(void) {
    // Snapshot volatile fields once.
    int32_t  pos      = enc_norm.position;
    int32_t  inv_pos  = enc_inv.position;
    int32_t  ppr      = enc_norm.ppr;
    uint32_t z_cnt    = enc_norm.index_count;
    uint32_t miss_z   = enc_norm.missed_index;
    bool     norm_ok  = enc_norm.index_seen;
    bool     inv_nc   = (enc_inv.transitions == 0 && enc_norm.transitions > 50);

    char line[22];

    u8g2_ClearBuffer(&u8g2);

    // Row 1: normal encoder position
    snprintf(line, sizeof(line), "POS:%+ld", (long)pos);
    u8g2_DrawStr(&u8g2, 0, 11, line);

    // Row 2: inverted encoder position (or N/C)
    if (inv_nc)
        snprintf(line, sizeof(line), "INV: N/C");
    else
        snprintf(line, sizeof(line), "INV:%+ld", (long)inv_pos);
    u8g2_DrawStr(&u8g2, 0, 23, line);

    // Row 3: PPR and index count
    if (ppr > 0)
        snprintf(line, sizeof(line), "PPR:%-5ld Z:%-4lu", (long)ppr, (unsigned long)z_cnt);
    else
        snprintf(line, sizeof(line), "PPR:??? Z:%-4lu", (unsigned long)z_cnt);
    u8g2_DrawStr(&u8g2, 0, 35, line);

    // Row 4: missed Z pulses
    snprintf(line, sizeof(line), "MZ:%-4lu", (unsigned long)miss_z);
    u8g2_DrawStr(&u8g2, 0, 47, line);

    // Row 5: status
    const char *status;
    if (miss_z > 0)
        status = "ERR: MISSED Z PULSE";
    else if (!norm_ok)
        status = "Waiting for Z...";
    else
        status = "OK";
    u8g2_DrawStr(&u8g2, 0, 59, status);

    u8g2_SendBuffer(&u8g2);
}
