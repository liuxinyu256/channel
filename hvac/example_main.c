/**
 * example_main.c —— HVAC 多品牌网关集成示例
 *
 * BLE 回调只投事件: ble_on_set_temp(26) → ac_post(CONTROL_CMD, 0, 26)
 */

#include "FreeRTOS.h"
#include "task.h"
#include "phy.h"
#include "bus.h"
#include "ac.h"
#include "frame_timer.h"

extern ac_device_t *ac_gree_create(bus_controller_t *bus,
                                    frame_timer_t *timer, uint8_t temp_base);
extern phy_driver_t *phy_rs485_create(uint8_t uart_id);
extern frame_timer_t *frame_timer_hw_create(uint8_t id);

static ac_device_t *g_ac;

void ble_on_set_temp(uint8_t temp) {
    event_t ev={.type=EVENT_CONTROL_CMD,.cmd_val=0,.cmd_arg=temp};
    ac_post(g_ac,&ev);
}
void ble_on_set_mode(uint8_t mode) {
    event_t ev={.type=EVENT_CONTROL_CMD,.cmd_val=1,.cmd_arg=mode};
    ac_post(g_ac,&ev);
}

int main(void){
    phy_driver_t *phy=phy_rs485_create(0);phy->open(phy);

    bus_controller_t bus;bus_init(&bus,phy,3);
    bus_create_tx_task(&bus,200,2);

    frame_timer_t *timer=frame_timer_hw_create(0);
    ac_device_t *ac=ac_gree_create(&bus,timer,0);

    ac_create_task(ac,256,3);g_ac=ac;

    event_t ev={.type=EVENT_SCAN_AC};ac_post(ac,&ev);

    vTaskStartScheduler();for(;;);
}
