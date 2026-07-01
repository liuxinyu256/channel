#ifndef __packetizer_timeout_h
#define __packetizer_timeout_h
#include "packet.h"
/**
 * @file packetizer_timeout.h
 * @brief 超时封包器
 * @details  两个接口，packetizer_timeout_create 创建一个超时封包器
 *           packetizer_timeout_destroy         销毁一个超时封包器
 */

/**
 * @brief 创建一个带超时功能的数据包解析器
 * @param timeout_us 帧间超时时间（微秒），超过此时间未收到新字节即认为一帧完成
 * @param timer      外部注入的定时器实例（frame_timer_hw_create / frame_timer_sw_create 等）
 *                   封包器不关心定时器实现，只调用 timer->ops 接口
 * @return 封包器基类指针，无可用实例时返回 NULL
 */
packetizer_t* packetizer_timeout_create(uint16_t timeout_us, frame_timer_t *timer);
void timeout_timer_callback(void *ctx); //超时封包器定时回调函数
void packetizer_timeout_destroy(packetizer_t *pkt);

#endif
