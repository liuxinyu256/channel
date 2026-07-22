/**
 * gree.c —— 格力空调协议驱动
 * 使用: brand_manager_register(&gree_table);
 */

#include "gateway.h"

static uint8_t g_scanning;

static const uint8_t mode_tbl[] = { 0x02, 0x04, 0x01, 0x08, 0x00 };
static const uint8_t fan_tbl[]  = { 0x01, 0x02, 0x03, 0x00 };

static uint8_t build(uint8_t cmd, uint8_t val, uint8_t *f) {
    uint8_t s = 0;
    f[0] = 0xAA; f[1] = 0x01;
    switch (cmd) {
    case 0: f[2] = 0x10; f[3] = val;              break; /* 温度 */
    case 1: f[2] = 0x11; f[3] = mode_tbl[val];    break; /* 模式 */
    case 2: f[2] = 0x12; f[3] = fan_tbl[val];     break; /* 风速 */
    case 3: f[2] = 0x13; f[3] = val ? 0x0F : 0x00; break; /* 摆风 */
    default: return 0;
    }
    int i; for (i = 0; i < 4; i++) s += f[i];
    f[4] = ~s; f[5] = 0x55;
    return 6;
}

/* ======== 事件处理 ======== */

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
        ac->power    = d[3] >> 7;
        ac->mode     = d[3] & 0x0F;
        ac->set_temp = d[4] >> 1;
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

/* ======== 品牌事件表 ======== */

const event_handler_t gree_table = {
    .on_periodic_send = ev_periodic,
    .on_rx_frame      = ev_rx_frame,
    .on_control_cmd   = ev_control,
    .on_need_ack      = ev_ack,
    .on_scan          = ev_scan,
};
