#include "brand_manager.h"
#include "bus.h"
#include "phy_uart1.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

#define MAX_BRANDS 8

extern const event_handler_t brand_manager_scan_table;

static struct {
    bus_controller_t     bus;
    phy_driver_t        *phy;
    gateway_device_t     gw;

    const brand_config_t *configs[MAX_BRANDS];
    const brand_config_t *active;
    uint8_t              count;
    uint8_t              index;
    uint8_t              locked;

    /* AC 品牌模块自己的任务/队列/定时器 */
    TaskHandle_t         task;
    QueueHandle_t        queue;
    TimerHandle_t        poll_timer;
    uint16_t             poll_period_ms;
} g_brand;

/* ---- 扫描期 evt_table 的持有者 ---- */
static const event_handler_t *g_evt_table;

/* ---- 帧入口(ISR): 紧急帧直接应答, 普通帧入队 ---- */
void brand_manager_on_frame(uint8_t *data, uint16_t length)
{
    const event_handler_t *handler = g_evt_table;

    /* 1. 紧急检查(ISR 中直接处理, <1.5ms 应答) */
    if (handler && handler->on_rx_isr) {
        if (handler->on_rx_isr(&g_brand.gw, data, length)) {
            bus_on_rx_done(&g_brand.bus);
            return;   /* 已应急处理, 不入队 */
        }
    }

    /* 2. 普通处理 → 入队 */
    BaseType_t woken = pdFALSE;
    event_t event = { .type = EVENT_RX_FRAME, .data = data, .len = length };
    xQueueSendFromISR(g_brand.queue, &event, &woken);
    bus_on_rx_done(&g_brand.bus);
    portYIELD_FROM_ISR(woken);
}

/* ---- AC 任务(事件循环) ---- */
static void brand_task(void *pv)
{
    (void)pv;
    event_t ev;
    for (;;) {
        xQueueReceive(g_brand.queue, &ev, portMAX_DELAY);
        if (g_evt_table) {
            switch (ev.type) {
            case EVENT_PERIODIC_SEND:
                if (g_evt_table->on_periodic_send)
                    g_evt_table->on_periodic_send(&g_brand.gw);
                break;
            case EVENT_RX_FRAME:
                if (g_evt_table->on_rx_frame)
                    g_evt_table->on_rx_frame(&g_brand.gw, ev.data, ev.len);
                break;
            case EVENT_CONTROL_CMD:
                if (g_evt_table->on_control_cmd)
                    g_evt_table->on_control_cmd(&g_brand.gw, ev.cmd_val, ev.cmd_arg);
                break;
            case EVENT_NEED_ACK:
                if (g_evt_table->on_need_ack)
                    g_evt_table->on_need_ack(&g_brand.gw);
                break;
            case EVENT_SCAN_AC:
                if (g_evt_table->on_scan)
                    g_evt_table->on_scan(&g_brand.gw);
                break;
            }
        }
    }
}

static void poll_cb(TimerHandle_t t)
{
    (void)t;
    event_t ev = { .type = EVENT_PERIODIC_SEND };
    xQueueSend(g_brand.queue, &ev, 0);
}

/* ---- 切品牌 ---- */
static void switch_to(uint8_t idx)
{
    g_brand.index  = idx;
    g_brand.active = g_brand.configs[idx];

    g_brand.bus.phy->set_rx_cb(g_brand.bus.phy,
        g_brand.active->evt_table->on_rx_byte, &g_brand.bus);

    g_evt_table = &brand_manager_scan_table;
    g_brand.active->evt_table->on_scan(&g_brand.gw);
}

void brand_manager_set_poll_period(uint16_t period_ms)
{
    g_brand.poll_period_ms = period_ms;
    xTimerChangePeriod(g_brand.poll_timer,
                       pdMS_TO_TICKS(period_ms), 0);
}

/*==================== 公开 API ====================*/

void brand_manager_init(void)
{
    g_brand.phy = phy_uart1_create(&g_brand.bus);
    bus_init(&g_brand.bus, g_brand.phy, 3);
    g_brand.phy->open(g_brand.phy);
    bus_create_tx_task(&g_brand.bus, 200, 2);

    gateway_init(&g_brand.gw, &g_brand.bus);

    g_brand.queue = xQueueCreate(8, sizeof(event_t));
    xTaskCreate(brand_task, "AC", 256, NULL, 3, &g_brand.task);

    g_brand.poll_period_ms = 200;
    g_brand.poll_timer = xTimerCreate("poll",
        pdMS_TO_TICKS(200), pdTRUE, NULL, poll_cb);
    xTimerStart(g_brand.poll_timer, 0);

    g_brand.count  = 0;
    g_brand.index  = 0;
    g_brand.locked = 0;
    g_brand.active = NULL;
    g_evt_table    = NULL;
}

void brand_manager_register(const brand_config_t *config)
{
    if (g_brand.count < MAX_BRANDS)
        g_brand.configs[g_brand.count++] = config;
}

void brand_manager_start_scan(void)
{
    g_brand.locked = 0;
    switch_to(0);
}

void brand_manager_lock(void)  { g_brand.locked = 1; }
int  brand_manager_locked(void) { return g_brand.locked; }
const brand_config_t *brand_manager_current(void) { return g_brand.active; }

/*==================== 扫描事件表 ====================*/

static void scan_periodic(void *ctx)
{
    if (g_brand.locked || g_brand.active == NULL) return;
    g_brand.active->evt_table->on_periodic_send(ctx);
}

static int scan_rx_frame(void *ctx, uint8_t *data, uint16_t length)
{
    if (g_brand.locked || g_brand.active == NULL) return 0;

    if (g_brand.active->evt_table->on_rx_frame(ctx, data, length)) {
        g_brand.locked     = 1;
        g_evt_table = g_brand.active->evt_table;
        return 1;
    }

    switch_to((g_brand.index + 1) % g_brand.count);
    return 0;
}

static void scan_control(void *ctx, uint8_t cmd, uint8_t val)
{ (void)ctx; (void)cmd; (void)val; }
static void scan_ack(void *ctx) { (void)ctx; }

static void scan_start(void *ctx)
{
    (void)ctx;
    g_brand.locked = 0;
    switch_to(0);
}

const event_handler_t brand_manager_scan_table = {
    .on_rx_byte       = NULL,
    .on_periodic_send = scan_periodic,
    .on_rx_frame      = scan_rx_frame,
    .on_rx_isr        = NULL,   /* 扫描期不紧急 */
    .on_control_cmd   = scan_control,
    .on_need_ack      = scan_ack,
    .on_scan          = scan_start,
};
