#include "input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <bcm2835.h>

#define INPUT_PIN 4

struct input
{
	uint64_t change_time;
	uint16_t value;
	uint8_t new_weight;
	uint8_t old_weight;
	uint8_t state;
};

static const char *_error;

static uint64_t input_now(void)
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (uint64_t)time.tv_sec * 1000000 + ((uint64_t)time.tv_nsec + 500) / 1000;
}

void input_deinit(input_t input)
{
	if(input)
	{
		free(input);
	}
}

uint16_t input_get(input_t input)
{
	return input->value;
}

input_t input_init(uint8_t old_weight, uint8_t new_weight)
{
	input_t input = 0;
	int result;

	result = bcm2835_init();
	if(!result)
	{
		_error = "Failed to initialize BCM2835 library.";
		goto fail;
	}

	input = malloc(sizeof(struct input));
	if(!input)
	{
		_error = "Failed to allocate input object.";
		goto fail;
	}
	memset(input, 0, sizeof(struct input));

	bcm2835_gpio_fsel(INPUT_PIN, BCM2835_GPIO_FSEL_INPT);
	input->state = bcm2835_gpio_lev(INPUT_PIN);

	input->change_time = input_now();
	input->new_weight = new_weight;
	input->old_weight = old_weight;

	return input;

fail:	
	input_deinit(input);
	return 0;
}

void input_update(input_t input)
{
	uint8_t new_state = bcm2835_gpio_lev(INPUT_PIN);
	if(new_state != input->state)
	{
		uint64_t old_time = input->change_time;
		input->change_time = input_now();

		if(!new_state)
		{
			uint64_t delta_time = input->change_time - old_time;
			if(delta_time > 800 && delta_time < 2200)
			{
				uint32_t num = input->old_weight * (uint32_t)input->value + input->new_weight * (uint32_t)delta_time;
				uint16_t denom = (uint16_t)input->old_weight + (uint16_t)input->new_weight;
				input->value = num / denom;
			}
		}

		input->state = new_state;
	}
}
