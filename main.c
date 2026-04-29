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
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "board.h"
#include "encoder.h"
#include "ws2812.h"
#include "display.h"
#include "serial.h"
#include "input.h"

int main(void) {
    stdio_init_all();

    ws2812_init();
    display_init();
    encoders_pio_init();
    input_init();

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
        serial_poll();
        event_t ev = input_poll();
        if (ev != EVENT_NONE) {
            // For demonstration, just print the event. In a real application,
            // you might want to handle it (e.g. reset position on ENTER).
            const char *ev_str = "UNKNOWN";
            switch (ev) {
                case EVENT_ESCAPE: ev_str = "ESCAPE"; break;
                case EVENT_ENTER:  ev_str = "ENTER";  break;
                case EVENT_UP:     ev_str = "UP";     break;
                case EVENT_DOWN:   ev_str = "DOWN";   break;
                default: break;
            }
            printf("Event: %s\r\n", ev_str);
        }

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
