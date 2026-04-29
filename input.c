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

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "board.h"
#include "input.h"
#include "input_debounce.pio.h"

// ---- Ring buffer -----------------------------------------------------------
// The PIO RX interrupt drains the PIO FIFO into this buffer immediately,
// so no state transitions are lost even when the main loop is blocked.

#define RINGBUF_SIZE 256u  // must be a power of 2

static uint32_t      ringbuf[RINGBUF_SIZE];
static volatile uint ringbuf_head = 0;  // written by ISR only
static volatile uint ringbuf_tail = 0;  // written by main loop only

static debounce_t g_deb;
static uint32_t   g_prev_state;

static void debounce_rx_irq(void) {
    while (!pio_sm_is_rx_fifo_empty(g_deb.pio, g_deb.sm)) {
        uint32_t v    = pio_sm_get(g_deb.pio, g_deb.sm);
        uint     next = (ringbuf_head + 1u) & (RINGBUF_SIZE - 1u);
        if (next != ringbuf_tail) {
            ringbuf[ringbuf_head] = v;
            ringbuf_head          = next;
        }
    }
}

void input_init(void) {
    // Buttons are on GPIO 26-29 (BUTTON_DOWN_PIN … BUTTON_ESCAPE_PIN).
    // ws2812 already owns pio0 SM0; claim the next free SM on pio0.
    g_prev_state = debounce_setup(&g_deb, pio0, BUTTON_DOWN_PIN, 4, 5000);

    pio_set_irq0_source_enabled(g_deb.pio,
        (pio_interrupt_source_t)(pis_sm0_rx_fifo_not_empty + g_deb.sm), true);
    irq_set_exclusive_handler(PIO0_IRQ_0, debounce_rx_irq);
    irq_set_enabled(PIO0_IRQ_0, true);
}

// ---- Event decoding --------------------------------------------------------

static event_t decode(uint32_t prev, uint32_t next) {
    // Buttons are active-high; detect rising edge (0 → 1 = pressed).
    uint32_t rose = ~prev & next;
    if (rose & (1u << 3)) return EVENT_ESCAPE;
    if (rose & (1u << 2)) return EVENT_ENTER;
    if (rose & (1u << 1)) return EVENT_UP;
    if (rose & (1u << 0)) return EVENT_DOWN;
    return EVENT_NONE;
}

// ---- Poll ------------------------------------------------------------------

event_t input_poll(void) {
    while (ringbuf_tail != ringbuf_head) {
        uint32_t state = ringbuf[ringbuf_tail];
        ringbuf_tail   = (ringbuf_tail + 1u) & (RINGBUF_SIZE - 1u);
        event_t ev     = decode(g_prev_state, state);
        g_prev_state   = state;
        if (ev != EVENT_NONE) return ev;
    }
    return EVENT_NONE;
}
