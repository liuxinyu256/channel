/* ============================================================
 * frame_timer_hw.c —— 硬件定时器子类实现
 *
 * 继承关系：
 *   frame_timer_hw_inst_t  "继承" frame_timer_t（C 风格：基类作为第一个成员）
 *
 * 字段分层：
 *   基类 frame_timer_t —— ops / cb / ctx / counter
 *   子类独有           —— hw_id（硬件通道号），tick 周期由 init() 返回，不存
 *
 * counter 驱动方式：
 *   硬件 ISR → frame_timer_hw_isr() → ++t->base.counter
 *   （软件定时器则通过 systick 钩子递增，驱动方式不同但 counter 字段共用）
 * ============================================================ */

#include "frame_timer_hw.h"
#include "timer1.h"

#define FRAME_TIMER_HW_MAX  4              /* 最多支持的硬件定时器通道数 */

/* ============================================================
 *  1. 硬件适配器接口（timer_hw_adapter_t）
 *
 *  抽象硬件寄存器操作，换平台只需换适配器表（见文件末尾）。
 *  每个方法对应当前平台的 HAL / 寄存器操作。
 * ============================================================ */
typedef struct {
    uint16_t (*init)     (void);            /* 初始化硬件定时器，返回 tick 周期（us） */
    void (*start)    (void);               /* 启动定时器计数                 */
    void (*stop)     (void);               /* 停止定时器计数                 */
    void (*restart)  (void);               /* 重启定时器（清零硬件计数器）    */
    void (*set_cmp)  (uint16_t ticks);     /* 设置定时器周期                  */
    void (*clear_isr_flag)(void);          /* 清除硬件中断标志               */
} timer_hw_adapter_t;

/* ============================================================
 *  2. 子类实例结构体
 *
 *  base 必须是第一个成员，保证 (frame_timer_t*)&inst == &inst.base，
 *  从而 ops 回调中可以从基类指针安全地 cast 回子类指针。
 *
 *  子类独有字段：
 *    hw_id          - 绑定到哪个硬件定时器通道（0 ~ FRAME_TIMER_HW_MAX-1）
 *    tick_period_us - 一次硬件 tick 的周期（微秒），用于 timeout_us → tick 数 转换
 * ============================================================ */
typedef struct {
    frame_timer_t base;                    /* [基类] 对外接口 + 引擎数据     */
    uint8_t       hw_id;                   /* [子类] 硬件通道号              */
} frame_timer_hw_inst_t;

/* ---- 实例池与位图分配器 ---- */
static frame_timer_hw_inst_t hw_instances[FRAME_TIMER_HW_MAX];
static uint8_t               hw_occupied_map = 0U;

/* ============================================================
 *  平台适配器表
 *
 *  换平台只需改这张表，其余代码不变。
 *  当前绑定：TIM1 作为 hw_id=0 的硬件定时器。
 *  新增通道在数组中追加即可（如 [1] = { timer2_... }）。
 * ============================================================ */
static const timer_hw_adapter_t hw_adapter[FRAME_TIMER_HW_MAX] = {
    [0] = { .start = timer1_start, .stop = timer1_stop,
            .init = timer1_init, .restart = timer1_restart,
            .clear_isr_flag = timer1_clear_isr_flag },
};
/* ============================================================
 *  3. ops 回调实现
 *
 *  t : frame_timer_t* —— 基类指针。
 *  self : frame_timer_hw_inst_t* —— 向下转型后的子类指针。
 *  提供给上层实现的硬件定时器操作。
 *  关键约定：
 *    - t->counter（基类字段）直接用 t 指针访问，无需转型
 *    - hw_id（子类字段）必须通过 self 访问
 * ============================================================ */

static void hw_start(frame_timer_t *t) {
    frame_timer_hw_inst_t *self = (frame_timer_hw_inst_t *)t;
    t->counter = 0;              /* 基类字段：清零 tick 计数值        */
    hw_adapter[self->hw_id].start();
}

static void hw_stop(frame_timer_t *t) {
    frame_timer_hw_inst_t *self = (frame_timer_hw_inst_t *)t;
    hw_adapter[self->hw_id].stop();
    t->counter = 0;              /* 基类字段：停定时器同时清计数值     */
}

static void hw_restart(frame_timer_t *t) {
    frame_timer_hw_inst_t *self = (frame_timer_hw_inst_t *)t;
    t->counter = 0;              /* 基类字段：重新从零计数            */
    hw_adapter[self->hw_id].restart();
}

static void hw_set_callback(frame_timer_t *t, timer_callback cb, void *ctx) {
    t->cb  = cb;
    t->ctx = ctx;
}

/* ---- 硬件定时器的多态操作表 ---- */
static const frame_timer_ops_t hw_timer_ops = {
    .start        = hw_start,
    .stop         = hw_stop,
    .restart      = hw_restart,
    .set_callback = hw_set_callback,
};

/* ============================================================
 *  4. 工厂函数 —— 唯一的对外创建入口
 *
 *  入参：hw_id - 硬件定时器通道号（0~3），对应适配器表
 *
 *  定时器自由运行，每 tick 触发回调。超时由上层策略判断。
 *  回调通过 timer->ops->set_callback() 注入。
 * ============================================================ */


frame_timer_t* frame_timer_hw_create(uint8_t hw_id) {
    if (hw_id >= FRAME_TIMER_HW_MAX) return NULL;
    if (hw_occupied_map & (1 << hw_id)) return NULL;
    if (hw_adapter[hw_id].init == NULL) return NULL;

    hw_occupied_map |= (uint8_t)(1U << hw_id);

    frame_timer_hw_inst_t *inst = &hw_instances[hw_id];

    inst->base.ops     = &hw_timer_ops;
    inst->base.cb      = NULL;
    inst->base.ctx     = NULL;
    inst->base.counter = 0;
    inst->hw_id        = hw_id;

    /* 初始化硬件，定时器自由运行。tick 周期由 BSP 决定，超时由策略判断 */
    uint16_t tick_period_us = hw_adapter[hw_id].init();
    hw_adapter[hw_id].set_cmp(1);  /* 自动重载值 = 1，每个 tick 触发 ISR */

    return &inst->base;
}

/* ============================================================
 *  5. ISR 入口 —— 由平台中断服务例程调用
 *
 *  职责：清标志 → 递增 counter → 通知回调。是否超时由策略判断。
 * ============================================================ */
void frame_timer_hw_isr(uint8_t hw_id) {
    if (!(hw_occupied_map & (1 << hw_id))) return;  /* 未分配，安全忽略    */

    frame_timer_hw_inst_t *t = &hw_instances[hw_id];
    hw_adapter[hw_id].clear_isr_flag();

    /* 每次 tick 递增计数器，通知上层。是否超时由策略判断 */
    t->base.counter++;
    if (t->base.cb) t->base.cb(t->base.ctx);
}



/* ============================================================
 *  6. destroy —— 释放定时器资源
 *
 *  停止硬件定时器，清回调，归还 bitmap 槽位。
 * ============================================================ */
void frame_timer_hw_destroy(frame_timer_t *t) {
    if (t == NULL) return;
    frame_timer_hw_inst_t *inst = (frame_timer_hw_inst_t *)t;
    uint8_t hw_id = inst->hw_id;

    hw_adapter[hw_id].stop();
    t->cb  = NULL;
    t->ctx = NULL;
    hw_occupied_map &= (uint8_t)~(1U << hw_id);
}
