#ifndef PACKET_H
#define PACKET_H

/**
 * 接收封包器 —— 将字节流组装为完整帧，通过回调推送给上层
 *
 * 数据流：
 *   ┌─────────┐    put_byte()     ┌──────────────┐
 *   │ 串口 ISR │ ──────────────→  │  packetizer  │
 *   └─────────┘                   │  (策略+缓冲)  │
 *                                 └──────┬───────┘
 *                                        │ 帧完成
 *                                        ↓
 *                                 on_frame_finish(frame, len)
 *
 * 回调在定时器 ISR 上下文中执行，上层应尽快完成拷贝。
 */

#include <stdint.h>

/* ---- 缓冲区大小 ---- */
#define RX_PACKET_BUF_SIZE      256   /* 最大帧长 */
#define PKT_BUF_SIZE            512   /* 环形缓冲(2的幂) */
#define PKT_BUF_MASK            (PKT_BUF_SIZE - 1)

/* ============================================================
 *  ring_t — 标准环形缓冲区（RX/TX 通用）
 *
 * 生产者写 wridx，消费者读 rdidx。
 *   - 空: rdidx == wridx
 *   - 满: (wridx + 1) & MASK == rdidx  (留 1 槽区分空/满)
 *   - 可用: (wridx - rdidx) & MASK
 *
 * RX 用法: ISR ring_put → 帧完成 ring_peek → 回调 → ring_commit
 * TX 用法: 任务 ring_write → ISR ring_get
 * ============================================================ */

typedef struct {
    uint8_t  buf[PKT_BUF_SIZE];
    uint16_t wridx;   /* 生产者写入位置 */
    uint16_t rdidx;   /* 消费者读取位置 */
} ring_t;

/* 初始化 / 重置 */
void     ring_init(ring_t *r);
void     ring_reset(ring_t *r);              /* rd = wr，丢弃全部 */

/* 状态查询 */
uint8_t  ring_empty(const ring_t *r);
uint8_t  ring_full(const ring_t *r);
uint16_t ring_count(const ring_t *r);

/* 生产者: 写字节 */
uint8_t  ring_put(ring_t *r, uint8_t byte);                      /* 1 字节 */
uint16_t ring_write(ring_t *r, const uint8_t *src, uint16_t len); /* 批量  */

/* 消费者: 读字节（推进 rd） */
uint8_t  ring_get(ring_t *r, uint8_t *out);                       /* 1 字节 */
uint16_t ring_read(ring_t *r, uint8_t *dst, uint16_t max);        /* 批量  */

/* 消费者: 窥探不推进 rd */
uint8_t  ring_peek_at(const ring_t *r, uint16_t offset);
uint16_t ring_peek(ring_t *r, uint8_t *dst, uint16_t max);

/* 消费者: 推进 rd */
void     ring_skip(ring_t *r, uint16_t n);
void     ring_commit(ring_t *r);             /* rd = wr */

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
    ring_t                   ring;
    frame_finish_callback    on_frame_finish;
};

/* ============================================================
 *  公开 API
 * ============================================================ */

void     packetizer_init(packetizer_t *pkt);
void     packetizer_reset(packetizer_t *pkt);

/* ISR 中调用，喂入一个字节。返回 0=成功 1=溢出 */
uint8_t  packetizer_put_byte(packetizer_t *pkt, uint8_t byte);

/* 注册帧完成回调 */
void     set_frame_finish_callback(packetizer_t *pkt, frame_finish_callback cb);

/* 帧完成时由策略调用：ring→tmp→回调交付→commit */
uint8_t  packetizer_push_frame(packetizer_t *pkt);

#endif
