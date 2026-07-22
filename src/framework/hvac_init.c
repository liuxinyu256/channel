#include "hvac_init.h"
#include "brand_manager.h"
#include "frame_timer_hw.h"
#include "packetizer_timeout.h"

extern const event_handler_t gree_table;

void hvac_start(void)
{
    brand_manager_init();

    frame_timer_t *timer = frame_timer_hw_create(0);
    packetizer_t  *pkt   = packetizer_timeout_create(timer, 5,
                                                     brand_manager_on_frame);

    brand_manager_register(&gree_table, pkt);
    brand_manager_start_scan();
}
