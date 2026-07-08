/**
 * log_demo.c —— 日志系统快速验证
 *   gcc -Isrc test/log_demo.c src/log.c -o build/log_demo && ./build/log_demo
 */
#include "log.h"
#include <stdio.h>

int main(void) {
    printf("=== 日志系统验证 ===\n\n");

    log_module_register("pkt");
    log_module_register("timer");
    log_module_register("uart");

    printf("--- 默认 (INFO+) ---\n");
    LOG_DEBUG("pkt",   "这条不应该出现");
    LOG_INFO("pkt",    "put_byte: 0xAA idx=%u", 0);
    LOG_WARN("timer",  "counter=%u near threshold", 9);
    LOG_ERROR("uart",  "framing error");

    printf("\n--- 开 DEBUG ---\n");
    log_set_level(LOG_DEBUG);
    LOG_DEBUG("pkt",   "on_byte: timer_running=%d", 1);
    LOG_INFO("pkt",    "frame done: len=%u", 16);

    printf("\n--- 关 pkt 模块 ---\n");
    log_module_enable("pkt", 0);
    LOG_INFO("pkt",    "这条不应该出现");
    LOG_INFO("timer",  "timer started, hw_id=%d", 0);

    printf("\n--- 全关 ---\n");
    log_set_level(LOG_OFF);
    LOG_ERROR("pkt",   "这条也不应该出现");

    printf("\n[DONE]\n");
    return 0;
}
