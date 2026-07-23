#ifndef BRAND_MANAGER_H
#define BRAND_MANAGER_H
#include "packet.h"
#include "gateway_device.h"

/* 物理接口配置 */
typedef struct {
    uint8_t  type;       /* RS485 / HBS / IR / TTL */
    uint8_t  uart_id;    /* UART0 / UART1 */
    uint32_t baudrate;
    uint8_t  de_pin;     /* RS485 方向控制 */
    uint8_t  rx_pin, tx_pin;
} phy_config_t;

/* 品牌配置 = 物理层 + 封包器 + 事件表 + 能力 */
typedef struct brand_config {
    const char             *name;
    const phy_config_t     *phy;
    const event_handler_t  *evt_table;
    const gateway_ability_t     *ability;
    packetizer_t           *packetizer;
} brand_config_t;

void  brand_manager_init(void);
void  brand_manager_register(const brand_config_t *config);
void  brand_manager_start_scan(void);
void  brand_manager_on_frame(uint8_t *data, uint16_t length);
void  brand_manager_lock(void);
int   brand_manager_locked(void);
const brand_config_t *brand_manager_current(void);

#endif
