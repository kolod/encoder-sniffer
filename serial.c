// Encoder emulator for Raspberry Pi Pico
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

// Commands (newline-terminated, case-sensitive):
//   pos <n>      set target position   (steps)
//   spd <n>      set target speed      (steps/s)
//   accel <n>    set acceleration      (steps/s²)
//   decel <n>    set deceleration      (steps/s²)
//   ppr <n>      set pulses/revolution
//   incr <n>     set encoder increment per detent
//   linear <0|1> 0=rotary  1=linear
//   mode <0|1>   0=position  1=speed
//   reset        move to 0 and zero current position immediately
//   get          print all params and current state
//   uid          print unique board ID (8-byte hex)
//   boot         reboot into BOOTSEL (programming) mode
//   help / ?     show this list

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/unique_id.h"
#include "serial.h"

static void serial_handle(const char *line) {
    while (*line == ' ') line++;
    if (*line == '\0') return;

    char kw[16] = {0};
    int  ki = 0;
    while (*line && *line != ' ' && ki < 15) kw[ki++] = *line++;
    while (*line == ' ') line++;

    if (!strcmp(kw, "reset")) {
        // Zero the current position immediately, without ramping.
        printf("ok: position reset to 0\r\n");
        return;
    }

    if (!strcmp(kw, "uid")) {
        pico_unique_board_id_t id;
        pico_get_unique_board_id(&id);
        printf("uid=");
        for (int j = 0; j < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; j++)
            printf("%02x", id.id[j]);
        printf("\r\n");
        return;
    }

    if (!strcmp(kw, "boot")) {
        printf("Rebooting into BOOTSEL mode...\r\n");
        reset_usb_boot(0, 0);
    }
    
    if (!strcmp(kw, "help") || !strcmp(kw, "?")) {
        printf("Commands:\r\n"
               "  pos <n>      target position (steps)\r\n"
               "  spd <n>      target speed (steps/s)\r\n"
               "  accel <n>    acceleration (steps/s^2)\r\n"
               "  decel <n>    deceleration (steps/s^2)\r\n"
               "  ppr <n>      pulses per revolution\r\n"
               "  incr <n>     encoder increment per detent\r\n"
               "  linear <0|1> 0=rotary 1=linear\r\n"
               "  mode <0|1>   0=position 1=speed\r\n"
               "  reset        zero position immediately\r\n"
               "  get          print current state\r\n"
               "  uid          print unique board ID (8-byte hex)\r\n"
               "  boot         reboot into BOOTSEL (programming) mode\r\n");
        return;
    }

    printf("err: unknown command '%s' (try 'help')\r\n", kw);
}

static char serial_buf[64];
static int  serial_len = 0;

void serial_poll(void) {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            if (serial_len > 0) {
                serial_buf[serial_len] = '\0';
                printf("\r\n");
                serial_handle(serial_buf);
                serial_len = 0;
            }
        } else if (c == '\b' || c == 127) {
            if (serial_len > 0) { serial_len--; printf("\b \b"); }
        } else if (serial_len < (int)sizeof(serial_buf) - 1) {
            serial_buf[serial_len++] = (char)c;
            putchar(c);
        }
    }
}
