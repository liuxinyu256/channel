/**
 * @file frame_timer_sw.h
 * @brief 软件定时器 —— PC 端模拟硬件定时器行为
 *
 * 通过主循环轮询 frame_timer_sw_poll() 驱动 counter 递增，
 * 无需硬件 ISR，无需后台线程。适用于：
 *   - 交互式模拟器（sim_app）
 *   - 批量压力测试（test_stress）
 *   - CI 自动化测试
 *
 * 与硬件定时器的差异：
 *   - 时间源：QueryPerformanceCounter (Win) / clock_gettime (Linux)
 *   - 驱动方式：主循环 poll，而非硬件 ISR
 *   - 接口完全兼容：同样返回 frame_timer_t*，同样实现 frame_timer_ops_t
 */

#ifndef FRAME_TIMER_SW_H
#define FRAME_TIMER_SW_H

#include <stdint.h>
#include "frame_timer.h"

/* ============================================================
 *  创建软件定时器
 *
 *  @param tick_period_us  每次 tick 的周期（微秒），默认 1000（=1ms）
 *  @return 定时器基类指针，无可用实例时返回 NULL
 *
 *  示例：
 *    frame_timer_t *t = frame_timer_sw_create(1000);  // 1ms/tick
 *    frame_timer_t *t = frame_timer_sw_create(100);   // 0.1ms/tick（高精度）
 * ============================================================ */
frame_timer_t* frame_timer_sw_create(uint32_t tick_period_us);

/* ---- 销毁 ---- */
void frame_timer_sw_destroy(frame_timer_t *t);

/* ============================================================
 *  poll —— 主循环中周期性调用，驱动定时器
 *
 *  检查自上次 tick 以来是否经过了 tick_period_us，
 *  若是则递增 counter 并触发回调。
 *
 *  典型用法（主循环每轮调用）：
 *    while (running) {
 *        frame_timer_sw_poll(timer);  // 驱动定时器
 *        check_user_input();
 *        sleep_a_bit();
 *    }
 *
 *  也可用 frame_timer_sw_poll_all() 驱动所有活跃实例。
 * ============================================================ */
void frame_timer_sw_poll(frame_timer_t *t);
void frame_timer_sw_poll_all(void);

/* ============================================================
 *  辅助：获取当前时间（微秒），供模拟器统一时间基准
 * ============================================================ */
uint64_t frame_timer_sw_now_us(void);

/* ============================================================
 *  辅助：睡眠指定微秒（跨平台）
 * ============================================================ */
void frame_timer_sw_sleep_us(uint64_t us);

#endif
