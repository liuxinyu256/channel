#ifndef FRAME_TIMER_H
#define FRAME_TIMER_H
#include <stdlib.h>
#include <stdint.h>
#include "packet.h"

typedef struct data_channel data_channel_t;
typedef enum {
    CHANNEL_STATE_IDLE,        // 空闲：已创建、未启动
    CHANNEL_STATE_RUNNING,     // 运行：start执行完成，正常收发数据
    CHANNEL_STATE_STOPPING,    // 停止中：正在关闭硬件、清空缓存
    CHANNEL_STATE_ERROR        // 异常：硬件故障、缓冲区持续溢出、字节源断开
}channel_state; 

// 收帧回调：仅通知「当前通道有完整帧可读」，不传递帧缓冲区
typedef void (*recv_callback_t)     (uint8_t ch_id, void *user_data);
// 发送完成回调不变
typedef void (*send_done_callback_t)(uint8_t ch_id, void *user_data);

typedef struct {
    void (*create)      (data_channel_t *ch);       //创建数据通道
    void (*destroy)     (data_channel_t *ch);       //销毁数据通道
    void (*start)       (data_channel_t *ch);       //启动数据通道
    void (*stop)        (data_channel_t *ch);       //停止数据通道
    void (*send_frame)  (data_channel_t *ch, uint8_t *buf, uint16_t  len);                                //发送数据帧，数据通道调用这个函数将一帧数据发送出去
    int  (*read_frame)  (data_channel_t *ch, uint8_t *buf, uint16_t *frame_len,uint16_t buf_max_size);    //读取数据帧，上层读取
    void (*set_callback)(data_channel_t *ch, recv_callback_t cb, void *user_data);                        //设置接收数据帧完成的回调函数  
    void (*set_send_done_callback)(data_channel_t *ch, send_done_callback_t cb, void *user_data);         //设置发送完成回调函数
} data_channel_ops_t;


//数据通道结构体，数据通道下层接收原始数据的来源由 byte_source 定义，根据封包策略进行封包，得到一帧完整的数据包
struct data_channel{
    data_channel_ops_t *ops;               //数据通道操作
    //数据通道数据源
    uint8_t                 id;           //每个通道ID都有一个id
    channel_state           state;        //通道状态

    /* 用户上下文，回调时传回 */
    void                    *user_data;
    /* 调试/统计 */
    uint32_t                rx_frames;      /* 收帧总数 */
    uint32_t                tx_frames;      /* 发帧总数 */
    uint32_t                rx_overflows;   /* 溢出次数 */

    recv_callback_t         on_frame_recv; //储存帧接收完成函数指针
    send_done_callback_t    on_send_done;  //储存发送完成函数指针
    packetizer_t            *pkt;           //一个数据通道绑定一个packetizer实例 
};


#endif
