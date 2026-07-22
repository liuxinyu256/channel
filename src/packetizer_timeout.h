/********************************** (C) COPYRIGHT *******************************
 * File Name          : packetizer_timeout.h
 * Description        : 超时封包策略 —— 注入 frame_timer，支持多实例
 *
 * 使用:
 *   1. timer = frame_timer_hw_create(0);
 *   2. pkt   = packetizer_timeout_create(timer, 5, cb);
 *   3. UART ISR: packetizer_put_byte(pkt, byte);
 *   4. 超时后 cb(frame, len) 自动触发
 *******************************************************************************/

#ifndef PACKETIZER_TIMEOUT_H
#define PACKETIZER_TIMEOUT_H

#include "packet.h"
#include "frame_timer.h"

packetizer_t* packetizer_timeout_create(frame_timer_t *timer,
                                        uint16_t timeout_ticks,
                                        frame_finish_callback cb);
void packetizer_timeout_destroy(packetizer_t *pkt);

#endif
