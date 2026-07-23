/**
 * gree.c —— 格力空调协议驱动
 */
#include "gateway.h"
#include "frame_timer_hw.h"
#include "packetizer_timeout.h"

static uint8_t g_scanning;

static frame_timer_t *g_timer;
static packetizer_t  *g_packetizer;

static const phy_config_t phy = {
    .type = 0, .uart_id = 1, .baudrate = 115200,
    .de_pin = 0, .rx_pin = 8, .tx_pin = 9,
};

static const ac_ability_t ability = {
    .temp_min = 16, .temp_max = 30, .fan_levels = 3,
    .mode_mask = 0x1F, .has_swing = 1, .has_sleep = 1, .has_eco = 0,
};

static const uint8_t mode_tbl[] = { 0x02, 0x04, 0x01, 0x08, 0x00 };
static const uint8_t fan_tbl[]  = { 0x01, 0x02, 0x03, 0x00 };

static uint8_t build(uint8_t cmd, uint8_t val, uint8_t *f) {
    uint8_t s = 0;
    f[0] = 0xAA; f[1] = 0x01;
    switch (cmd) {
    case 0: f[2] = 0x10; f[3] = val;              break;
    case 1: f[2] = 0x11; f[3] = mode_tbl[val];    break;
    case 2: f[2] = 0x12; f[3] = fan_tbl[val];     break;
    case 3: f[2] = 0x13; f[3] = val ? 0x0F : 0x00; break;
    default: return 0;
    }
    int i; for (i = 0; i < 4; i++) s += f[i];
    f[4] = ~s; f[5] = 0x55;
    return 6;
}

/* ---- 品牌自己的字节入口(品牌管理器不碰字节) ---- */
static void on_rx_byte(uint8_t byte, void *ctx)
{
    (void)ctx;
    packetizer_put_byte(g_packetizer, byte);
}

/* ---- 事件 ---- */
static void ev_periodic(void *ctx) {
    ac_device_t *ac = (ac_device_t *)ctx;
    if (g_scanning) {
        uint8_t f[] = { 0xAA, 0x01, 0xFF, 0x00, 0x00, 0x55 };
        bus_send(ac->bus, f, 6);
    } else {
        uint8_t f[] = { 0xAA, 0x01, 0x20, 0x00, 0xDE, 0x55 };
        bus_send(ac->bus, f, 6);
    }
}

static int ev_rx_frame(void *ctx, uint8_t *d, uint16_t n) {
    ac_device_t *ac = (ac_device_t *)ctx;
    if (n < 6 || d[0] != 0xAA || d[5] != 0x55) return 0;

    if (g_scanning) {
        g_scanning = 0;
        ac_set_poll_period(ac, 5000);
        return 1;
    }
    if (d[2] == 0x20) {
        ac->power = d[3] >> 7; ac->mode = d[3] & 0x0F; ac->set_temp = d[4] >> 1;
    }
    return 1;
}

static void ev_control(void *ctx, uint8_t cmd, uint8_t val) {
    ac_device_t *ac = (ac_device_t *)ctx;
    uint8_t f[8];
    uint8_t n = build(cmd, val, f);
    if (n) bus_send(ac->bus, f, n);
}

static void ev_ack(void *ctx)  { (void)ctx; }
static void ev_scan(void *ctx) { (void)ctx; g_scanning = 1; }

static const event_handler_t gree_table = {
    .on_rx_byte       = on_rx_byte,
    .on_periodic_send = ev_periodic,
    .on_rx_frame      = ev_rx_frame,
    .on_control_cmd   = ev_control,
    .on_need_ack      = ev_ack,
    .on_scan          = ev_scan,
};

/* ---- 品牌配置 ---- */
const brand_config_t gree_config = {
    .name       = "gree",
    .phy        = &phy,
    .evt_table  = &gree_table,
    .ability    = &ability,
    .packetizer = NULL,   /* gree_init() 填充 */
};

void gree_init(void)
{
    g_timer      = frame_timer_hw_create(0);
    g_packetizer = packetizer_timeout_create(g_timer, 5,
                                             brand_manager_on_frame);
    ((brand_config_t *)&gree_config)->packetizer = g_packetizer;
}
