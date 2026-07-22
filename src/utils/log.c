/**
 * log.c —— 运行时日志系统实现
 */
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* ---- 内部 ---- */
typedef struct {
    const char *name;
    uint8_t     enabled;
} log_module_t;

static log_level_t   g_level  = LOG_INFO;
static log_output_fn g_output = NULL;           /* NULL = printf */
static log_module_t  g_modules[LOG_MODULE_MAX];
static int           g_mod_cnt = 0;

/* ---- 环形缓冲 (ISR 安全模式) ---- */
#define RING_SZ  128

static char    g_ring[RING_SZ][128];
static uint8_t g_head, g_tail, g_over;
static int     g_ring_on;   /* 0=直接输出, 1=环形缓冲 */

/* ---- 模块管理 ---- */
int log_module_register(const char *name) {
    if (g_mod_cnt >= LOG_MODULE_MAX) return -1;
    for (int i = 0; i < g_mod_cnt; i++) {
        if (strcmp(g_modules[i].name, name) == 0) return i;
    }
    int idx = g_mod_cnt++;
    g_modules[idx].name    = name;
    g_modules[idx].enabled = 1;
    return idx;
}

void log_module_enable(const char *name, int on) {
    for (int i = 0; i < g_mod_cnt; i++) {
        if (strcmp(g_modules[i].name, name) == 0) {
            g_modules[i].enabled = (uint8_t)(on ? 1 : 0);
            return;
        }
    }
}

static int mod_on(const char *mod) {
    for (int i = 0; i < g_mod_cnt; i++) {
        if (strcmp(g_modules[i].name, mod) == 0) return g_modules[i].enabled;
    }
    return 1;  /* 未注册默认允许 */
}

/* ---- 等级名称 ---- */
static const char *lv_name(log_level_t lv) {
    switch (lv) {
        case LOG_ERROR: return "ERR";
        case LOG_WARN:  return "WRN";
        case LOG_INFO:  return "INF";
        case LOG_DEBUG: return "DBG";
        default:        return "???";
    }
}

/* ---- 核心 ---- */
void log_set_level(log_level_t lv) { g_level = lv; }
void log_set_output(log_output_fn fn) { g_output = fn; }
void log_ring_mode(int on) { g_ring_on = on; }

void log_write(log_level_t lv, const char *mod, const char *fmt, ...) {
    if (lv > g_level || g_level == LOG_OFF) return;
    if (!mod_on(mod)) return;

    char line[128];
    int pos = snprintf(line, sizeof(line), "[%s] %s: ", lv_name(lv), mod);

    va_list args;
    va_start(args, fmt);
    pos += vsnprintf(line + pos, sizeof(line) - pos, fmt, args);
    va_end(args);
    if (pos >= (int)sizeof(line)) pos = (int)sizeof(line) - 2;
    line[pos++] = '\n';
    line[pos]   = '\0';

    if (g_ring_on) {
        uint8_t next = (g_head + 1) % RING_SZ;
        if (next == g_tail) { g_over++; return; }
        strncpy(g_ring[g_head], line, sizeof(g_ring[0]) - 1);
        g_head = next;
    } else {
        if (g_output) g_output(line);
        else printf("%s", line);
    }
}

void log_flush(void) {
    while (g_tail != g_head) {
        if (g_output) g_output(g_ring[g_tail]);
        else printf("%s", g_ring[g_tail]);
        g_tail = (g_tail + 1) % RING_SZ;
    }
}
