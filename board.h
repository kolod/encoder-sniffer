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


#pragma once

// Waveshare RP2040-Zero Pin Definitions

// OLED Display (I2C)
#define OLED_SDA_PIN  0
#define OLED_SCL_PIN  1
#define OLED_I2C_PORT i2c0
#define OLED_I2C_FREQ 400000  // 400 kHz
#define OLED_I2C_ADDR 0x3C    // SSD1306 I2C address

// Buttons
#define BUTTON_ESCAPE_PIN  8
#define BUTTON_ENTER_PIN   9
#define BUTTON_UP_PIN     10
#define BUTTON_DOWN_PIN   11

// Quadrature Encoder - Normal Input
#define ENCODER_NORMAL_PHASE_A_PIN 2
#define ENCODER_NORMAL_PHASE_B_PIN 3
#define ENCODER_NORMAL_INDEX_PIN   4

// Quadrature Encoder - Inverted Input
#define ENCODER_INVERTED_PHASE_A_PIN 5
#define ENCODER_INVERTED_PHASE_B_PIN 6
#define ENCODER_INVERTED_INDEX_PIN   7

// Onboard LED
#define WS2812_LED_PIN 16