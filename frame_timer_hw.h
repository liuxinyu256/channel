#ifndef FRAME_TIMER1_H
#define FRAME_TIMER1_H
#include <stdlib.h>
#include <stdint.h>
#include "frame_timer.h"

void frame_timer_hw_init(uint8_t hw_id, timer_callback cb, void *ctx,
                         uint16_t tick_period_us);
void frame_timer_hw_start  (uint8_t hw_id);
void frame_timer_hw_stop   (uint8_t hw_id);
void frame_timer_hw_restart(uint8_t hw_id);
void frame_timer_hw_set_timeout(uint8_t hw_id, uint16_t us);






#endif
