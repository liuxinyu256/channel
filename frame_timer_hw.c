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

/* ---- 2. 实例数据 ---- */
typedef struct {
    timer_callback cb;
    void          *ctx;
    uint16_t       counter;
    uint16_t       timeout_threshold;
    uint16_t       tick_period_us;
} frame_timer_hw_t;

static frame_timer_hw_t hw_instance[FRAME_TIMER_HW_MAX];

/* ---- 3. 适配器表 —— 每平台实现一份（见文件末尾平台适配区）---- */

/* ============================================================
 *  公共接口
 * ============================================================ */

void frame_timer_hw_init(uint8_t hw_id, timer_callback cb, void *ctx,
                         uint16_t tick_period_us) {
    frame_timer_hw_t *t = &hw_instance[hw_id];
    t->cb                = cb;
    t->ctx               = ctx;
    t->counter           = 0;
    t->timeout_threshold = 0;
    t->tick_period_us    = tick_period_us;
    hw_adapter[hw_id].init();
}

void frame_timer_hw_start(uint8_t hw_id) {
    hw_instance[hw_id].counter = 0;
    hw_adapter[hw_id].start();
}

void frame_timer_hw_stop(uint8_t hw_id) {
    hw_adapter[hw_id].stop();
    hw_instance[hw_id].counter = 0;
}

void frame_timer_hw_restart(uint8_t hw_id) {
    hw_instance[hw_id].counter = 0;
    hw_adapter[hw_id].restart();
}

void frame_timer_hw_set_timeout(uint8_t hw_id, uint16_t us) {
    frame_timer_hw_t *t = &hw_instance[hw_id];
    t->timeout_threshold = (us + t->tick_period_us - 1) / t->tick_period_us;
    hw_adapter[hw_id].set_cmp(t->timeout_threshold);
}

/* ---- ISR —— 每平台每 hw_id 独立调用 ---- */
void frame_timer_hw_isr(uint8_t hw_id) {
    frame_timer_hw_t *t = &hw_instance[hw_id];
    hw_adapter[hw_id].clear_isr_flag();

    if (t->timeout_threshold == 0) return;

    if (++t->counter >= t->timeout_threshold) {
        t->counter = 0;
        if (t->cb) t->cb(t->ctx);
    }
}




//每换一个平台，只写一份适配器表：



/* 平台 B：TMR + SysTick + RTC 混用 */
static const timer_hw_adapter_t hw_adapter[FRAME_TIMER_HW_MAX] = {
    [0] = { .start = timer1_start,  .stop = timer1_stop,  .init=timer1_init, .restart=timer1_restart, .clear_isr_flag=timer1_clear_isr_flag},

};

/* ============================================================
 *  frame_timer_t 包装层 —— 将 hw_id 封装为通用 frame_timer_t 接口
 *  外部通过 frame_timer_hw_create() 获取定时器，只依赖 ops 表
 * ============================================================ */

#define TICK_PERIOD_US_DEFAULT  1000U   /* 默认定时器 tick 周期 1ms */

/* 包装实例：每个 hw_id 对应一个 frame_timer_t */
typedef struct {
    frame_timer_t base;
    uint8_t       hw_id;
} frame_timer_hw_wrapper_t;

static frame_timer_hw_wrapper_t hw_wrapper[FRAME_TIMER_HW_MAX];
static uint8_t                  hw_occupied_map = 0U;

/* ---- ops 实现：委托到 frame_timer_hw_* ---- */
static void hw_ops_start(frame_timer_t *t) {
    frame_timer_hw_wrapper_t *self = (frame_timer_hw_wrapper_t *)t;
    frame_timer_hw_start(self->hw_id);
}

static void hw_ops_stop(frame_timer_t *t) {
    frame_timer_hw_wrapper_t *self = (frame_timer_hw_wrapper_t *)t;
    frame_timer_hw_stop(self->hw_id);
}

static void hw_ops_restart(frame_timer_t *t) {
    frame_timer_hw_wrapper_t *self = (frame_timer_hw_wrapper_t *)t;
    frame_timer_hw_restart(self->hw_id);
}

static const frame_timer_ops_t hw_timer_ops = {
    .start   = hw_ops_start,
    .stop    = hw_ops_stop,
    .restart = hw_ops_restart,
};

/* ---- 工厂函数 ---- */
frame_timer_t* frame_timer_hw_create(timer_callback cb, void *ctx,
                                      uint16_t timeout_us,
                                      uint8_t hw_id) {
    if (hw_id >= FRAME_TIMER_HW_MAX) return NULL;
    if (hw_occupied_map & (1 << hw_id)) return NULL;  /* 已被占用 */

    hw_occupied_map |= (1 << hw_id);

    frame_timer_hw_wrapper_t *w = &hw_wrapper[hw_id];
    w->base.ops = &hw_timer_ops;
    w->hw_id    = hw_id;

    frame_timer_hw_init(hw_id, cb, ctx, TICK_PERIOD_US_DEFAULT);
    frame_timer_hw_set_timeout(hw_id, timeout_us);

    return &w->base;
}