#ifndef HVAC_BUS_H
#define HVAC_BUS_H

#include <stdint.h>
#ifdef FAKE_FREERTOS
#include "fake_freertos.h"
#else
#include "FreeRTOS.h"
#include "task.h"
#endif
#include "phy.h"
#include "packet.h"  /* ring_t */

#define BUS_FRAME_DEPTH 8

typedef struct bus_controller {
    phy_driver_t   *phy;
    ring_t          tx_queue;
    uint8_t         tx_buf[PKT_BUF_SIZE];

    TaskHandle_t    tx_task;

    volatile uint8_t idle;
    uint16_t         gap_ms;
    uint32_t         gap_until;
} bus_controller_t;

void bus_init(bus_controller_t *bus, phy_driver_t *phy, uint16_t gap_ms);
void bus_create_tx_task(bus_controller_t *bus, uint16_t stack, UBaseType_t prio);
int  bus_send(bus_controller_t *bus, const uint8_t *frame, uint16_t len);
void bus_on_tx_done(bus_controller_t *bus);
void bus_on_rx_byte(bus_controller_t *bus, uint8_t byte);

#endif
