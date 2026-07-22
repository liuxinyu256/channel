/**
 * log.h —— 运行时日志系统
 *
 * 设计:
 *   1. 四级: DEBUG/INFO/WARN/ERROR/OFF
 *   2. 模块独立开关，运行时可控
 *   3. 输出函数可替换 (默认 printf，嵌入式换 UART)
 *   4. ISR 安全: 可选环形缓冲 + 主循环消费
 */

#ifndef LOG_H
#define LOG_H

#include <stdint.h>

/* ============================================================
 *  日志等级
 * ============================================================ */
typedef enum {
    LOG_OFF   = 0,
    LOG_ERROR = 1,
    LOG_WARN  = 2,
    LOG_INFO  = 3,
    LOG_DEBUG = 4,
} log_level_t;

/* ============================================================
 *  模块注册 (最多 16 个模块)
 * ============================================================ */
#define LOG_MODULE_MAX  16

/* 注册一个模块 (通常在模块 init 时调用) */
int  log_module_register(const char *name);

/* 运行时开关: log_module_enable("pkt", 0) 关闭封包器日志 */
void log_module_enable(const char *name, int on);

/* ============================================================
 *  输出函数
 * ============================================================ */
typedef void (*log_output_fn)(const char *str);

/* 设置输出函数: log_set_output(uart_putstring); */
void log_set_output(log_output_fn fn);

/* ============================================================
 *  全局控制
 * ============================================================ */
void log_set_level(log_level_t lv);
void log_ring_mode(int on);  /* 1=环形缓冲(ISR安全), 0=直接输出 */
void log_flush(void);        /* 环形缓冲 → 输出 */

/* ============================================================
 *  日志宏 —— 各模块用这些埋点
 * ============================================================ */
#define LOG_ERROR(mod, fmt, ...)  log_write(LOG_ERROR, mod, fmt, ##__VA_ARGS__)
#define LOG_WARN(mod,  fmt, ...)  log_write(LOG_WARN,  mod, fmt, ##__VA_ARGS__)
#define LOG_INFO(mod,  fmt, ...)  log_write(LOG_INFO,  mod, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(mod, fmt, ...)  log_write(LOG_DEBUG, mod, fmt, ##__VA_ARGS__)

/* 内部 */
void log_write(log_level_t lv, const char *mod, const char *fmt, ...);

#endif
