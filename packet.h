#ifndef PACKET_H
#define PACKET_H
/**
 * 接收封包器
 */
#include <stdlib.h>
#include <stdint.h>


#define RX_PACKET_BUF_SIZE  256

typedef enum { 
    packetizer_type_Timeout,    /* 超时封包策略 */
}packetizer_type;           /* 预留不同封包策略的枚举类型 */

typedef void (*frame_finish_callback)(uint8_t *frame, uint16_t len);

typedef struct packetizer packetizer_t;
typedef struct frame_timer frame_timer_t;  /* 前置声明，供封包器注入定时器 */
/**
 * packet_ops_t — 公开操作接口
 * 上层（channel 层）通过 pkt->ops 调用，负责生命周期管理
 */
typedef struct {
    void (*init) (packetizer_t *pkt);   /* 初始化封包器（绑定策略后调用） */
    void (*reset)(packetizer_t *pkt);   /* 重置封包器（切换策略前调用）*/
    void    (*on_byte)         (packetizer_t *pkt);  /* 收到一个字节时基类内部调用 */
    uint8_t (*is_frame_complete)(packetizer_t *pkt); /* 是否收完一帧，基类内部调用 */
} packet_ops_t;



/**
 * packetizer 封包器基类
 *
 * 使用方式：
 *   1. 调用工厂函数创建实例（内部绑定 ops + strategy）
 *   2. 上层通过 pkt->ops->init() 初始化
 *   3. 切换策略：pkt->ops->reset() → 注入新策略
 */
struct packetizer{
    const packet_ops_t      *ops;       /* 公开操作（上层调用）        */
    packetizer_type         type;       /* 封包策略类型                */
    uint8_t                 Rxbuf[RX_PACKET_BUF_SIZE]; /* 接收缓存    */
    uint16_t                Rxidx;      /* 接收缓存索引（仅 put_byte 写入，无 ISR 并发） */
    frame_finish_callback   on_frame_finish;  /* 帧完成回调             */
};
/*********************************************************
 *                【基类通用公有方法】
 * 说明：基类实现的通用逻辑，所有子类直接调用，无需重写
 *********************************************************/
void     packetizer_init(packetizer_t *pkt); //初始化封包器，调用策略的init方法
void     packetizer_reset(packetizer_t *pkt); //重置封包器
uint8_t   packetizer_put_byte(packetizer_t *pkt, uint8_t byte); //将一个字节放入packet中，返回0表示成功，返回1表示失败（如溢出）
uint8_t   get_frame(packetizer_t *pkt,uint8_t *buf,uint16_t *len); //从packetizer中读取一帧数据，传入一个需要接收这帧数据的缓冲区和一个指向缓冲区大小的指针，函数将帧数据复制到缓冲区，并将实际帧长度写入指针指向的位置，返回0表示成功，返回1表示失败（如没有完整帧可读）
uint8_t   finish(packetizer_t *pkt); //封包完成，返回0表示成功，返回1表示失败
void      set_frame_finish_callback(packetizer_t *pkt, frame_finish_callback cb); //设置帧完成回调函数，packetizer在完成一帧数据的封包时调用这个回调函数，通知channel层有完整帧可读

/*************************************************************************************************************************** 
 * 超时封包器创建函数:创建一个基于超时封包策略的packetizer实例，传入超时时间参数，返回指向packetizer_t的指针，如果没有可用实例则返回NULL
 ****************************************************************************************************************************/
packetizer_t* packetizer_timeout_create(uint16_t timeout_us, frame_timer_t *timer);
void          packetizer_timeout_destroy(packetizer_t *pkt); /* 释放封包器实例，归还 bitmap 槽位 */
void          timeout_timer_callback(void *ctx);             /* 超时封包器标准定时回调，供 frame_timer_hw_create 使用 */

#endif