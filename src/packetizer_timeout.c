/**
 * 文件名: packetizer_timeout.c
 * 描述: 超时封包器实现，基于定时器的封包策略，适用于帧间隔长的场景比如说10ms的帧间隔
 *
 */
#include "packet.h"
#include "frame_timer.h"
#include <string.h>

//超时封包器类，继承自packetizer基类
typedef struct {
    packetizer_t  base;
    frame_timer_t          *timer;
    uint16_t               timeout_ticks;   /* 超时阈值（tick 数），策略判断 */
    uint8_t                timer_running;   /* 定时器是否在运行：0=停，1=运行中 */
}packetizer_Timer_t;

#define MAX_TIMEOUT_INSTANCES  4 //最大超时封包器实例数量

static packetizer_Timer_t timeout_instance[MAX_TIMEOUT_INSTANCES]; //超时封包器实例
static uint8_t            timeout_occupied_map =0U; //超时封包器实例占用情况


static void packetizer_Timer_init(packetizer_t *pkt);
static void packetizer_Timer_reset(packetizer_t *pkt);
static void packetizer_Timer_on_byte(packetizer_t *pkt);

/* ---- ops表 ---- */
static const packet_ops_t timeout_ops = {
    .init    = packetizer_Timer_init,
    .reset   = packetizer_Timer_reset,
    .on_byte = packetizer_Timer_on_byte,
};




//packet_ops_t中操作接口的超时封包器类实现
/**
 * @brief   初始化封包器
 * @param  pkt: 封包器指针
 */
static void packetizer_Timer_init(packetizer_t *pkt)
{
    packetizer_Timer_t *self = (packetizer_Timer_t *)pkt;

    if (self == NULL || self->base.ops == NULL) {
        return;
    }
    self->timer_running=0; //初始化定时器运行状态为停止
    self->base.Rxidx=0;    //清空缓冲区字节计数
    //清空缓冲区
    memset(self->base.Rxbuf, 0, sizeof(self->base.Rxbuf));

}

/**
 * @brief   重置封包器
 * @param  pkt: 需要重置的封包器指针
 */
static void packetizer_Timer_reset(packetizer_t *pkt)
{
    if (pkt == NULL || pkt->ops == NULL) return;
    packetizer_Timer_t *self = (packetizer_Timer_t *)pkt;

    self->timer_running = 0;
    self->base.Rxidx = 0;
    memset(self->base.Rxbuf, 0, sizeof(self->base.Rxbuf));
}


/**
*  基类方法 on_byte的实现
*  接收到一字节应该怎么处理
*/
static void packetizer_Timer_on_byte(packetizer_t *pkt) //接收到一个字节，进行处理
{

    if (pkt == NULL || pkt->ops == NULL) {
        return;
    }
    packetizer_Timer_t *self =(packetizer_Timer_t *)pkt; //获取超时封包器
    /* 首字节启动定时器，后续字节清零计数重新计时 */
    if (!self->timer_running) {
        self->timer->ops->start(self->timer);
        self->timer_running = 1;
    } else {
        self->timer->ops->restart(self->timer);
    }
}

/* 定时中断回调 —— 每次 tick 判定是否超时 */
void timeout_timer_callback(void *ctx) {
    if (ctx == NULL) return;
    packetizer_Timer_t *self = (packetizer_Timer_t *)ctx;

    /* 没到超时阈值，继续等 */
    if (self->timer->counter < self->timeout_ticks) return;

    /* 超时 → 停定时器 → 帧完成 → 重置 */
    if (self->timer && self->timer->ops) {
        self->timer->ops->stop(self->timer);
    }
    
    if (self->base.on_frame_finish) {
        self->base.on_frame_finish(self->base.Rxbuf, self->base.Rxidx);
    }

    packetizer_Timer_reset(&self->base);
}


/**
 * @brief 创建一个超时封包器，绑定外部定时器
 * @param timer 外部注入的定时器实例（硬件/软件均可），超时时间由 timer 自己负责
 * @param cb    帧完成回调，可传 NULL 后续注册
 *
 * 内部自动绑定：timer->ctx = pkt;  pkt->on_frame_finish = cb
 * @return 封包器基类指针，无可用实例时返回 NULL
 */
packetizer_t* packetizer_timeout_create(frame_timer_t *timer,
                                        uint16_t timeout_ticks,
                                        frame_finish_callback cb) {
    if (timer == NULL) return NULL;

    for (uint8_t i = 0; i < MAX_TIMEOUT_INSTANCES; i++) {
        if (!(timeout_occupied_map & (1 << i))) {
            timeout_occupied_map |= (1 << i);

            packetizer_Timer_t *inst = &timeout_instance[i];
            memset(inst, 0, sizeof(*inst));

            inst->base.ops          = &timeout_ops;
            inst->timer             = timer;
            inst->timeout_ticks     = timeout_ticks;
            inst->base.on_frame_finish = cb;

            /* timer ↔ packetizer 双向绑定 */
            if (timer->ops && timer->ops->set_callback) {
                timer->ops->set_callback(timer, timeout_timer_callback, &inst->base);
            }

            return &inst->base;
        }
    }
    return NULL;
}

void packetizer_timeout_destroy(packetizer_t *pkt) {
    if (pkt == NULL) return;

    for (uint8_t i = 0; i < MAX_TIMEOUT_INSTANCES; i++) {
        if (&timeout_instance[i].base == pkt) {
            packetizer_Timer_t *self = &timeout_instance[i];
            /* 解除 timer→pkt 绑定 */
            if (self->timer && self->timer->ops && self->timer->ops->set_callback) {
                self->timer->ops->set_callback(self->timer, NULL, NULL);
            }
            self->timer      = NULL;
            self->base.on_frame_finish = NULL;
            timeout_occupied_map &= (uint8_t)~(1U << i);
            return;
        }
    }
}


