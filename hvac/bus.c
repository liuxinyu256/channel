#include "bus.h"
#include <string.h>

static void tx_task(void *pv) {
    bus_controller_t *bus = (bus_controller_t *)pv;
    uint8_t byte;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (!ring_empty(&bus->tx_queue) && bus->idle) {
            ring_get(&bus->tx_queue, &byte);
            bus->idle = 0;
            bus->phy->write(bus->phy, byte);
        }
    }
}

void bus_init(bus_controller_t *bus, phy_driver_t *phy, uint16_t gap_ms) {
    memset(bus, 0, sizeof(*bus));
    bus->phy    = phy;
    bus->gap_ms = gap_ms;
    bus->idle   = 1;
    ring_init(&bus->tx_queue);
}

void bus_create_tx_task(bus_controller_t *bus, uint16_t stack,
                         UBaseType_t prio) {
    xTaskCreate(tx_task, "TX", stack, bus, prio, &bus->tx_task);
}

int bus_send(bus_controller_t *bus, const uint8_t *frame, uint16_t len) {
    uint16_t wrote = ring_write(&bus->tx_queue, frame, len);
    if (wrote == len) { xTaskNotifyGive(bus->tx_task); return 0; }
    return -1;
}

void bus_on_tx_done(bus_controller_t *bus) {
    if (!ring_empty(&bus->tx_queue)) {
        uint8_t byte;
        ring_get(&bus->tx_queue, &byte);
        bus->phy->write(bus->phy, byte);
    } else {
        bus->gap_until = xTaskGetTickCount() + pdMS_TO_TICKS(bus->gap_ms);
        bus->idle = 1;
        xTaskNotifyGive(bus->tx_task);
    }
}

void bus_on_rx_byte(bus_controller_t *bus, uint8_t byte) {
    (void)byte;
    bus->gap_until = xTaskGetTickCount() + pdMS_TO_TICKS(bus->gap_ms);
    bus->idle = 0;
}
