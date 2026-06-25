
#include "packet.h"

uint8_t packetizer_put_byte(packetizer_t *pkt, uint8_t byte)
{
    if (pkt == NULL || pkt->strategy == NULL) {
        return 1;  /* 失败：未初始化 */
    }
    if (pkt->Rxidx >= RX_PACKET_BUF_SIZE) {
        return 1;  /* 失败：缓冲区溢出 */
    }
    pkt->Rxbuf[pkt->Rxidx++] = byte;

    /* 通知策略层：有新字节到达，策略自行维护封包状态机 */
    if (pkt->strategy->on_byte) {
        pkt->strategy->on_byte(pkt);
    }
    return 0;  /* 成功 */
}
