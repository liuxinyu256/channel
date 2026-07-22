#ifndef HVAC_PHY_H
#define HVAC_PHY_H

#include <stdint.h>

typedef struct phy_driver phy_driver_t;

struct phy_driver {
    int  (*open) (phy_driver_t *self);
    void (*close)(phy_driver_t *self);
    void (*write)(phy_driver_t *self, uint8_t byte);
    void (*set_rx_cb)(phy_driver_t *self,
                       void (*cb)(uint8_t byte, void *ctx), void *ctx);

    /* 物理层自收发过滤: 半双工设 1, 全双工设 0 */
    uint8_t sending;
    uint8_t half_duplex;
};

#endif
