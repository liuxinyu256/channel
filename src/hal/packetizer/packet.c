/********************************** (C) COPYRIGHT *******************************
 * File Name          : packet.c
 * Description        : 接收封包器基类实现
 *******************************************************************************/

#include "packet.h"

/* ============================================================
 *  ring_t 实现 —— 标准环形缓冲区（RX/TX 通用）
 * ============================================================ */

void ring_init(ring_t *r)
{
    if (r == NULL) return;
    r->wridx = 0;
    r->rdidx = 0;
}

void ring_reset(ring_t *r)
{
    if (r == NULL) return;
    r->rdidx = r->wridx;
}

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

/* ---- 生产者 ---- */

uint8_t ring_put(ring_t *r, uint8_t byte)
{
    if (r == NULL || ring_full(r)) return 1;
    r->buf[r->wridx] = byte;
    r->wridx = (r->wridx + 1) & PKT_BUF_MASK;
    return 0;
}

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

/* ---- 消费者（推进 rd）---- */

uint8_t ring_get(ring_t *r, uint8_t *out)
{
    if (r == NULL || out == NULL || ring_empty(r)) return 1;
    *out = r->buf[r->rdidx];
    r->rdidx = (r->rdidx + 1) & PKT_BUF_MASK;
    return 0;
}

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

/* ---- 消费者（不推进 rd）---- */

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

uint8_t ring_peek_at(const ring_t *r, uint16_t offset)
{
    if (r == NULL) return 0;
    return r->buf[(r->rdidx + offset) & PKT_BUF_MASK];
}

void ring_skip(ring_t *r, uint16_t n)
{
    if (r == NULL) return;
    r->rdidx = (r->rdidx + n) & PKT_BUF_MASK;
}

void ring_commit(ring_t *r)
{
    if (r == NULL) return;
    r->rdidx = r->wridx;
}

/* ============================================================
 *  packetizer 基类实现
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

uint8_t packetizer_push_frame(packetizer_t *pkt)
{
    if (pkt == NULL) return 0;

    static uint8_t tmp[PKT_BUF_SIZE];
    uint16_t n = ring_read(&pkt->ring, tmp, PKT_BUF_SIZE);
    if (n == 0) return 0;

    if (n > RX_PACKET_BUF_SIZE) n = RX_PACKET_BUF_SIZE;


    if (pkt->on_frame_finish) {
        pkt->on_frame_finish(tmp, n);
    }
    return 1;
}
