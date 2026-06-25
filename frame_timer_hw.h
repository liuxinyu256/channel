#ifndef FRAME_TIMER1_H
#define FRAME_TIMER1_H
#include <stdlib.h>
#include <stdint.h>
#include "frame_timer.h"

/* 平台 ISR 中调用，每个硬件定时器中断独立调用 */
void frame_timer_hw_isr(uint8_t hw_id);






#endif
