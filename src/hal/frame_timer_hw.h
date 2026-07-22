/********************************** (C) COPYRIGHT *******************************
 * File Name          : frame_timer_hw.h
 * Description        : 硬件定时器工厂 —— CH579 TMR0-3
 *******************************************************************************/

#ifndef FRAME_TIMER_HW_H
#define FRAME_TIMER_HW_H

#include "frame_timer.h"

/* hw_id: 0=TMR0, 1=TMR1, 2=TMR2, 3=TMR3 */
frame_timer_t* frame_timer_hw_create(uint8_t hw_id);
void           frame_timer_hw_destroy(frame_timer_t *t);

/* ISR 入口 —— 由平台定时器 ISR 调用 */
void frame_timer_hw_isr(uint8_t hw_id);

#endif
