/**
 * mock_phy.c —— 软件模拟物理层 (PC 测试用)
 * write 不输出, rx_cb 由测试手动触发
 */
#include "phy.h"
#include <string.h>

typedef struct {
    phy_driver_t base;
    void (*rx_cb)(uint8_t byte, void *ctx);
    void        *rx_ctx;
} mock_phy_t;

static int  mock_open(phy_driver_t *p) { (void)p; return 0; }
static void mock_close(phy_driver_t *p) { (void)p; }
static void mock_write(phy_driver_t *p, uint8_t b) { (void)p; (void)b; }
static void mock_set_rx_cb(phy_driver_t *p,
                            void (*cb)(uint8_t byte, void *ctx), void *ctx) {
    mock_phy_t *mp=(mock_phy_t *)p; mp->rx_cb=cb; mp->rx_ctx=ctx;
}

phy_driver_t *mock_phy_create(void) {
    static mock_phy_t inst; memset(&inst,0,sizeof(inst));
    inst.base.open=mock_open; inst.base.close=mock_close;
    inst.base.write=mock_write; inst.base.set_rx_cb=mock_set_rx_cb;
    inst.base.half_duplex=0;
    return &inst.base;
}
