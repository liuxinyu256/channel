
#include "packet.h"
uint8_t packetizer_put_byte(packetizer_t *pkt, uint8_t byte)
{
    if (pkt == NULL || pkt->ops == NULL) {
        return 0;
    }
    if (pkt->Rxidx >= RX_PACKET_BUF_SIZE) {
        return 0;
    }
    pkt->Rxbuf[pkt->Rxidx++] = byte;
    return 1;
}
