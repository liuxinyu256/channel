#ifndef FRAME_TIMER_H
#define FRAME_TIMER_H
#include <stdint.h>

/* ============================================================
 * frame_timer.h —— 定时器基类
 *
 * 设计原则：
 *   基类持有所有定时器共有的数据（counter / cb / ctx），
 *   子类只放实现相关字段（硬件通道号等）。
 *
 * 继承方式（C 风格）：
 *   frame_timer_t 作为子类结构体的第一个成员，外部拿到基类指针后
 *   通过 ops 表实现多态——调用者不关心底层是硬件定时器还是软件定时器。
 *
 * 子类：
 *   frame_timer_hw_inst_t  —— 硬件定时器（frame_timer_hw.c）
 *   frame_timer_sw_inst_t  —— 软件定时器（后续实现）
 * ============================================================ */

typedef struct frame_timer frame_timer_t;

/* ---- 超时回调 ---- */
typedef void (*timer_callback)(void *ctx);

/* ============================================================
 *  frame_timer_ops_t —— 多态操作接口
 *
 *  每个子类提供自己的 ops 表，基类指针 t->ops->start(t) 即可
 *  触发对应实现。
 * ============================================================ */
typedef struct {
    void (*start)       (frame_timer_t *t);
    void (*restart)     (frame_timer_t *t);
    void (*stop)        (frame_timer_t *t);
    void (*set_callback)(frame_timer_t *t, timer_callback cb, void *ctx);
} frame_timer_ops_t;

/* ============================================================
 *  frame_timer —— 定时器基类
 *
 *  字段说明：
 *    ops     - 多态操作表，子类构造时绑定
 *    cb/ctx  - tick 回调，每次 tick 触发
 *    counter - tick 计数值，由子类驱动递增（ISR / systick）

 * ============================================================ */
struct frame_timer {
    const frame_timer_ops_t *ops;              /* 多态操作接口               */
    timer_callback           cb;               /* tick 回调                  */
    void                    *ctx;              /* 回调上下文                 */
    uint32_t                 counter;          /* 当前 tick 计数值           */
};

#endif
