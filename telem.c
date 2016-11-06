#include "telem.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct telem
{
	telem_callback_t callback;
	void *callback_data;

	int port;
	uint16_t values[256];

	uint8_t buffer[3];
	uint8_t cells;
	uint8_t escape;
	uint8_t invert;
	uint8_t state;
};

static const char *_error = 0;

void telem_callback(telem_t telem, telem_callback_t callback, void *data)
{
	telem->callback = callback;
	telem->callback_data = data;
}

void telem_close(telem_t telem)
{
	if(telem)
	{
		if(telem->port >= 0) close(telem->port);
		free(telem);
	}
}

const char *telem_error(void)
{
	return _error;
}

int32_t telem_get_altitude(telem_t telem)
{
	int16_t whole = telem->values[0x10];
	uint16_t frac = telem->values[0x21];

	int32_t altitude = (int32_t)whole * 100;
	if(whole >= 0) altitude += frac;
	else altitude -= frac;

	return altitude;
}

uint16_t telem_get_cell_voltage(telem_t telem)
{
	uint16_t raw = telem->values[0x06];
	return ((raw >> 7) & 0x1fe) | ((raw & 0x0f) << 9);
}

uint8_t telem_get_cells(telem_t telem)
{
	return telem->cells;
}

uint16_t telem_get_heading(telem_t telem)
{
	return telem->values[0x14] * 100 + telem->values[0x1c];
}

uint16_t telem_get_raw(telem_t telem, uint8_t id)
{
	return telem->values[id];
}

uint16_t telem_get_vfas_voltage(telem_t telem)
{
	if(telem->values[0x39])
	{
		return telem->values[0x39] * 100;
	}
	else
	{
		// TODO: Low-precision VFAS using 0x3a and 0x3b
		return 0;
	}
}

void telem_invert(telem_t telem, uint8_t invert)
{
	telem->invert = invert;
}

telem_t telem_open(const char *device)
{
	telem_t result = malloc(sizeof(struct telem));
	if(!result) goto fail;
	memset(result, 0, sizeof(struct telem));

	result->port = open(device, O_NDELAY | O_NOCTTY | O_RDONLY);
	if(!result->port)
	{
		_error = strerror(errno);
		goto fail;
	}

	return result;

fail:
	telem_close(result);
	return 0;
}

static uint8_t telem_receive(telem_t telem, uint8_t id, uint16_t value)
{
	uint8_t result = 0;

	switch(id)
	{
		case 0x06:
		{
			uint8_t cell = (value >> 4) & 0x0f;
			if(cell >= telem->cells)
			{
				result++;
				telem->cells = cell + 1;
			}

			break;
		}
	}	

	if(telem->values[id] != value)
	{
		result++;
		telem->values[id] = value;
	}

	if(telem->callback)
		telem->callback(telem->callback_data, id, value);

	return result;
}

uint8_t telem_update(telem_t telem)
{
	ssize_t count;
	uint8_t updates = 0;
	uint8_t ch;

	while((count = read(telem->port, &ch, sizeof(ch))) > 0)
	{
		ch ^= telem->invert;
		if(ch == 0x5d)
		{
			telem->escape = 0x60;
		}
		else if(ch == 0x5e)
		{
			telem->state = 3;
		}
		else if(telem->state > 0)
		{
			ch ^= telem->escape;
			telem->escape = 0;

			telem->buffer[3 - telem->state] = ch;
			telem->state--;

			if(telem->state == 0)
			{
				uint8_t id = telem->buffer[0];
				uint16_t value = ((uint16_t)telem->buffer[1] << 0) | ((uint16_t)telem->buffer[2] << 8);

				if(telem_receive(telem, id, value) && updates < 0xff)
					updates++;
			}
		}
	}

	return updates;
}
