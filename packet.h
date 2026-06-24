#ifndef PACKET_H
#define PACKET_H
/**
 * 接收封包器
 */
#include <stdlib.h>
#include <stdint.h>


#define RX_PACKET_BUF_SIZE  256

typedef enum { 
    packetizer_type_Timeout, //超时封包策略
}packetizer_type; //预留不同封包策略的枚举类型

typedef void (*frame_finish_callback)(uint8_t *frame, uint16_t len);

typedef struct packetizer packetizer_t;
//基类虚函数表，子类需要实现这些函数接口
typedef struct{
    void (*init)(packetizer_t *pkt);    
    void (*reset)(packetizer_t *pkt); 
    void (*on_byte)(packetizer_t *pkt);
    uint8_t (*is_frame_complete)(packetizer_t *pkt);
}packet_ops_t;

/**packetizer封包器基类，定义了封包器的基本属性和操作接口
 * @brief 封包器基类
 * 使用封包器时，需要继承该类，并实现packet_ops_t中的操作接口
*/
struct packetizer{
    const packet_ops_t    *ops;                         //封包器操作接口    
    packetizer_type       type;                           //封包策略类型    
    uint8_t               Rxbuf[RX_PACKET_BUF_SIZE];        //接收缓存
    volatile uint16_t     Rxidx;                            //接收缓存索引      
    frame_finish_callback on_frame_finish;                   //帧完成回调函数指针,channel层注册这个回调函数，当packetizer完成一帧数据的封包时调用，通知channel层有完整帧可读
};
/*********************************************************
 *                【基类通用公有方法】
 * 说明：基类实现的通用逻辑，所有子类直接调用，无需重写
 *********************************************************/
uint8_t   packetizer_put_byte(packetizer_t *pkt, uint8_t byte); //将一个字节放入packet中，返回0表示成功，返回1表示失败（如溢出）
uint8_t   get_frame(packetizer_t *pkt,uint8_t *buf,uint16_t *len); //从packetizer中读取一帧数据，传入一个需要接收这帧数据的缓冲区和一个指向缓冲区大小的指针，函数将帧数据复制到缓冲区，并将实际帧长度写入指针指向的位置，返回0表示成功，返回1表示失败（如没有完整帧可读）
uint8_t   finish(packetizer_t *pkt); //封包完成，返回0表示成功，返回1表示失败
void      set_frame_finish_callback(packetizer_t *pkt, frame_finish_callback cb); //设置帧完成回调函数，packetizer在完成一帧数据的封包时调用这个回调函数，通知channel层有完整帧可读

/*************************************************************************************************************************** 
 * 超时封包器创建函数:创建一个基于超时封包策略的packetizer实例，传入超时时间参数，返回指向packetizer_t的指针，如果没有可用实例则返回NULL
 ****************************************************************************************************************************/
packetizer_t* packetizer_timeout_create(uint16_t timeout);

#endif
