#include "brand_manager.h"
#include "bus.h"
#include "packet.h"
#include "phy_uart1.h"

#define MAX_BRANDS 8

extern const event_handler_t brand_manager_scan_table;

static struct {
    bus_controller_t        bus;
    phy_driver_t           *phy;
    ac_device_t             ac_device;
    const event_handler_t  *tables[MAX_BRANDS];
    packetizer_t           *packetizers[MAX_BRANDS];
    uint8_t                 count;
    uint8_t                 index;
    uint8_t                 locked;
} g_brand;

/* ---- 品牌管理器公开的帧入口(ISR), 品牌在封包器回调中调用 ---- */
void brand_manager_on_frame(uint8_t *data, uint16_t length)
{
    BaseType_t woken = pdFALSE;
    event_t event = { .type = EVENT_RX_FRAME, .data = data, .len = length };
    xQueueSendFromISR(g_brand.ac_device.evt_queue, &event, &woken);
    bus_on_rx_done(&g_brand.bus);
    portYIELD_FROM_ISR(woken);
}

/* ---- RX 字节回调 → 当前品牌封包器 + 总线 ---- */
static void brand_on_rx_byte(uint8_t byte, void *context)
{
    bus_controller_t *bus = (bus_controller_t *)context;
    packetizer_put_byte(g_brand.packetizers[g_brand.index], byte);
    bus_on_rx_byte(bus, byte);
}

/*==================== 公开 API ====================*/

void brand_manager_init(void)
{
    g_brand.phy = phy_uart1_create(&g_brand.bus);
    bus_init(&g_brand.bus, g_brand.phy, 3);
    g_brand.phy->open(g_brand.phy);
    bus_create_tx_task(&g_brand.bus, 200, 2);

    ac_init(&g_brand.ac_device, &g_brand.bus, NULL, &brand_manager_scan_table, 200);
    ac_create_task(&g_brand.ac_device, 256, 3);

    g_brand.count  = 0;
    g_brand.index  = 0;
    g_brand.locked = 0;

    g_brand.bus.phy->set_rx_cb(g_brand.bus.phy, brand_on_rx_byte, &g_brand.bus);
}

void brand_manager_register(const event_handler_t *table, packetizer_t *packetizer)
{
    if (g_brand.count < MAX_BRANDS) {
        g_brand.tables[g_brand.count]      = table;
        g_brand.packetizers[g_brand.count] = packetizer;
        g_brand.count++;
    }
}

void brand_manager_start_scan(void)
{
    event_t event = { .type = EVENT_SCAN_AC };
    ac_post(&g_brand.ac_device, &event);
}

void brand_manager_lock(void)           { g_brand.locked = 1; }
int  brand_manager_locked(void)         { return g_brand.locked; }
const event_handler_t *brand_manager_table(void) {
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

    if (g_brand.tables[g_brand.index]->on_rx_frame(context, data, length)) {
        g_brand.locked = 1;
        ac_device_t *ac = (ac_device_t *)context;
        ac->evt_table = g_brand.tables[g_brand.index];
        return 1;
    }

    g_brand.index = (g_brand.index + 1) % g_brand.count;
    g_brand.tables[g_brand.index]->on_scan(context);
    return 0;
}

static void scan_control(void *context, uint8_t cmd, uint8_t val)
{ (void)context; (void)cmd; (void)val; }

static void scan_ack(void *context)
{ (void)context; }

static void scan_start(void *context)
{
    g_brand.locked = 0;
    g_brand.index  = 0;
    g_brand.tables[0]->on_scan(context);
}

const event_handler_t brand_manager_scan_table = {
    .on_periodic_send = scan_periodic,
    .on_rx_frame      = scan_rx_frame,
    .on_control_cmd   = scan_control,
    .on_need_ack      = scan_ack,
    .on_scan          = scan_start,
};
