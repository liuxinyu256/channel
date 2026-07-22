/**
 * phy_uart1.c —— CH579 UART1 RS485 物理层驱动
 */
#include "CH57x_common.h"
#include "phy_uart1.h"
#include "bus.h"

typedef struct {
    phy_driver_t       base;
    void (*rx_cb)(uint8_t byte, void *ctx);
    void              *rx_ctx;
    bus_controller_t  *bus;       /* TX 完成时回调 */
} phy_uart1_t;

static phy_uart1_t g_phy;

static int uart1_open(phy_driver_t *p) {
    (void)p;
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit(); UART1_BaudRateCfg(115200);
    UART1_ByteTrigCfg(UART_1BYTE_TRIG);
    UART1_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    NVIC_SetPriority(UART1_IRQn, 1);  /* must be >= configMAX_SYSCALL(1) for FreeRTOS ISR API */
    NVIC_EnableIRQ(UART1_IRQn);
    return 0;
}
static void uart1_close(phy_driver_t *p) {
    (void)p;
    UART1_INTCfg(DISABLE, RB_IER_RECV_RDY | RB_IER_THR_EMPTY | RB_IER_LINE_STAT);
}
static void uart1_write(phy_driver_t *p, uint8_t byte) {
    p->sending = 1;                             /* 半双工: 屏蔽 RX 回声 */
    R8_UART1_THR = byte;
    UART1_INTCfg(ENABLE, RB_IER_THR_EMPTY);    /* 字节发完后 ISR 回调 */
}
static void uart1_set_rx_cb(phy_driver_t *p,
                             void (*cb)(uint8_t byte, void *ctx), void *ctx) {
    phy_uart1_t *up=(phy_uart1_t *)p; up->rx_cb=cb; up->rx_ctx=ctx;
}

void UART1_IRQHandler(void) {
    uint8_t b;
    switch(UART1_GetITFlag()){
    case UART_II_RECV_RDY:
        b=UART1_RecvByte();
        if(!g_phy.base.sending && g_phy.rx_cb) g_phy.rx_cb(b,g_phy.rx_ctx);
        break;
    case UART_II_RECV_TOUT:
        while(UART1_GetLinSTA()&STA_RECV_DATA){
            b=UART1_RecvByte();
            if(!g_phy.base.sending && g_phy.rx_cb) g_phy.rx_cb(b,g_phy.rx_ctx);
        }
        break;
    case UART_II_THR_EMPTY:
        /* 发完一字节, 通知 bus 取下一字节或标记空闲 */
        UART1_INTCfg(DISABLE, RB_IER_THR_EMPTY);
        if (g_phy.bus) bus_on_tx_done(g_phy.bus);
        break;
    default:break;
    }
}

phy_driver_t *phy_uart1_create(bus_controller_t *bus) {
    g_phy.base.open=uart1_open; g_phy.base.close=uart1_close;
    g_phy.base.write=uart1_write; g_phy.base.set_rx_cb=uart1_set_rx_cb;
    g_phy.base.half_duplex=1; g_phy.base.sending=0;
    g_phy.bus=bus;
    return &g_phy.base;
}
