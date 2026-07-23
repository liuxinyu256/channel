#include "gateway_device.h"
#include "bus.h"
#include <string.h>

void gateway_init(gateway_device_t *gw, bus_controller_t *bus)
{
    memset(gw, 0, sizeof(*gw));
    gw->bus = bus;
}
