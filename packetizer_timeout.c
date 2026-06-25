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
    packetizer_t  base;                             //继承packetizer基类
    //超时封包器特有的属性
    uint16_t               timeout_ticks;             //超时时间（微秒）
    frame_timer_t          *timer;                   //注入的定时器实例（硬件/软件/模拟均可）
}packetizer_Timer_t;

#define MAX_TIMEOUT_INSTANCES  4 //最大超时封包器实例数量

static packetizer_Timer_t timeout_instance[MAX_TIMEOUT_INSTANCES]; //超时封包器实例
static uint8_t            timeout_occupied_map =0U;                     //超时封包器实例占用情况

//packet_ops_t中操作接口的超时封包器类实现
/**
 * @brief   初始化封包器
 * @param  pkt: 封包器指针
 * @param  timeout: 超时时间
 */
static void packetizer_Timer_init(packetizer_t *pkt)
{   
    packetizer_Timer_t *self = (packetizer_t *)pkt;

    if (self == NULL||self->base.ops==NULL) {
        return;
    }

    self->base.Rxidx=0;
    
    self->base.on_frame_finish=NULL;//初始化帧完成回调函数指针为NULL
    //清空缓冲区
    memset(self->base.Rxbuf, 0, sizeof(self->base.Rxbuf));
    
}
/**
 * @brief   重置封包器
 * @param  pkt: 需要重置的封包器指针
 */
static void packetizer_Timer_reset(packetizer_t *pkt)
{
  
    if (pkt == NULL || pkt->ops == NULL) {
        return;
    }
    packetizer_Timer_t *self = (packetizer_t *)pkt;
    self->base.Rxidx = 0;
    memset(self->base.Rxbuf, 0, sizeof(self->base.Rxbuf));
    self->timeout_ticks=0;
}


/**
 * @brief   is_frame_complete 判断是否接收完成一帧数据
 * @param   packetizer_t *pkt 指针
 */

static uint8_t is_frame_complete(packetizer_t *pkt) //判断一帧是否接收完成
{
    if (pkt == NULL || pkt->strategy == NULL) {
        return 0;
    }

}
/**
*  基类方法 on_byte的实现
*  接收到一字节应该怎么处理
*/
static void packetizer_Timer_on_byte(packetizer_t *pkt) //接收到一个字节，进行处理
{

    if (pkt == NULL || pkt->strategy == NULL) {
        return;
    }
    packetizer_Timer_t *self =(packetizer_Timer_t *)pkt; //获取超时封包器
    /**对于超时封包器而言，接收到一个字节就应该清空超时计数器的计数值，清空定时器计数寄存器的计数值
    * 
    */
   
}
/* ---- 策略实现：注册双表 ---- */
static const packet_ops_t timeout_ops = {
    .init  = packetizer_Timer_init,
    .reset = packetizer_Timer_reset,
};

static const packet_strategy_t timeout_strategy = {
    .on_byte           = packetizer_Timer_on_byte,
    .is_frame_complete = is_frame_complete,
};

//定时中断回调函数
static void timeout_timer_callback(void *ctx) {
    if(ctx==NULL){
        return;
    }
    packetizer_Timer_t *self = (packetizer_Timer_t *)ctx;
    if(self->timer==NULL || self->base.strategy==NULL){
        return;
    }
    
    

    self->base.Rxidx = 0; //重置接收索引，准备接收下一帧数据
    memset(self->base.Rxbuf, 0, sizeof(self->base.Rxbuf)); //清空接收缓冲区
}


/**
 * @brief 创建一个带超时功能的数据包解析器
 * @param timeout_us 帧间超时时间（微秒），超过此时间未收到新字节即认为一帧完成
 * @param timer      外部注入的定时器实例（frame_timer_hw_create / frame_timer_sw_create 等）
 *                   封包器不关心定时器实现，只调用 timer->ops 接口
 * @return 封包器基类指针，无可用实例时返回 NULL
 */
packetizer_t* packetizer_timeout_create(uint16_t timeout_us, frame_timer_t *timer) {
    if (timer == NULL) return NULL;

    for (uint8_t i = 0; i < MAX_TIMEOUT_INSTANCES; i++) {
        if (!(timeout_occupied_map & (1 << i))) {
            timeout_occupied_map |= (1 << i);
            timeout_instance[i].base.ops      = &timeout_ops;
            timeout_instance[i].base.strategy = &timeout_strategy;
            timeout_instance[i].timer       = timer;                     /* 外部注入，不内部创建 */
            timeout_instance[i].timeout_ticks = timeout_us;
            timeout_instance[i].base.Rxidx  = 0;
            memset(timeout_instance[i].base.Rxbuf, 0, sizeof(timeout_instance[i].base.Rxbuf));

            return &timeout_instance[i].base;
        }
    }
    return NULL;
}

void packetizer_timeout_destroy(packetizer_t *pkt) {
    if (pkt == NULL) return;

    for (uint8_t i = 0; i < MAX_TIMEOUT_INSTANCES; i++) {
        if (&timeout_instance[i].base == pkt) {
            timeout_instance[i].timer = NULL;
            timeout_occupied_map &= ~(1 << i);
            return;
        }
    }
}