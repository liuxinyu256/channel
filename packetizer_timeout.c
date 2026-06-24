/**
 * 文件名: packetizer_timeout.c
 * 描述: 超时封包器实现，基于定时器的封包策略，适用于帧间隔长的场景比如说10ms的帧间隔
 * 
 */
#include "packet.h"
#include "frame_timer.h"
//超时封包器类，继承自packetizer基类
typedef struct {
    packetizer_t  base;                             //继承packetizer基类
    //超时封包器特有的属性
    uint8_t                timer_id;                 //绑定定时器id
    uint16_t               timeout_ticks;             //超时时间 
   
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
    packetizer_Timer_t *self = (packetizer_Timer_t *)pkt;

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
    packetizer_Timer_t *self = (packetizer_Timer_t *)pkt;
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
    if (pkt == NULL || pkt->ops == NULL) {
        return 0;
    }

}
static void packetizer_Timer_on_byte(packetizer_t *pkt) //接收到一个字节，进行处理
{
    if (pkt == NULL || pkt->ops == NULL) {
        return;
    }

}
//注册接口
const packet_ops_t timeout_packet_ops = {
    .init              = packetizer_Timer_init,
    .reset             = packetizer_Timer_reset,
    .is_frame_complete = is_frame_complete,
    .on_byte           = packetizer_Timer_on_byte,
    
};

//定时中断回调函数
static void timeout_timer_callback(void *ctx) {
    if(ctx==NULL){
        return;
    }
    packetizer_Timer_t *self = (packetizer_Timer_t *)ctx;
    if(self->timer==NULL || self->base.ops==NULL){
        return;
    }
    
    

    self->base.Rxidx = 0; //重置接收索引，准备接收下一帧数据
    memset(self->base.Rxbuf, 0, sizeof(self->base.Rxbuf)); //清空接收缓冲区
}


/**
 * @brief 创建一个带超时功能的数据包解析器
 * @param timeout_us 超时时间，单位为微秒,数值上至少要大于两个字节的长度，转换公式为：timeout_us=
 */
packetizer_t* packetizer_timeout_create(uint16_t timeout_us) {
  
    for(uint8_t i=0;i<MAX_TIMEOUT_INSTANCES;i++){
        if(!(timeout_occupied_map & (1 << i))){ //判断这块内存是否被占用
            timeout_occupied_map|=(1<<i); //标记这块内存被占用
            timeout_instance[i].base.ops=&timeout_packet_ops; //设置虚函数表指针 
            timeout_instance[i].timer=frame_timer_create_hw(timeout_timer_callback, &timeout_instance[i], timeout_us); //创建硬件定时器实例
            timeout_instance[i].timeout_ticks= timeout_us;
            timeout_instance[i].base.Rxidx=0; //初始化接收索引
            memset(timeout_instance[i].base.Rxbuf, 0, sizeof(timeout_instance[i].base.Rxbuf)); //清空接收缓冲区
       
            return &timeout_instance[i].base; //返回基类指针
        }

    }
    return NULL; //没有可用实例，返回NULL
}