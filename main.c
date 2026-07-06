/**
 * main.c —— 框架集成示例 & 编译入口
 *
 * 嵌入式目标上将此文件替换为固件主循环，本文件仅用于：
 *   1. 验证编译通过
 *   2. 演示 API 调用链路
 */

#include <stdio.h>
#include "packetizer.h"

/* 帧到回调（上层实现） */
static void my_frame_handler(uint8_t *frame, uint16_t len) {
    (void)frame;
    (void)len;
}

int main(void) {
    printf("=== Packetizer 封包器框架使用示例 ===\n\n");

    /* 1. 创建定时器（ctx 先 NULL，packetizer_timeout_create 内部绑定） */
    frame_timer_t *timer = frame_timer_hw_create(
        timeout_timer_callback,
        NULL,
        10000,
        0
    );
    if (timer == NULL) {
        printf("[FAIL] 定时器创建失败\n");
        return 1;
    }
    printf("[OK] 定时器创建成功 (hw_id=0, timeout=10ms)\n");

    /* 2. 创建超时封包器（内部自动：timer->ctx=pkt; pkt->on_frame_finish=cb） */
    packetizer_t *pkt = packetizer_timeout_create(timer, my_frame_handler);
    if (pkt == NULL) {
        printf("[FAIL] 封包器创建失败\n");
        frame_timer_hw_destroy(timer);
        return 1;
    }
    printf("[OK] 超时封包器创建成功（timer↔pkt 已绑定，回调已注册）\n");

    /* 3. 模拟接收字节（硬件串口 ISR 中调用 put_byte） */
    printf("\n--- 模拟接收 ---\n");
    uint8_t test_data[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    for (int i = 0; i < 4; i++) {
        uint8_t ret = packetizer_put_byte(pkt, test_data[i]);
        printf("  put_byte(0x%02X) -> %s\n", test_data[i], ret == 0 ? "OK" : "FAIL");
    }

    /* 4. 清理 */
    packetizer_timeout_destroy(pkt);
    frame_timer_hw_destroy(timer);
    printf("\n[DONE] 资源已释放\n");

    return 0;
}
