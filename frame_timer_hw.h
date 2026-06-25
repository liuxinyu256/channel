#ifndef FRAME_TIMER_HW_H
#define FRAME_TIMER_HW_H
#include <stdlib.h>
#include <stdint.h>
#include "frame_timer.h"

/* ============================================================
 * frame_timer_hw.h —— 硬件定时器子类头文件
 *
 * 对外只暴露 ISR 入口，其余实现细节（hw_adapter、instances 等）
 * 全部藏在 frame_timer_hw.c 内部。
 *
 * 使用方式：
 *   1. 平台中断服务函数中调用 frame_timer_hw_isr(hw_id)
 *   2. 创建/销毁通过基类声明的 frame_timer_hw_create/destroy
 * ============================================================ */

/* ---- ISR 入口 ----
 *
 * 由平台定时器中断服务例程调用，每个硬件定时器通道独立触发。
 * 内部完成：清中断标志 → 递增 base.counter → 判断超时 → 触发 base.cb。
 *
 * hw_id : 硬件定时器通道编号，必须与 create 时传入的一致。
 *         未分配或已销毁的 hw_id 会被安全忽略。
 */
void frame_timer_hw_isr(uint8_t hw_id);

#endif
