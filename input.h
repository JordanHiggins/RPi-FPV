#pragma once

#include <stdint.h>

typedef struct input *input_t;

input_t input_init(uint8_t old_weight, uint8_t new_weight);
void input_deinit(input_t input);

uint16_t input_get(input_t input);
void input_update(input_t input);
