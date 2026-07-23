#include "brand_manager.h"
#include "bus.h"
#include "phy_uart1.h"

#define MAX_BRANDS 8

extern const event_handler_t brand_manager_scan_table;

static struct {
    bus_controller_t    bus;
    phy_driver_t       *phy;
    gateway_device_t         ac_device;
    const brand_config_t *configs[MAX_BRANDS];
    const brand_config_t *active;
    uint8_t             count;
    uint8_t             index;
    uint8_t             locked;
} g_brand;

/* ---- 品牌管理器公开的帧入口(ISR) ---- */
void brand_manager_on_frame(uint8_t *data, uint16_t length)
{
    BaseType_t woken = pdFALSE;
    event_t event = { .type = EVENT_RX_FRAME, .data = data, .len = length };
    xQueueSendFromISR(g_brand.ac_device.evt_queue, &event, &woken);
    bus_on_rx_done(&g_brand.bus);
    portYIELD_FROM_ISR(woken);
}

/* ---- 切品牌: 指针一换, rx_cb 一挂 ---- */
static void switch_to(uint8_t idx)
{
    g_brand.index  = idx;
    g_brand.active = g_brand.configs[idx];

    g_brand.bus.phy->set_rx_cb(g_brand.bus.phy,
        g_brand.active->evt_table->on_rx_byte, &g_brand.bus);

    gateway_device_t *ac = &g_brand.ac_device;
    ac->evt_table = &brand_manager_scan_table;

    g_brand.active->evt_table->on_scan(ac);
}

/*==================== 公开 API ====================*/

void brand_manager_init(void)
{
    g_brand.phy = phy_uart1_create(&g_brand.bus);
    bus_init(&g_brand.bus, g_brand.phy, 3);
    g_brand.phy->open(g_brand.phy);
    bus_create_tx_task(&g_brand.bus, 200, 2);

    ac_init(&g_brand.ac_device, &g_brand.bus,
            &brand_manager_scan_table, 200);
    ac_create_task(&g_brand.ac_device, 256, 3);

    g_brand.count  = 0;
    g_brand.index  = 0;
    g_brand.locked = 0;
    g_brand.active = NULL;
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

void brand_manager_lock(void)
    { g_brand.locked = 1; }

int brand_manager_locked(void)
    { return g_brand.locked; }

const brand_config_t *brand_manager_current(void)
    { return g_brand.active; }

/*==================== 扫描事件表(内部) ====================*/

static void scan_periodic(void *context)
{
    if (g_brand.locked || g_brand.active == NULL) return;
    g_brand.active->evt_table->on_periodic_send(context);
}

static int scan_rx_frame(void *context, uint8_t *data, uint16_t length)
{
    if (g_brand.locked || g_brand.active == NULL) return 0;

    if (g_brand.active->evt_table->on_rx_frame(context, data, length)) {
        g_brand.locked = 1;
        gateway_device_t *ac = (gateway_device_t *)context;
        ac->evt_table = g_brand.active->evt_table;
        return 1;
    }

    /* 不匹配 → 下一个 */
    switch_to((g_brand.index + 1) % g_brand.count);
    return 0;
}

static void scan_control(void *context, uint8_t cmd, uint8_t val)
{ (void)context; (void)cmd; (void)val; }

static void scan_ack(void *context)
{ (void)context; }

static void scan_start(void *context)
{
    (void)context;
    g_brand.locked = 0;
    switch_to(0);
}

const event_handler_t brand_manager_scan_table = {
    .on_rx_byte       = NULL,   /* 扫描期由 switch_to 直接挂品牌的 */
    .on_periodic_send = scan_periodic,
    .on_rx_frame      = scan_rx_frame,
    .on_control_cmd   = scan_control,
    .on_need_ack      = scan_ack,
    .on_scan          = scan_start,
};
