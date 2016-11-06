#pragma once

#include <stdint.h>

typedef void (*telem_callback_t)(void *, uint8_t, uint16_t);
typedef struct telem *telem_t;

telem_t telem_open(const char *device);
void telem_close(telem_t telem);
const char *telem_error(void);

void telem_callback(telem_t telem, telem_callback_t callback, void *data);
uint16_t telem_get_raw(telem_t telem, uint8_t id);
void telem_invert(telem_t telem, uint8_t invert);
uint8_t telem_update(telem_t telem);

int32_t telem_get_altitude(telem_t telem);
uint16_t telem_get_cell_voltage(telem_t telem);
uint8_t telem_get_cells(telem_t telem);
uint16_t telem_get_heading(telem_t telem);
uint16_t telem_get_vfas_voltage(telem_t telem);
