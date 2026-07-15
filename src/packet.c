#include "packet.h"

void packetizer_init(packetizer_t *pkt)
{
    if (pkt == NULL || pkt->ops == NULL) {
        return;
    }
    pkt->ops->init(pkt);
}

void packetizer_reset(packetizer_t *pkt)
{
    if (pkt == NULL || pkt->ops == NULL) {
        return;
    }
    pkt->ops->reset(pkt);
}

//将一个字节放入packet中，返回0表示成功，返回1表示失败（如溢出）
uint8_t packetizer_put_byte(packetizer_t *pkt, uint8_t byte)
{
    if (pkt == NULL || pkt->ops == NULL) {
        return 1;  /* 失败：未初始化 */
    }
    if (pkt->Rxidx >= RX_PACKET_BUF_SIZE) {
        return 1;  /* 失败：缓冲区溢出 */
    }
    pkt->Rxbuf[pkt->Rxidx++] = byte;

    /* 有新字节到达，策略自行维护封包状态机 */
    if (pkt->ops->on_byte) {
        pkt->ops->on_byte(pkt);
    }
    return 0;  /* 成功 */
}

/**
 * 设置帧完成回调函数
 * @param pkt packetizer实例
 * @param cb 回调函数
 */
void  set_frame_finish_callback(packetizer_t *pkt, frame_finish_callback cb)
{
    if (pkt == NULL) return;
    pkt->on_frame_finish = cb;    /* NULL = 清回调 */
}

uint8_t packetizer_push_frame(packetizer_t *pkt, uint8_t *d, uint16_t n)
{
    if (pkt == NULL || d == NULL || n == 0) return 0;
    uint8_t next = (pkt->ring.wr + 1) & PKT_RING_MASK;
    if (next == pkt->ring.rd) { pkt->ring.drops++; return 0; }  /* 满 */
    pkt_frame_slot_t *s = &pkt->ring.slots[pkt->ring.wr];
    s->len = n;
    {uint16_t i; for(i = 0; i < n; i++) s->data[i] = d[i];}
    pkt->ring.wr = next;
    packetizer_reset(pkt);  /* 释放 Rxbuf */
    return 1;
}

uint8_t packetizer_drops(packetizer_t *pkt)
{
    if (pkt == NULL) return 0;
    return pkt->ring.drops;
}

uint16_t packetizer_get_frame(packetizer_t *pkt, uint8_t *buf)
{
    if (pkt == NULL || buf == NULL) return 0;
    if (pkt->ring.rd == pkt->ring.wr) return 0;  /* 空 */
    pkt_frame_slot_t *s = &pkt->ring.slots[pkt->ring.rd];
    {uint16_t i; for(i = 0; i < s->len; i++) buf[i] = s->data[i];}
    pkt->ring.rd = (pkt->ring.rd + 1) & PKT_RING_MASK;
    return s->len;
}