/* ============================================================
 * frame_timer_hw.c —— 硬件定时器层
 * ============================================================ */
#include "frame_timer_hw.h"
#include "timer1.h"
#define FRAME_TIMER_HW_MAX  4

/* ---- 1. 硬件适配器接口 ---- */
typedef struct {
    void (*init)     (void);
    void (*start)    (void);
    void (*stop)     (void);
    void (*restart)  (void);
    void (*set_cmp)  (uint16_t ticks);
    void (*clear_isr_flag)(void);
} timer_hw_adapter_t;

/* ---- 2. 实例数据 —— 一个结构体：对外接口 + 引擎数据全在这 ---- */
typedef struct {
    frame_timer_t base;               /* 对外暴露的 frame_timer_t 接口  */
    uint8_t       hw_id;              /* 绑定的硬件定时器编号          */

    /* 引擎数据 */
    timer_callback cb;
    void          *ctx;
    uint16_t       counter;
    uint16_t       timeout_threshold;
    uint16_t       tick_period_us;
} frame_timer_hw_inst_t;

static frame_timer_hw_inst_t hw_instances[FRAME_TIMER_HW_MAX];
static uint8_t               hw_occupied_map = 0U;

/* ---- 3. 适配器表 —— 每平台实现一份（见文件末尾）---- */

/* ============================================================
 *  ops 回调 —— 引擎逻辑直接内联，再调适配器写硬件
 * ============================================================ */

static void hw_start(frame_timer_t *t) {
    frame_timer_hw_inst_t *self = (frame_timer_hw_inst_t *)t;
    self->counter = 0;
    hw_adapter[self->hw_id].start();
}

static void hw_stop(frame_timer_t *t) {
    frame_timer_hw_inst_t *self = (frame_timer_hw_inst_t *)t;
    hw_adapter[self->hw_id].stop();
    self->counter = 0;
}

static void hw_restart(frame_timer_t *t) {
    frame_timer_hw_inst_t *self = (frame_timer_hw_inst_t *)t;
    self->counter = 0;
    hw_adapter[self->hw_id].restart();
}

static const frame_timer_ops_t hw_timer_ops = {
    .start   = hw_start,
    .stop    = hw_stop,
    .restart = hw_restart,
};

/* ============================================================
 *  工厂函数 —— 唯一的对外入口
 * ============================================================ */

#define TICK_PERIOD_US_DEFAULT  1000U   /* 默认定时器 tick 周期 1ms */

frame_timer_t* frame_timer_hw_create(timer_callback cb, void *ctx,
                                      uint16_t timeout_us,
                                      uint8_t hw_id) {
    if (hw_id >= FRAME_TIMER_HW_MAX) return NULL;
    if (hw_occupied_map & (1 << hw_id)) return NULL;

    hw_occupied_map |= (1 << hw_id);

    frame_timer_hw_inst_t *inst = &hw_instances[hw_id];
    inst->base.ops       = &hw_timer_ops;
    inst->hw_id          = hw_id;
    inst->cb             = cb;
    inst->ctx            = ctx;
    inst->counter        = 0;
    inst->tick_period_us = TICK_PERIOD_US_DEFAULT;
    inst->timeout_threshold = (timeout_us + TICK_PERIOD_US_DEFAULT - 1)
                              / TICK_PERIOD_US_DEFAULT;

    hw_adapter[hw_id].init();
    hw_adapter[hw_id].set_cmp(inst->timeout_threshold);

    return &inst->base;
}

/* ---- ISR —— 平台中断服务例程调用 ---- */
void frame_timer_hw_isr(uint8_t hw_id) {
    if (!(hw_occupied_map & (1 << hw_id))) return;  /* 未分配的 hw_id，忽略 */

    frame_timer_hw_inst_t *t = &hw_instances[hw_id];
    hw_adapter[hw_id].clear_isr_flag();

    if (t->timeout_threshold == 0) return;

    if (++t->counter >= t->timeout_threshold) {
        t->counter = 0;
        if (t->cb) t->cb(t->ctx);
    }
}

/* ============================================================
 *  平台适配器表 —— 换平台只改这张表
 * ============================================================ */
static const timer_hw_adapter_t hw_adapter[FRAME_TIMER_HW_MAX] = {
    [0] = { .start = timer1_start, .stop = timer1_stop,
            .init = timer1_init, .restart = timer1_restart,
            .clear_isr_flag = timer1_clear_isr_flag },
};

/* ---- destroy ---- */
void frame_timer_hw_destroy(frame_timer_t *t) {
    if (t == NULL) return;
    frame_timer_hw_inst_t *inst = (frame_timer_hw_inst_t *)t;
    uint8_t hw_id = inst->hw_id;

    hw_adapter[hw_id].stop();
    hw_occupied_map &= ~(1 << hw_id);
}