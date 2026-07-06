#ifndef PACKET_H
#define PACKET_H

/**
 * 接收封包器 —— 将字节流组装为完整帧，通过回调推送给上层
 *
 * 数据流（上层视角）：
 *   ┌─────────┐    put_byte()     ┌──────────────┐
 *   │ 串口 ISR │ ──────────────→  │  packetizer  │
 *   └─────────┘                   │  (策略+缓冲)  │
 *                                 └──────┬───────┘
 *                                        │ 帧完成时触发
 *                                        ↓
 *                                 on_frame_finish(frame, len)
 *                                        │
 *                                        ↓
 *                                 ┌──────────────┐
 *                                 │ 上层(应用层)   │
 *                                 │ 拷贝/入队/标记 │
 *                                 └──────────────┘
 *
 * 使用步骤：
 *   1. pkt = packetizer_timeout_create(timeout, timer)   — 创建实例
 *   2. set_frame_finish_callback(pkt, my_callback)       — 注册帧到回调
 *   3. 串口 ISR 中调用 packetizer_put_byte(pkt, byte)    — 喂字节
 *   4. 帧完成后 my_callback(frame, len) 自动触发          — 收帧
 *
 * 回调在定时器 ISR 上下文中执行，上层可在回调中直接拷贝数据。
 */

#include <stdlib.h>
#include <stdint.h>

/* ---- 接收缓冲区大小（Modbus RTU 最大 256 字节） ---- */
#define RX_PACKET_BUF_SIZE  256

/* ---- 封包策略枚举（扩展新策略时在此追加） ---- */
typedef enum {
    packetizer_type_Timeout,    /* 超时封包策略 */
} packetizer_type;

/* ---- 帧完成回调：frame=数据指针, len=帧字节数 ---- */
typedef void (*frame_finish_callback)(uint8_t *frame, uint16_t len);

typedef struct packetizer packetizer_t;
typedef struct frame_timer frame_timer_t;  /* 前置声明，供封包器注入定时器 */

/**
 * packet_ops_t — 策略操作接口
 *
 * 由各封包策略实现（如超时策略），仅基类 wrapper 内部调用。
 * 上层不直接访问 ops —— 请用 packetizer_init / reset / put_byte 等公开 API。
 */
typedef struct {
    void (*init)   (packetizer_t *pkt);  /* 初始化封包器 */
    void (*reset)  (packetizer_t *pkt);  /* 重置封包器   */
    void (*on_byte)(packetizer_t *pkt);  /* 收到一个字节，策略更新状态机 */
} packet_ops_t;

/**
 * packetizer 封包器基类
 *
 * 字段对上层透明，通过公开 API 操作。
 * Rxbuf 是线性缓冲区，帧到后通过 on_frame_finish 回调推送，
 * 推送完自动清零，下一帧从头写入。
 */
struct packetizer {
    const packet_ops_t      *ops;            /* 策略操作（基类内部调用）     */
    packetizer_type          type;           /* 封包策略类型                */
    uint8_t                  Rxbuf[RX_PACKET_BUF_SIZE]; /* 接收缓存        */
    uint16_t                 Rxidx;          /* 缓存写入位置 / 当前帧长度   */
    frame_finish_callback    on_frame_finish;/* 帧完成回调（ISR 中触发）    */
};

/* ============================================================
 *  公开 API（上层通过以下函数操作封包器，不直接访问 struct 成员）
 * ============================================================ */

/* 初始化 / 重置封包器（内部调用策略的 init / reset） */
void     packetizer_init(packetizer_t *pkt);
void     packetizer_reset(packetizer_t *pkt);

/* 向封包器喂入一个字节，串口 ISR 中调用。返回 0=成功，1=溢出 */
uint8_t  packetizer_put_byte(packetizer_t *pkt, uint8_t byte);

/* 注册帧完成回调
 * cb 参数为 (frame, len)，frame 指向内部缓冲区，len 为帧长度。
 * 回调在定时器 ISR 中执行，上层应尽快拷贝数据后返回。
 */
void     set_frame_finish_callback(packetizer_t *pkt, frame_finish_callback cb);

#endif
