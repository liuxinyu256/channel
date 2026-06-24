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

/* ---- 3. 适配器表 —— 每平台实现一份 ---- */
static const timer_hw_adapter_t hw_adapter[FRAME_TIMER_HW_MAX];

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
static const timer_hw_adapter_t hw_adapter[] = {
    [0] = { .start = timer1_start,  .stop = timer1_stop,  .init=timer1_init, .restart=timer1_restart, .clear_isr_flag=timer1_clear_isr_flag},

};