/********************************** (C) COPYRIGHT *******************************
 * File Name          : frame_timer_hw.c
 * Description        : 硬件定时器子类 —— CH579 TMR0-3 适配
 *
 * 继承关系：
 *   frame_timer_hw_inst_t "继承" frame_timer_t（基类作为第一个成员）
 *
 * 平台适配器表：
 *   每个 hw_id 绑定一组 BSP 函数指针。CH579 的 TMRx_Enable/Disable
 *   是宏而非函数，因此需要包装函数来生成函数指针。
 *******************************************************************************/

#include "frame_timer_hw.h"

#define HW_MAX  4

/* ============================================================
 *  平台适配器接口
 * ============================================================ */
typedef struct {
    uint16_t (*init)         (void);   /* 初始化硬件，返回 tick 周期(us) */
    void     (*start)        (void);   /* 启动计数                        */
    void     (*stop)         (void);   /* 停止计数                        */
    void     (*restart)      (void);   /* 复位计数器                      */
    void     (*clear_flag)   (void);   /* 清除中断标志                    */
} hw_adapter_t;

/* ============================================================
 *  TMRx 宏 → 函数指针 包装（每个定时器一套）
 * ============================================================ */

/* TMR0 */
static uint16_t tmr0_init(void) {
    TMR0_TimerInit(FREQ_SYS / 1000);
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    NVIC_SetPriority(TMR0_IRQn, 1);
    NVIC_EnableIRQ(TMR0_IRQn);
    return 1000;
}
static void tmr0_start(void)   { TMR0_Enable(); }
static void tmr0_stop(void)    { TMR0_Disable(); }
static void tmr0_restart(void) { TMR0_Disable(); TMR0_Enable(); }
static void tmr0_clr(void)     { TMR0_ClearITFlag(TMR0_3_IT_CYC_END); }

/* TMR1 */
static uint16_t tmr1_init(void) {
    TMR1_TimerInit(FREQ_SYS / 1000);
    TMR1_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    NVIC_EnableIRQ(TMR1_IRQn);
    return 1000;
}
static void tmr1_start(void)   { TMR1_Enable(); }
static void tmr1_stop(void)    { TMR1_Disable(); }
static void tmr1_restart(void) { TMR1_Disable(); TMR1_Enable(); }
static void tmr1_clr(void)     { TMR1_ClearITFlag(TMR0_3_IT_CYC_END); }

/* TMR2 */
static uint16_t tmr2_init(void) {
    TMR2_TimerInit(FREQ_SYS / 1000);
    TMR2_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    NVIC_EnableIRQ(TMR2_IRQn);
    return 1000;
}
static void tmr2_start(void)   { TMR2_Enable(); }
static void tmr2_stop(void)    { TMR2_Disable(); }
static void tmr2_restart(void) { TMR2_Disable(); TMR2_Enable(); }
static void tmr2_clr(void)     { TMR2_ClearITFlag(TMR0_3_IT_CYC_END); }

/* TMR3 */
static uint16_t tmr3_init(void) {
    TMR3_TimerInit(FREQ_SYS / 1000);
    TMR3_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    NVIC_EnableIRQ(TMR3_IRQn);
    return 1000;
}
static void tmr3_start(void)   { TMR3_Enable(); }
static void tmr3_stop(void)    { TMR3_Disable(); }
static void tmr3_restart(void) { TMR3_Disable(); TMR3_Enable(); }
static void tmr3_clr(void)     { TMR3_ClearITFlag(TMR0_3_IT_CYC_END); }

/* ============================================================
 *  适配器表 —— hw_id 映射到 BSP 函数（换平台只改此表）
 * ============================================================ */
static const hw_adapter_t adapter[HW_MAX] = {
    { tmr0_init, tmr0_start, tmr0_stop, tmr0_restart, tmr0_clr },
    { tmr1_init, tmr1_start, tmr1_stop, tmr1_restart, tmr1_clr },
    { tmr2_init, tmr2_start, tmr2_stop, tmr2_restart, tmr2_clr },
    { tmr3_init, tmr3_start, tmr3_stop, tmr3_restart, tmr3_clr },
};

/* ============================================================
 *  子类实例（继承关系: base 作为第一个成员）
 * ============================================================ */
typedef struct {
    frame_timer_t base;
    uint8_t       hw_id;
} frame_timer_hw_inst_t;

static frame_timer_hw_inst_t instances[HW_MAX];
static uint8_t               occupied = 0U;

/* ============================================================
 *  ops 回调实现（向下转型访问子类字段）
 * ============================================================ */
static void hw_start(frame_timer_t *t) {
    frame_timer_hw_inst_t *self = (frame_timer_hw_inst_t *)t;
    t->counter = 0;
    adapter[self->hw_id].start();
}

static void hw_stop(frame_timer_t *t) {
    frame_timer_hw_inst_t *self = (frame_timer_hw_inst_t *)t;
    adapter[self->hw_id].stop();
    t->counter = 0;
}

static void hw_restart(frame_timer_t *t) {
    frame_timer_hw_inst_t *self = (frame_timer_hw_inst_t *)t;
    t->counter = 0;
    adapter[self->hw_id].restart();
}

static void hw_set_cb(frame_timer_t *t, timer_callback cb, void *ctx) {
    t->cb  = cb;
    t->ctx = ctx;
}

static const frame_timer_ops_t hw_ops = {
    hw_start, hw_restart, hw_stop, hw_set_cb
};

/* ============================================================
 *  工厂函数 —— 唯一对外创建入口
 *  定时器初始化为停止状态，由策略按需启动。
 * ============================================================ */
frame_timer_t* frame_timer_hw_create(uint8_t hw_id) {
    if (hw_id >= HW_MAX) return NULL;
    if (occupied & (1 << hw_id)) return NULL;

    occupied |= (uint8_t)(1U << hw_id);

    frame_timer_hw_inst_t *inst = &instances[hw_id];
    inst->base.ops     = &hw_ops;
    inst->base.cb      = NULL;
    inst->base.ctx     = NULL;
    inst->base.counter = 0;
    inst->hw_id        = hw_id;

    adapter[hw_id].init();
    adapter[hw_id].stop();    /* 初始停止，等上层 start() */

    return &inst->base;
}

void frame_timer_hw_destroy(frame_timer_t *t) {
    if (t == NULL) return;
    frame_timer_hw_inst_t *inst = (frame_timer_hw_inst_t *)t;
    uint8_t hw_id = inst->hw_id;

    adapter[hw_id].stop();
    t->cb  = NULL;
    t->ctx = NULL;
    occupied &= (uint8_t)~(1U << hw_id);
}

/* ============================================================
 *  ISR 入口 —— 由各 TMRx_IRQHandler 调用
 *  职责：清标志 → 递增 counter → 调用回调
 * ============================================================ */
void frame_timer_hw_isr(uint8_t hw_id) {
    if (!(occupied & (1 << hw_id))) return;

    frame_timer_hw_inst_t *t = &instances[hw_id];
    adapter[hw_id].clear_flag();
    t->base.counter++;
    if (t->base.cb) t->base.cb(t->base.ctx);
}
