#include "brand_manager.h"
#include "bus.h"
#include "packet.h"
#include "packetizer_timeout.h"
#include "frame_timer_hw.h"
#include "phy_uart1.h"

#define MAX_BRANDS 8

extern const event_handler_t brand_manager_scan_table;

static struct {
    bus_controller_t        bus;
    phy_driver_t           *phy;
    ac_device_t             ac_device;
    frame_timer_t          *timer;
    packetizer_t           *packetizer;
    const event_handler_t  *tables[MAX_BRANDS];
    uint8_t                 count;
    uint8_t                 index;
    uint8_t                 locked;
} g_brand;

/* ---- RX 字节回调 → 喂封包器 + 通知总线 ---- */
static void brand_on_rx_byte(uint8_t byte, void *context)
{
    bus_controller_t *bus = (bus_controller_t *)context;
    packetizer_put_byte(g_brand.packetizer, byte);
    bus_on_rx_byte(bus, byte);
}

/* ---- 帧完成回调(ISR) → 转发到 AC 任务队列 + 释放总线 ---- */
static void brand_on_frame(uint8_t *data, uint16_t length)
{
    BaseType_t woken = pdFALSE;
    event_t event = { .type = EVENT_RX_FRAME, .data = data, .len = length };
    xQueueSendFromISR(g_brand.ac_device.evt_queue, &event, &woken);
    bus_on_rx_done(&g_brand.bus);   /* 封包完成, 立即释放总线 */
    portYIELD_FROM_ISR(woken);
}

/*==================== 公开 API ====================*/

void brand_manager_init(void)
{
    /* —— PHY + 总线 —— */
    g_brand.phy = phy_uart1_create(&g_brand.bus);
    bus_init(&g_brand.bus, g_brand.phy, 3);
    g_brand.phy->open(g_brand.phy);
    bus_create_tx_task(&g_brand.bus, 200, 2);

    /* —— 帧定时器 + 封包器 —— */
    g_brand.timer = frame_timer_hw_create(0);
    g_brand.packetizer = packetizer_timeout_create(g_brand.timer, 5, brand_on_frame);

    /* —— AC 设备(初始用扫描事件表) —— */
    ac_init(&g_brand.ac_device, &g_brand.bus, NULL, &brand_manager_scan_table, 200);
    ac_create_task(&g_brand.ac_device, 256, 3);

    /* —— 品牌管理器内部状态 —— */
    g_brand.count  = 0;
    g_brand.index  = 0;
    g_brand.locked = 0;

    /* —— RX 字节回调挂载 —— */
    g_brand.bus.phy->set_rx_cb(g_brand.bus.phy, brand_on_rx_byte, &g_brand.bus);
}

void brand_manager_register(const event_handler_t *table)
{
    if (g_brand.count < MAX_BRANDS)
        g_brand.tables[g_brand.count++] = table;
}

void brand_manager_start_scan(void)
{
    event_t event = { .type = EVENT_SCAN_AC };
    ac_post(&g_brand.ac_device, &event);
}

void brand_manager_lock(void)
{
    g_brand.locked = 1;
}

int brand_manager_locked(void)
{
    return g_brand.locked;
}

const event_handler_t *brand_manager_table(void)
{
    return g_brand.locked ? g_brand.tables[g_brand.index] : NULL;
}

/*==================== 扫描事件表(内部) ====================*/

static void scan_periodic(void *context)
{
    if (g_brand.locked || g_brand.count == 0) return;
    const event_handler_t *table = g_brand.tables[g_brand.index];
    if (table->on_periodic_send) table->on_periodic_send(context);
}

static int scan_rx_frame(void *context, uint8_t *data, uint16_t length)
{
    if (g_brand.locked || g_brand.count == 0) return 0;

    /* 交给当前品牌判断, 匹配(返回1)才锁定并切换事件表 */
    if (!g_brand.tables[g_brand.index]->on_rx_frame(context, data, length))
        return 0;

    g_brand.locked = 1;
    ac_device_t *ac = (ac_device_t *)context;
    ac->evt_table = g_brand.tables[g_brand.index];
    return 1;
}

static void scan_control(void *context, uint8_t cmd, uint8_t val)
{
    (void)context; (void)cmd; (void)val;
}

static void scan_ack(void *context)
{
    (void)context;
}

static void scan_start(void *context)
{
    g_brand.locked = 0;
    g_brand.index  = 0;
    if (g_brand.tables[0]->on_scan)
        g_brand.tables[0]->on_scan(context);
}

const event_handler_t brand_manager_scan_table = {
    .on_periodic_send = scan_periodic,
    .on_rx_frame      = scan_rx_frame,
    .on_control_cmd   = scan_control,
    .on_need_ack      = scan_ack,
    .on_scan          = scan_start,
};
