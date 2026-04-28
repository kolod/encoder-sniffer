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


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "u8g2.h"
#include "ws2812.pio.h"
#include "quadrature_encoder.pio.h"
#include "board.h"

// ---- Quadrature decoder -----------------------------------------------
//
// A/B phases are decoded entirely by two PIO state machines (pio1, sm0/sm1).
// Each SM pushes +1 or -1 (as uint32_t) to its RX FIFO on every valid step.
// The main loop drains the FIFO and accumulates enc->position / ->transitions.
//
// The Z (index) pulse still uses a GPIO interrupt to snapshot the position
// and compute PPR between successive Z pulses.

typedef struct {
    volatile int32_t  position;
    volatile int32_t  rev_start_pos;  // position at last Z pulse
    volatile int32_t  ppr;            // pulses per revolution (0 = unknown)
    volatile uint32_t missed_index;   // Z pulses missed (detected from main loop)
    volatile uint32_t index_count;
    volatile uint32_t transitions;    // total AB steps (used to detect N/C input)
    bool              index_seen;
} encoder_t;

static encoder_t enc_norm;
static encoder_t enc_inv;

// ---- PIO state machines ---------------------------------------------------
// ws2812 uses pio0; both encoder SMs share the single program loaded in pio1.

static PIO  enc_pio;
static uint enc_program_offset;
static uint enc_norm_sm;
static uint enc_inv_sm;

static void encoders_pio_init(void) {
    enc_pio            = pio1;
    enc_program_offset = pio_add_program(enc_pio, &quadrature_encoder_program);

    // Normal and inverted encoders can share the same program
    enc_norm_sm = pio_claim_unused_sm(enc_pio, true);
    quadrature_encoder_program_init(enc_pio, enc_norm_sm, enc_program_offset, ENCODER_NORMAL_PHASE_A_PIN);

    // Inverted encoder: swap A/B pins in the same program to decode in opposite direction.
    enc_inv_sm = pio_claim_unused_sm(enc_pio, true);
    quadrature_encoder_program_init(enc_pio, enc_inv_sm, enc_program_offset, ENCODER_INVERTED_PHASE_A_PIN);
}

static void encoder_init(encoder_t *enc, uint pin_idx) {
    memset(enc, 0, sizeof(*enc));
    gpio_init(pin_idx);
    gpio_set_dir(pin_idx, GPIO_IN);
    gpio_disable_pulls(pin_idx);
}

// Drain the PIO RX FIFO and accumulate position.  Called every main-loop tick.
static void encoder_poll_pio(encoder_t *enc, uint sm) {
    while (!pio_sm_is_rx_fifo_empty(enc_pio, sm)) {
        int32_t delta = (int32_t)pio_sm_get(enc_pio, sm);
        enc->position += delta;
        enc->transitions++;
    }
}

// Called from GPIO ISR on Z rising edge.
static inline void encoder_z_pulse(encoder_t *enc) {
    int32_t delta = enc->position - enc->rev_start_pos;
    if (enc->index_seen && delta != 0)
        enc->ppr = (int32_t)abs((int)delta);
    enc->rev_start_pos = enc->position;
    enc->index_count++;
    enc->index_seen = true;
}

// Shared GPIO interrupt handler — Z pins only; A/B are handled by PIO.
static void gpio_irq_handler(uint gpio, uint32_t events) {
    if (gpio == ENCODER_NORMAL_INDEX_PIN) {
        encoder_z_pulse(&enc_norm);
    } else if (gpio == ENCODER_INVERTED_INDEX_PIN) {
        encoder_z_pulse(&enc_inv);
    }
}

// Poll from main loop: detect Z pulse missing when position overshot expected.
static void check_missed_z(encoder_t *enc) {
    if (!enc->index_seen || enc->ppr == 0)
        return;
    int32_t since = enc->position - enc->rev_start_pos;
    int32_t threshold = enc->ppr + (enc->ppr >> 1); // 1.5 × PPR
    if (abs((int)since) >= (int)threshold) {
        enc->missed_index++;
        // Advance reference so we don't keep re-counting the same miss.
        enc->rev_start_pos += (since > 0) ? enc->ppr : -enc->ppr;
    }
}

// ---- WS2812 -----------------------------------------------------------

static PIO  ws_pio;
static uint ws_sm;

static void ws2812_init(void) {
    ws_pio        = pio0;
    ws_sm         = pio_claim_unused_sm(ws_pio, true);
    uint offset   = pio_add_program(ws_pio, &ws2812_program);
    ws2812_program_init(ws_pio, ws_sm, offset, WS2812_LED_PIN, 800000.f, false);
}

static void ws2812_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    pio_sm_put_blocking(ws_pio, ws_sm, grb << 8u);
}

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

static void display_init(void) {
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
static void display_update(void) {
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

// ---- main -------------------------------------------------------------

int main(void) {
    stdio_init_all();

    ws2812_init();
    display_init();
    encoders_pio_init();

    encoder_init(&enc_norm, ENCODER_NORMAL_INDEX_PIN);
    encoder_init(&enc_inv,  ENCODER_INVERTED_INDEX_PIN);

    // Only the Z (index) pins need GPIO interrupts; A/B are handled by PIO.
    gpio_set_irq_enabled_with_callback(
        ENCODER_NORMAL_INDEX_PIN,
        GPIO_IRQ_EDGE_RISE, true, gpio_irq_handler);
    gpio_set_irq_enabled(ENCODER_INVERTED_INDEX_PIN,
                         GPIO_IRQ_EDGE_RISE, true);

    uint32_t last_display_ms = 0;

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        encoder_poll_pio(&enc_norm, enc_norm_sm);
        encoder_poll_pio(&enc_inv,  enc_inv_sm);

        check_missed_z(&enc_norm);
        check_missed_z(&enc_inv);

        if (now - last_display_ms >= 100) {
            last_display_ms = now;
            display_update();

            // WS2812 status:  red=error, blue=waiting, green=OK
            if (enc_norm.missed_index > 0)
                ws2812_set_rgb(8, 0, 0);
            else if (!enc_norm.index_seen)
                ws2812_set_rgb(0, 0, 8);
            else
                ws2812_set_rgb(0, 8, 0);
        }

        tight_loop_contents();
    }
}
