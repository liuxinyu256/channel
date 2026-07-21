#ifndef HVAC_SCAN_H
#define HVAC_SCAN_H

#include "ac.h"

void scan_start(ac_device_t *ac, const event_handler_t **brands,
                uint8_t count);
int  scan_try_match(ac_device_t *ac, uint8_t *data, uint16_t len);
void scan_lock(ac_device_t *ac, const event_handler_t *table);

#endif
