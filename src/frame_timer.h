/********************************** (C) COPYRIGHT *******************************
 * File Name          : frame_timer.h
 * Description        : 定时器基类 —— C 风格继承 + 多态 ops 虚表
 *
 * 设计原则：
 *   基类持有所有定时器共有的数据（counter / cb / ctx），
 *   子类只放实现相关字段（硬件通道号等）。
 *
 * 继承方式（C 风格）：
 *   frame_timer_t 作为子类结构体的第一个成员，外部拿到基类指针后
 *   通过 ops 表实现多态。
 *
 * 子类：
 *   frame_timer_hw_inst_t  —— 硬件定时器（frame_timer_hw.c）
 *******************************************************************************/

#ifndef FRAME_TIMER_H
#define FRAME_TIMER_H

#include "CH57x_common.h"

typedef struct frame_timer frame_timer_t;

typedef void (*timer_callback)(void *ctx);

typedef struct {
    void (*start)       (frame_timer_t *t);
    void (*restart)     (frame_timer_t *t);
    void (*stop)        (frame_timer_t *t);
    void (*set_callback)(frame_timer_t *t, timer_callback cb, void *ctx);
} frame_timer_ops_t;

struct frame_timer {
    const frame_timer_ops_t *ops;
    timer_callback           cb;
    void                    *ctx;
    uint32_t                 counter;
};

#endif
