#ifndef FRAME_TIMER_HW_H
#define FRAME_TIMER_HW_H
#include <stdlib.h>
#include <stdint.h>
#include "frame_timer.h"

/* ============================================================
 * frame_timer_hw.h —— 硬件定时器
 *
 * 对外暴露：工厂函数 + ISR 入口
 * 实现细节（hw_adapter、instances）藏在 frame_timer_hw.c 内部。
 * ============================================================ */

/* 创建硬件定时器
 * cb, ctx     - 超时回调及上下文
 * timeout_us  - 超时时间（微秒）
 * hw_id       - 硬件通道号（0~3）
 * 返回基类指针，无可用实例返回 NULL
 */
frame_timer_t* frame_timer_hw_create(timer_callback cb, void *ctx,
                                     uint16_t timeout_us,
                                     uint8_t hw_id);
void           frame_timer_hw_destroy(frame_timer_t *t);

/* ISR 入口 —— 由平台定时器中断服务例程调用
 * hw_id: 硬件通道号，必须与 create 时一致
 */
void frame_timer_hw_isr(uint8_t hw_id);

#endif
