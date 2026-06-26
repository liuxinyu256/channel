#ifndef FRAME_TIMER_H
#define FRAME_TIMER_H
#include <stdlib.h>
#include <stdint.h>

/* ============================================================
 * frame_timer.h —— 定时器基类
 *
 * 设计原则：
 *   基类持有所有定时器共有的数据（counter / threshold / cb / ctx），
 *   子类只放实现相关字段（硬件通道号、tick 周期等）。
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
 *  触发对应实现。三个方法的 counter 清零语义由基类保证——
 *  counter 字段在基类中，子类 ops 直接操作 t->counter。
 * ============================================================ */
typedef struct {
    void (*start)  (frame_timer_t *t);   /* 启动定时器，counter 从 0 开始计数     */
    void (*restart)(frame_timer_t *t);   /* 清空 counter，从零重新计数             */
    void (*stop)   (frame_timer_t *t);   /* 停止定时器，counter 清零               */
} frame_timer_ops_t;

/* ============================================================
 *  frame_timer —— 定时器基类
 *
 *  字段说明：
 *    ops              - 多态操作表，子类构造时绑定
 *    cb / ctx         - 超时回调及其上下文，ISR 或软 tick 中调用
 *    counter          - 当前 tick 计数值，递增逻辑由子类驱动
 *                       （硬件 ISR 递增 或 软件 systick 递增）
 *    timeout_threshold - 超时阈值（单位：tick 数），counter 达到此值时触发 cb

 * ============================================================ */
struct frame_timer {
    const frame_timer_ops_t *ops;              /* 多态操作接口               */
    timer_callback           cb;               /* 超时回调                   */
    void                    *ctx;              /* 回调上下文                 */
    uint32_t                 counter;          /* 当前 tick 计数值           */
    uint32_t                 timeout_threshold;/* 超时阈值（tick 数）        */
};

/* ============================================================
 *  工厂函数（硬件定时器）
 *
 *  cb, ctx, timeout_us 是所有定时器共性参数；
 *  hw_id 是硬件定时器特有参数，由子类工厂接收。
 *  软件定时器工厂签名可不同（例如传入 systick 引用）。
 * ============================================================ */
frame_timer_t* frame_timer_hw_create(timer_callback cb, void *ctx,
                                      uint16_t timeout_us,
                                      uint8_t hw_id);
void           frame_timer_hw_destroy(frame_timer_t *t);

#endif
