#ifndef GATEWAY_DEVICE_H
#define GATEWAY_DEVICE_H

#include <stdint.h>
#ifdef FAKE_FREERTOS
#include "fake_freertos.h"
#else
#include "FreeRTOS.h"
#endif

typedef struct bus_controller bus_controller_t;

/* ---- 事件类型 ---- */
typedef enum {
    EVENT_PERIODIC_SEND,
    EVENT_RX_FRAME,
    EVENT_CONTROL_CMD,
    EVENT_NEED_ACK,
    EVENT_SCAN_AC,
} event_type_t;

/* ---- 事件包 ---- */
typedef struct {
    event_type_t  type;
    uint8_t      *data;
    uint16_t      len;
    uint8_t       cmd_val;
    uint8_t       cmd_arg;
} event_t;

/* ---- 事件表 ---- */
typedef struct {
    void (*on_rx_byte)      (uint8_t byte, void *ctx);
    void (*on_periodic_send)(void *ctx);
    int  (*on_rx_frame)     (void *ctx, uint8_t *data, uint16_t len);
    int  (*on_rx_isr)       (void *ctx, uint8_t *data, uint16_t len);
    void (*on_control_cmd)  (void *ctx, uint8_t cmd, uint8_t val);
    void (*on_need_ack)     (void *ctx);
    void (*on_scan)         (void *ctx);
} event_handler_t;

/* ---- 网关设备 ---- */
typedef struct {
    bus_controller_t *bus;
} gateway_device_t;

/* ---- API ---- */
void gateway_init(gateway_device_t *gw, bus_controller_t *bus);

#endif
