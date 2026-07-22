/********************************** (C) COPYRIGHT *******************************
 * File Name          : packetizer_timeout.c
 * Description        : 超时封包器 —— 注入 frame_timer，bitmap 实例池 (最多4个)
 *******************************************************************************/

#include "packetizer_timeout.h"
#include <string.h>

typedef struct {
    packetizer_t   base;
    frame_timer_t *timer;
    uint16_t       timeout_ticks;
    uint8_t        timer_running;
} packetizer_Timer_t;

#define MAX_INST  4
static packetizer_Timer_t instances[MAX_INST];
static uint8_t            occupied_map = 0U;

/* ---- ops ---- */
static void to_init(packetizer_t *pkt) {
    packetizer_Timer_t *s = (packetizer_Timer_t *)pkt;
    s->timer_running = 0;
    ring_init(&pkt->ring);
}

static void to_reset(packetizer_t *pkt) {
    packetizer_Timer_t *s = (packetizer_Timer_t *)pkt;
    if (s->timer && s->timer->ops) s->timer->ops->stop(s->timer);
    s->timer_running = 0;
    ring_reset(&pkt->ring);  /* 丢弃当前帧 */
}

static void to_on_byte(packetizer_t *pkt) {
    packetizer_Timer_t *s = (packetizer_Timer_t *)pkt;
    if (!s->timer_running) {
        if (s->timer && s->timer->ops) s->timer->ops->start(s->timer);
        s->timer_running = 1;
    } else {
        if (s->timer && s->timer->ops) s->timer->ops->restart(s->timer);
    }
}

static const packet_ops_t timeout_ops = { to_init, to_reset, to_on_byte };

/* ---- 定时器 tick 回调 ---- */
static void on_timeout(void *ctx) {
    packetizer_Timer_t *s = (packetizer_Timer_t *)ctx;
    if (s == NULL || s->timer == NULL) return;
    if (s->timer->counter < s->timeout_ticks) return;

    if (s->timer->ops) s->timer->ops->stop(s->timer);

    /* 从字节环拷到输出队列，push_frame 内部回调 on_frame_finish */
    packetizer_push_frame(&s->base);
    s->timer_running = 0;  /* timer 已停止，同步状态 */
}

/* ---- 工厂 ---- */
packetizer_t* packetizer_timeout_create(frame_timer_t *timer,
                                        uint16_t timeout_ticks,
                                        frame_finish_callback cb) {
    uint8_t i;
    if (timer == NULL) return NULL;

    for (i = 0; i < MAX_INST; i++) {
        if (!(occupied_map & (1 << i))) {
            occupied_map |= (uint8_t)(1U << i);
            packetizer_Timer_t *inst = &instances[i];
            memset(inst, 0, sizeof(*inst));
            inst->base.ops            = &timeout_ops;
            inst->timer               = timer;
            inst->timeout_ticks       = timeout_ticks;
            inst->base.on_frame_finish = cb;

            if (timer->ops && timer->ops->set_callback)
                timer->ops->set_callback(timer, on_timeout, &inst->base);

            return &inst->base;
        }
    }
    return NULL;
}

/* ---- 销毁 ---- */
void packetizer_timeout_destroy(packetizer_t *pkt) {
    uint8_t i;
    if (pkt == NULL) return;
    for (i = 0; i < MAX_INST; i++) {
        if (&instances[i].base == pkt) {
            packetizer_Timer_t *s = &instances[i];
            if (s->timer && s->timer->ops && s->timer->ops->set_callback)
                s->timer->ops->set_callback(s->timer, NULL, NULL);
            s->timer               = NULL;
            s->base.on_frame_finish = NULL;
            occupied_map &= (uint8_t)~(1U << i);
            return;
        }
    }
}
