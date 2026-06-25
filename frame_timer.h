#ifndef FRAME_TIMER_H
#define FRAME_TIMER_H
#include <stdlib.h>
#include <stdint.h>

//定时器基类  frame_timer.h

typedef struct frame_timer frame_timer_t;

typedef void (*timer_callback)(void *ctx);

typedef struct {
    void (*start)(frame_timer_t *t);    //启动定时器
    void (*restart)(frame_timer_t *t);  //清空当前计数值，从零开始计数
    void (*stop)(frame_timer_t *t);     //停止定时器，清空计数值
} frame_timer_ops_t;

struct frame_timer {
    const frame_timer_ops_t *ops;
};

/* 硬件定时器工厂：共性参数 (cb, ctx, timeout_us) + 平台参数 (hw_id)
 * tick_period_us 等平台细节下沉到 frame_timer_hw_init 阶段处理 */
frame_timer_t* frame_timer_hw_create(timer_callback cb, void *ctx,
                                      uint16_t timeout_us,
                                      uint8_t hw_id);

#endif
