#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"

typedef struct {
    volatile int32_t  position;
    volatile int32_t  rev_start_pos;
    volatile int32_t  ppr;
    volatile uint32_t missed_index;
    volatile uint32_t index_count;
    volatile uint32_t transitions;
    bool              index_seen;
} encoder_t;

extern encoder_t enc_norm;
extern encoder_t enc_inv;
extern uint      enc_norm_sm;
extern uint      enc_inv_sm;

void encoders_pio_init(void);
void encoder_init(encoder_t *enc, uint pin_idx);
void encoder_poll_pio(encoder_t *enc, uint sm);
void check_missed_z(encoder_t *enc);
void gpio_irq_handler(uint gpio, uint32_t events);
