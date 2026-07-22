/********************************** (C) COPYRIGHT *******************************
 * File Name          : packet.h
 * Description        : 接收封包器基类 —— 将字节流组装为完整帧，通过回调推送
 *
 * 单层 ring 架构：
 *   ISR 逐字节写入 → 策略判定帧完成 → 回调直接交付帧数据
 *
 * 使用步骤：
 *   1. packetizer_timeout_create(timer, ticks, my_callback)
 *   2. UART ISR 中调用 packetizer_put_byte(pkt, byte)
 *   3. 帧完成后 my_callback(frame, len) 自动触发
 *
 * 回调在定时器 ISR 上下文中执行，上层应尽快完成拷贝。
 *******************************************************************************/

#ifndef PACKET_H
#define PACKET_H

#include "CH57x_common.h"

/* ---- 缓冲区大小 ---- */
#define RX_PACKET_BUF_SIZE      256   /* 最大帧长 */
#define PKT_BUF_SIZE            512   /* 字节环形缓冲(2的幂) */
#define PKT_BUF_MASK            (PKT_BUF_SIZE - 1)

/* ---- ring — 标准环形缓冲区（ISR 写，帧完线性化读出）---- */
typedef struct {
    uint8_t  buf[PKT_BUF_SIZE];
    uint16_t wridx;
    uint16_t rdidx;
} ring_t;

void     ring_init(ring_t *r);
void     ring_reset(ring_t *r);
uint8_t  ring_empty(const ring_t *r);
uint8_t  ring_full(const ring_t *r);
uint16_t ring_count(const ring_t *r);
uint8_t  ring_put(ring_t *r, uint8_t byte);
uint16_t ring_write(ring_t *r, const uint8_t *src, uint16_t len);
uint8_t  ring_get(ring_t *r, uint8_t *out);
uint16_t ring_read(ring_t *r, uint8_t *dst, uint16_t max);
uint16_t ring_peek(ring_t *r, uint8_t *dst, uint16_t max);
uint8_t  ring_peek_at(const ring_t *r, uint16_t offset);
void     ring_skip(ring_t *r, uint16_t n);
void     ring_commit(ring_t *r);

/* ---- 帧完成回调 ---- */
typedef void (*frame_finish_callback)(uint8_t *frame, uint16_t len);

typedef struct packetizer packetizer_t;
typedef struct frame_timer frame_timer_t;

/* ---- 策略虚表 ---- */
typedef struct {
    void (*init)   (packetizer_t *pkt);
    void (*reset)  (packetizer_t *pkt);
    void (*on_byte)(packetizer_t *pkt);
} packet_ops_t;

/* ---- 封包器基类 ---- */
struct packetizer {
    const packet_ops_t      *ops;
    ring_t              ring;
    frame_finish_callback    on_frame_finish;
};

/* ============================================================
 *  公开 API
 * ============================================================ */
void     packetizer_init(packetizer_t *pkt);
void     packetizer_reset(packetizer_t *pkt);
uint8_t  packetizer_put_byte(packetizer_t *pkt, uint8_t byte);
uint8_t  packetizer_push_frame(packetizer_t *pkt);
void     set_frame_finish_callback(packetizer_t *pkt, frame_finish_callback cb);

#endif
