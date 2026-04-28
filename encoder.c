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
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "quadrature_encoder.pio.h"
#include "board.h"
#include "encoder.h"

// ---- Quadrature decoder -----------------------------------------------
//
// A/B phases are decoded entirely by two PIO state machines (pio1, sm0/sm1).
// Each SM pushes +1 or -1 (as uint32_t) to its RX FIFO on every valid step.
// The main loop drains the FIFO and accumulates enc->position / ->transitions.
//
// The Z (index) pulse still uses a GPIO interrupt to snapshot the position
// and compute PPR between successive Z pulses.

encoder_t enc_norm;
encoder_t enc_inv;

static PIO  enc_pio;
static uint enc_program_offset;
uint enc_norm_sm;
uint enc_inv_sm;

void encoders_pio_init(void) {
    enc_pio            = pio1;
    enc_program_offset = pio_add_program(enc_pio, &quadrature_encoder_program);

    // Normal and inverted encoders can share the same program
    enc_norm_sm = pio_claim_unused_sm(enc_pio, true);
    quadrature_encoder_program_init(enc_pio, enc_norm_sm, enc_program_offset, ENCODER_NORMAL_PHASE_A_PIN);

    enc_inv_sm = pio_claim_unused_sm(enc_pio, true);
    quadrature_encoder_program_init(enc_pio, enc_inv_sm, enc_program_offset, ENCODER_INVERTED_PHASE_A_PIN);
}

void encoder_init(encoder_t *enc, uint pin_idx) {
    memset(enc, 0, sizeof(*enc));
    gpio_init(pin_idx);
    gpio_set_dir(pin_idx, GPIO_IN);
    gpio_disable_pulls(pin_idx);
}

void encoder_poll_pio(encoder_t *enc, uint sm) {
    while (!pio_sm_is_rx_fifo_empty(enc_pio, sm)) {
        int32_t delta = (int32_t)pio_sm_get(enc_pio, sm);
        enc->position += delta;
        enc->transitions++;
    }
}

static inline void encoder_z_pulse(encoder_t *enc) {
    int32_t delta = enc->position - enc->rev_start_pos;
    if (enc->index_seen && delta != 0)
        enc->ppr = (int32_t)abs((int)delta);
    enc->rev_start_pos = enc->position;
    enc->index_count++;
    enc->index_seen = true;
}

// Shared GPIO interrupt handler — Z pins only; A/B are handled by PIO.
void gpio_irq_handler(uint gpio, uint32_t events) {
    if (gpio == ENCODER_NORMAL_INDEX_PIN) {
        encoder_z_pulse(&enc_norm);
    } else if (gpio == ENCODER_INVERTED_INDEX_PIN) {
        encoder_z_pulse(&enc_inv);
    }
}

void check_missed_z(encoder_t *enc) {
    if (!enc->index_seen || enc->ppr == 0)
        return;
    int32_t since = enc->position - enc->rev_start_pos;
    int32_t threshold = enc->ppr + (enc->ppr >> 1); // 1.5 × PPR
    if (abs((int)since) >= (int)threshold) {
        enc->missed_index++;
        enc->rev_start_pos += (since > 0) ? enc->ppr : -enc->ppr;
    }
}
