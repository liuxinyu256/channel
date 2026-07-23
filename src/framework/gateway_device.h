#ifndef HVAC_AC_H
#define HVAC_AC_H

#include <stdint.h>
#ifdef FAKE_FREERTOS
#include "fake_freertos.h"
#else
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#endif

typedef struct bus_controller bus_controller_t;

/* ---- 能力描述 ---- */
typedef struct {
    uint8_t temp_min, temp_max;
    uint8_t fan_levels;
    uint8_t mode_mask;
    uint8_t has_swing, has_sleep, has_eco;
} ac_ability_t;

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

/* ---- 事件表 (品牌填空) ---- */
typedef struct {
    void (*on_rx_byte)      (uint8_t byte, void *ctx);
    void (*on_periodic_send)(void *ctx);
    int  (*on_rx_frame)     (void *ctx, uint8_t *data, uint16_t len);
    void (*on_control_cmd)  (void *ctx, uint8_t cmd, uint8_t val);
    void (*on_need_ack)     (void *ctx);
    void (*on_scan)         (void *ctx);
} event_handler_t;

/* ---- 网关设备基类 ---- */
typedef struct {
    const event_handler_t *evt_table;
    bus_controller_t *bus;

    TimerHandle_t  poll_timer;
    uint16_t       poll_period_ms;

    QueueHandle_t  evt_queue;
    TaskHandle_t   task;
} gateway_device_t;

/* ---- 框架 API ---- */
void ac_task(void *pv);
void ac_init(gateway_device_t *ac, bus_controller_t *bus,
             const event_handler_t *table, uint16_t poll_ms);
void ac_create_task(gateway_device_t *ac, uint16_t stack, UBaseType_t prio);
void ac_post(gateway_device_t *ac, const event_t *ev);
void ac_post_from_isr(gateway_device_t *ac, const event_t *ev,
                      BaseType_t *pxHigherPriorityTaskWoken);
void ac_set_poll_period(gateway_device_t *ac, uint16_t period_ms);

#endif
