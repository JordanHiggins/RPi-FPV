#pragma once

#include <stdint.h>

typedef struct osd *osd_t;

osd_t osd_init(void);
void osd_deinit(osd_t osd);
const char *osd_error(void);

void osd_set_altitude(osd_t osd, int32_t altitude);
void osd_set_heading(osd_t osd, uint16_t heading);
void osd_set_recording(osd_t osd, uint8_t recording);
void osd_set_voltage(osd_t osd, uint16_t voltage, uint8_t cells);
void osd_update(osd_t osd);
