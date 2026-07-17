#include "packet.h"

/* ============================================================
 *  ring_t 实现 —— 标准环形缓冲区（RX/TX 通用）
 *
 *  布局: buf[PKT_BUF_SIZE], 2 的幂，掩码定位
 *  指针: wridx(生产者写)  rdidx(消费者读)
 *  判空: rdidx == wridx
 *  判满: (wridx + 1) & MASK == rdidx  (留 1 槽防混淆)
 *  可用: (wridx - rdidx) & MASK
 *
 *  RX 用法见 packetizer_push_frame 注释
 * ============================================================ */

void ring_init(ring_t *r)
{
    if (r == NULL) return;
    r->wridx = 0;
    r->rdidx = 0;
}

/* 丢弃全部未读数据 */
void ring_reset(ring_t *r)
{
    if (r == NULL) return;
    r->rdidx = r->wridx;
}

/* ---- 状态查询 ---- */

uint8_t ring_empty(const ring_t *r)
{
    if (r == NULL) return 1;
    return r->rdidx == r->wridx;
}

uint8_t ring_full(const ring_t *r)
{
    if (r == NULL) return 0;
    return ((r->wridx + 1) & PKT_BUF_MASK) == r->rdidx;
}

uint16_t ring_count(const ring_t *r)
{
    if (r == NULL) return 0;
    return (r->wridx - r->rdidx) & PKT_BUF_MASK;
}

/* ---- 生产者 API（写端，推进 wridx）---- */

/* 写入 1 字节，满返回 1 */
uint8_t ring_put(ring_t *r, uint8_t byte)
{
    if (r == NULL || ring_full(r)) return 1;
    r->buf[r->wridx] = byte;
    r->wridx = (r->wridx + 1) & PKT_BUF_MASK;
    return 0;
}

/* 批量写入，返回实际写入字节数（满时停止） */
uint16_t ring_write(ring_t *r, const uint8_t *src, uint16_t len)
{
    if (r == NULL || src == NULL || len == 0) return 0;
    uint16_t wrote = 0;
    for (uint16_t i = 0; i < len; i++) {
        if (ring_put(r, src[i])) break;
        wrote++;
    }
    return wrote;
}

/* ---- 消费者 API（读端，推进 rdidx）---- */

/* 读出 1 字节，空返回 1 */
uint8_t ring_get(ring_t *r, uint8_t *out)
{
    if (r == NULL || out == NULL || ring_empty(r)) return 1;
    *out = r->buf[r->rdidx];
    r->rdidx = (r->rdidx + 1) & PKT_BUF_MASK;
    return 0;
}

/* 批量读出 max 字节到 dst，返回实际读出数，推进 rdidx。
   自动处理回绕，dst 必须足够大。 */
uint16_t ring_read(ring_t *r, uint8_t *dst, uint16_t max)
{
    if (r == NULL || dst == NULL || max == 0) return 0;
    uint16_t n = ring_count(r);
    if (n > max) n = max;
    if (n == 0) return 0;

    uint16_t start = r->rdidx;
    if (start + n <= PKT_BUF_SIZE) {
        for (uint16_t i = 0; i < n; i++) dst[i] = r->buf[start + i];
    } else {
        uint16_t first = PKT_BUF_SIZE - start;
        for (uint16_t i = 0; i < first; i++) dst[i] = r->buf[start + i];
        for (uint16_t i = 0; i < n - first; i++) dst[first + i] = r->buf[i];
    }
    r->rdidx = (r->rdidx + n) & PKT_BUF_MASK;
    return n;
}

/* ---- 消费者 API（窥探，不推进 rdidx）---- */

/* 批量读出但不推进 rdidx，用于 RX 帧交付（回调后 commit） */
uint16_t ring_peek(ring_t *r, uint8_t *dst, uint16_t max)
{
    if (r == NULL || dst == NULL || max == 0) return 0;
    uint16_t n = ring_count(r);
    if (n > max) n = max;
    if (n == 0) return 0;

    uint16_t start = r->rdidx;
    if (start + n <= PKT_BUF_SIZE) {
        for (uint16_t i = 0; i < n; i++) dst[i] = r->buf[start + i];
    } else {
        uint16_t first = PKT_BUF_SIZE - start;
        for (uint16_t i = 0; i < first; i++) dst[i] = r->buf[start + i];
        for (uint16_t i = 0; i < n - first; i++) dst[first + i] = r->buf[i];
    }
    return n;
}

/* 窥探 offset 偏移处的单字节（调试用），offset 从 rdidx 起算，自动回绕 */
uint8_t ring_peek_at(const ring_t *r, uint16_t offset)
{
    if (r == NULL) return 0;
    return r->buf[(r->rdidx + offset) & PKT_BUF_MASK];
}

/* 跳过 n 字节（推进 rdidx 但不拷出） */
void ring_skip(ring_t *r, uint16_t n)
{
    if (r == NULL) return;
    r->rdidx = (r->rdidx + n) & PKT_BUF_MASK;
}

/* 提交（rdidx = wridx），RX 帧交付完成后调用，释放缓冲空间 */
void ring_commit(ring_t *r)
{
    if (r == NULL) return;
    r->rdidx = r->wridx;
}

/* ============================================================
 *  packetizer 基类实现
 *
 *  数据流:
 *    ISR: put_byte → ring_put (写端)
 *    策略判定帧完成 → push_frame:
 *      ring_read → 截断 → 回调交付
 * ============================================================ */

void packetizer_init(packetizer_t *pkt)
{
    if (pkt == NULL || pkt->ops == NULL) return;
    pkt->ops->init(pkt);
}

void packetizer_reset(packetizer_t *pkt)
{
    if (pkt == NULL || pkt->ops == NULL) return;
    pkt->ops->reset(pkt);
}

/* ISR 中调用：先写 ring，再通知策略更新状态机。满时返回 1 */
uint8_t packetizer_put_byte(packetizer_t *pkt, uint8_t byte)
{
    if (pkt == NULL || pkt->ops == NULL) return 1;
    if (ring_put(&pkt->ring, byte)) return 1;
    if (pkt->ops->on_byte) pkt->ops->on_byte(pkt);
    return 0;
}

void set_frame_finish_callback(packetizer_t *pkt, frame_finish_callback cb)
{
    if (pkt == NULL) return;
    pkt->on_frame_finish = cb;
}

/* 策略帧完成时调用:
 *   1. ring_read 读出帧数据并释放 ring 空间
 *   2. 截断超长帧（> RX_PACKET_BUF_SIZE）
 *   3. 回调向上层交付帧数据（ISR 上下文）
 *
 * tmp 用静态缓冲（ISR 单线程无重入），不占 ISR 栈。 */
uint8_t packetizer_push_frame(packetizer_t *pkt)
{
    static uint8_t tmp[PKT_BUF_SIZE];
    uint16_t n;

    if (pkt == NULL) return 0;

    n = ring_read(&pkt->ring, tmp, PKT_BUF_SIZE);
    if (n == 0) return 0;

    if (n > RX_PACKET_BUF_SIZE) n = RX_PACKET_BUF_SIZE;

    if (pkt->on_frame_finish) {
        pkt->on_frame_finish(tmp, n);
    }
    return 1;
}
