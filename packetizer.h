/**
 * @file packetizer.h
 * @brief 封包器框架 —— 唯一公开头文件
 *
 * 用户只需 #include "packetizer.h" 即可使用全部功能。
 *
 * 使用步骤:
 *   1. timer = frame_timer_hw_create(timeout_timer_callback, NULL, 10000, 0);
 *   2. pkt   = packetizer_timeout_create(timer, my_callback);
 *   3. UART ISR 中: packetizer_put_byte(pkt, byte);
 *   4. 超时后 my_callback(frame, len) 自动触发
 */

#ifndef PACKETIZER_H
#define PACKETIZER_H

#include <stdint.h>

/* ---- 缓冲区大小 ---- */
#define RX_PACKET_BUF_SIZE  256

/* ---- 不透明类型 ---- */
typedef struct packetizer packetizer_t;
typedef struct frame_timer frame_timer_t;

/* ---- 帧完成回调 ---- */
typedef void (*frame_finish_callback)(uint8_t *frame, uint16_t len);

/* ---- 定时器回调 ---- */
typedef void (*timer_callback)(void *ctx);

/* ============================================================
 *  封包器基类 API
 * ============================================================ */
void    packetizer_init(packetizer_t *pkt);
void    packetizer_reset(packetizer_t *pkt);
uint8_t packetizer_put_byte(packetizer_t *pkt, uint8_t byte);
void    set_frame_finish_callback(packetizer_t *pkt, frame_finish_callback cb);

/* ============================================================
 *  超时封包器工厂
 * ============================================================ */
packetizer_t* packetizer_timeout_create(frame_timer_t *timer, frame_finish_callback cb);
void          packetizer_timeout_destroy(packetizer_t *pkt);
void          timeout_timer_callback(void *ctx);

/* ============================================================
 *  硬件定时器工厂
 * ============================================================ */
frame_timer_t* frame_timer_hw_create(timer_callback cb, void *ctx,
                                     uint16_t timeout_us, uint8_t hw_id);
void           frame_timer_hw_destroy(frame_timer_t *t);
void           frame_timer_hw_isr(uint8_t hw_id);

#endif
