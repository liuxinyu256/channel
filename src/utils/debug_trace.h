/**
 * debug_trace.h —— 架构级调试日志系统
 *
 * 设计:
 *   - 环形缓冲记录事件，不阻塞
 *   - 按模块过滤 (PKT/TIMER/FRAME/BIND)
 *   - 事件带时间戳和关键数据
 *   - #define DEBUG_ENABLED 启用，#undef 全部消失
 *
 * 用法示例:
 *   TRACE_PKT(TRACE_EVT_BYTE_IN, byte, pkt->Rxidx);
 *   TRACE_TIMER(TRACE_EVT_TIMER_START, 0, hw_id);
 */

#ifndef DEBUG_TRACE_H
#define DEBUG_TRACE_H

#include <stdint.h>

/* ---- 按需开启 ---- */
// #define DEBUG_ENABLED

/* ============================================================
 *  事件类型 —— 覆盖整个收帧链路
 * ============================================================ */
typedef enum {
    TRACE_EVT_BYTE_IN,       /* 收到一个字节         */
    TRACE_EVT_TIMER_START,   /* 定时器启动           */
    TRACE_EVT_TIMER_RESTART, /* 定时器清零重启        */
    TRACE_EVT_TIMER_STOP,    /* 定时器停止           */
    TRACE_EVT_TIMER_TICK,    /* 定时器 tick          */
    TRACE_EVT_TIMEOUT,       /* 超时触发              */
    TRACE_EVT_FRAME_DONE,    /* 帧完成                */
    TRACE_EVT_RESET,         /* 封包器重置            */
    TRACE_EVT_BIND,          /* 创建/绑定             */
    TRACE_EVT_UNBIND,        /* 销毁/解绑             */
    TRACE_EVT_OVERFLOW,      /* 缓冲区溢出            */
} trace_event_t;

/* ============================================================
 *  环形缓冲区
 * ============================================================ */
#ifdef DEBUG_ENABLED

#define TRACE_BUF_SIZE 256

typedef struct {
    uint32_t      tick;
    trace_event_t event;
    uint8_t       data;
    uint16_t      aux;
} trace_entry_t;

typedef struct {
    trace_entry_t buf[TRACE_BUF_SIZE];
    uint16_t      head;
    uint32_t      count;
    uint32_t      overflow;
} trace_ring_t;

extern trace_ring_t g_trace;

void trace_ring_put(trace_ring_t *r, trace_event_t evt,
                    uint8_t data, uint16_t aux, uint32_t tick);
void trace_ring_dump(trace_ring_t *r);
int  trace_module_enabled(const char *mod);

#define TRACE_PKT(evt, d, a, t)    do { if (trace_module_enabled("PKT"))    trace_ring_put(&g_trace, evt, d, a, t); } while(0)
#define TRACE_TIMER(evt, d, a, t)  do { if (trace_module_enabled("TIMER"))  trace_ring_put(&g_trace, evt, d, a, t); } while(0)
#define TRACE_FRAME(evt, d, a, t)  do { if (trace_module_enabled("FRAME"))  trace_ring_put(&g_trace, evt, d, a, t); } while(0)
#define TRACE_BIND(evt, d, a, t)   do { if (trace_module_enabled("BIND"))   trace_ring_put(&g_trace, evt, d, a, t); } while(0)

#else

#define TRACE_PKT(evt, d, a, t)    ((void)0)
#define TRACE_TIMER(evt, d, a, t)  ((void)0)
#define TRACE_FRAME(evt, d, a, t)  ((void)0)
#define TRACE_BIND(evt, d, a, t)   ((void)0)

#endif

uint32_t debug_tick_get(void);

#endif
