#ifndef PACKETIZER_TIMEOUT_H
#define PACKETIZER_TIMEOUT_H
#include "packet.h"

/**
 * @file packetizer_timeout.h
 * @brief 超时封包策略 —— 基于帧间超时判定帧边界
 *
 * 使用步骤:
 *   1. timer = frame_timer_hw_create(timeout_timer_callback, NULL, 10000, 0);
 *   2. pkt   = packetizer_timeout_create(timer, my_callback);
 *      // 内部自动: timer->ctx = pkt;  pkt->on_frame_finish = my_callback
 *   3. UART ISR 中 packetizer_put_byte(pkt, byte);
 *   4. 超时后 my_callback(frame, len) 自动触发
 */

/**
 * @brief 创建一个超时封包器，绑定外部定时器
 * @param timer 外部注入的定时器实例（硬件/软件均可）
 * @param cb    帧完成回调，可传 NULL 后续注册
 * @return 封包器基类指针，无可用实例时返回 NULL
 */
packetizer_t* packetizer_timeout_create(frame_timer_t *timer,
                                        frame_finish_callback cb);

/** @brief 销毁超时封包器，归还槽位，解绑 timer */
void packetizer_timeout_destroy(packetizer_t *pkt);

/**
 * @brief 超时定时器回调 —— 传给 frame_timer_XX_create 的 cb 参数
 *
 * 定时器超时后调用，内部完成: 停定时器 → 触发 on_frame_finish → 清 Rxbuf。
 * ctx 必须为 packetizer_timeout_create 创建的 packetizer_t 指针。
 */
void timeout_timer_callback(void *ctx);

#endif
