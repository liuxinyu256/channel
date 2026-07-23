#include "ac.h"
#include "bus.h"
#include <string.h>

void ac_task(void *pv) {
    ac_runtime_t *ac = (ac_runtime_t *)pv;
    event_t ev;

#ifdef FAKE_FREERTOS
    xQueueReceive(ac->evt_queue, &ev, portMAX_DELAY);
#endif
    for (;;) {
#ifndef FAKE_FREERTOS
        xQueueReceive(ac->evt_queue, &ev, portMAX_DELAY);
#endif
        switch (ev.type) {
        case EVENT_PERIODIC_SEND:
            if (ac->evt_table->on_periodic_send)
                ac->evt_table->on_periodic_send(ac);
            break;
        case EVENT_RX_FRAME:
            if (ac->evt_table->on_rx_frame)
                ac->evt_table->on_rx_frame(ac, ev.data, ev.len);
            break;
        case EVENT_CONTROL_CMD:
            if (ac->evt_table->on_control_cmd)
                ac->evt_table->on_control_cmd(ac, ev.cmd_val, ev.cmd_arg);
            break;
        case EVENT_NEED_ACK:
            if (ac->evt_table->on_need_ack)
                ac->evt_table->on_need_ack(ac);
            break;
        case EVENT_SCAN_AC:
            if (ac->evt_table->on_scan)
                ac->evt_table->on_scan(ac);
            break;
        }
#ifdef FAKE_FREERTOS
        return;
#endif
    }
}

void ac_init(ac_runtime_t *ac, bus_controller_t *bus,
             const event_handler_t *table, uint16_t poll_ms) {
    memset(ac, 0, sizeof(*ac));
    ac->evt_table = table;
    ac->bus       = bus;
    ac->poll_period_ms = poll_ms;
}

static void poll_cb(TimerHandle_t t) {
    ac_runtime_t *ac = (ac_runtime_t *)pvTimerGetTimerID(t);
    event_t ev = { .type = EVENT_PERIODIC_SEND };
    ac_post(ac, &ev);
}

void ac_create_task(ac_runtime_t *ac, uint16_t stack, UBaseType_t prio) {
    ac->evt_queue = xQueueCreate(8, sizeof(event_t));
    xTaskCreate(ac_task, "AC", stack, ac, prio, &ac->task);
    /* 周期定时器: 每 poll_period_ms 触发一次轮询 */
    ac->poll_timer = xTimerCreate("poll", pdMS_TO_TICKS(ac->poll_period_ms),
                                   pdTRUE, ac, poll_cb);
    xTimerStart(ac->poll_timer, 0);
}

void ac_post(ac_runtime_t *ac, const event_t *ev) {
    xQueueSend(ac->evt_queue, ev, 0);
}

void ac_post_from_isr(ac_runtime_t *ac, const event_t *ev,
                      BaseType_t *pxWoken) {
    xQueueSendFromISR(ac->evt_queue, ev, pxWoken);
}

void ac_set_poll_period(ac_runtime_t *ac, uint16_t period_ms) {
    ac->poll_period_ms = period_ms;
    xTimerChangePeriod(ac->poll_timer, pdMS_TO_TICKS(period_ms), 0);
}
