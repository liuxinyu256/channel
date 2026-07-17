/**
 * @file sim_app.c
 * @brief 封包器交互式模拟器 —— 在 PC 上模拟 MCU UART 接收 + 硬件定时器
 *
 * 你可以像操作真实硬件一样：
 *   - 手动输入 hex 字节（模拟 UART ISR 逐字节到达）
 *   - 观察定时器计数、帧边界判定
 *   - 实时控制日志模块开关和等级
 *   - 调整字节间隔，模拟不同速率
 *
 * 编译（命令行）:
 *   gcc -std=c99 -Wall -Wextra -Isrc -I. \
 *       sim/sim_app.c src/packetizer_timeout.c src/packet.c \
 *       src/frame_timer_sw.c src/log.c \
 *       -o build/sim_app && build/sim_app
 *
 * 用法示例:
 *   > s AA BB CC DD     — 发送 4 个字节 (UART 模拟)
 *   > w 100             — 等待 100ms
 *   > g 5               — 设置字节间间隔为 5ms
 *   > l pkt 0           — 关闭 pkt 模块日志
 *   > v 4               — 日志等级设为 DEBUG
 *   > i                 — 显示当前状态
 *   > q                 — 退出
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "packetizer.h"
#include "packet.h"            /* struct packetizer 定义，供模拟器访问内部状态 */
#include "frame_timer_sw.h"
#include "log.h"

/* ============================================================
 *  模拟器配置
 * ============================================================ */
#define SIM_TICK_PERIOD_US   1000    /* 1ms per tick                  */
#define SIM_TIMEOUT_TICKS    10      /* 10ms 超时（= 10 ticks）       */
#define SIM_BYTE_GAP_MS      0       /* 默认字节间无间隔               */

/* ============================================================
 *  全局状态
 * ============================================================ */
static frame_timer_t *g_timer    = NULL;
static packetizer_t  *g_pkt      = NULL;
static int            g_byte_gap_ms = SIM_BYTE_GAP_MS;
static int            g_frame_count = 0;
static int            g_running     = 1;

/* ============================================================
 *  帧完成回调 —— 模拟器核心反馈
 * ============================================================ */
static void on_frame_ready(uint8_t *frame, uint16_t len) {
    g_frame_count++;

    uint64_t now_us = frame_timer_sw_now_us();
    uint64_t ms = now_us / 1000;
    uint64_t sec = ms / 1000;
    ms = ms % 1000;

    printf("\n");
    printf("  ╔══════════════════════════════════════╗\n");
    printf("  ║  *** FRAME #%d COMPLETE ***          ║\n", g_frame_count);
    printf("  ╠══════════════════════════════════════╣\n");
    printf("  ║  Time:  +%llu.%03llus                  ║\n",
           (unsigned long long)sec, (unsigned long long)ms);
    printf("  ║  Len:   %u bytes", len);
    for (int i = 0; i < 36 - (len >= 10 ? 2 : 1) - (len >= 100 ? 1 : 0); i++)
        putchar(' ');
    printf("║\n");

    /* hex dump */
    printf("  ║  Hex:  ");
    for (uint16_t i = 0; i < len && i < 32; i++) {
        printf("%02X ", frame[i]);
        if ((i + 1) % 16 == 0 && i + 1 < len && i + 1 < 32) {
            printf("\n  ║        ");
        }
    }
    if (len > 32) printf("... (+%u more)", len - 32);
    for (int i = 0; i < (32 - (len < 32 ? len : 32)) * 3 + 2; i++) putchar(' ');
    printf("║\n");

    /* ASCII 预览 */
    printf("  ║  ASCII: ");
    int printed = 0;
    for (uint16_t i = 0; i < len && i < 48; i++) {
        char c = (frame[i] >= 0x20 && frame[i] <= 0x7E) ? (char)frame[i] : '.';
        putchar(c);
        printed++;
    }
    if (len > 48) printf("...");
    for (int i = printed; i < 48; i++) putchar(' ');
    printf("║\n");

    printf("  ╚══════════════════════════════════════╝\n\n");
}

/* ============================================================
 *  状态显示
 * ============================================================ */
static void show_status(void) {
    frame_timer_sw_poll(g_timer);

    uint32_t counter = g_timer ? g_timer->counter : 0;
    uint16_t rxidx   = g_pkt   ? ring_count(&g_pkt->ring) : 0;

    printf("\n");
    printf("  ─── Status ───────────────────────────\n");
    printf("  Timer:       counter=%u/%u ticks (%ums timeout)\n",
           counter, (unsigned)SIM_TIMEOUT_TICKS,
           SIM_TIMEOUT_TICKS * SIM_TICK_PERIOD_US / 1000);
    printf("  Byte gap:    %d ms\n", g_byte_gap_ms);
    printf("  Buffer:      %u/%u bytes\n", rxidx, (unsigned)PKT_BUF_SIZE);
    if (rxidx > 0 && g_pkt) {
        printf("  Data:        ");
        for (uint16_t i = 0; i < rxidx && i < 32; i++) {
            printf("%02X ", ring_peek_at(&g_pkt->ring, i));
        }
        if (rxidx > 32) printf("...");
        printf("\n");
    }
    printf("  Frames:      %d captured\n", g_frame_count);
    printf("  Log level:   3 (INFO)\n");
    printf("  ──────────────────────────────────────\n\n");
}

/* ============================================================
 *  发送字节（模拟 UART ISR）
 * ============================================================ */
static void send_bytes(const uint8_t *bytes, int count) {
    if (g_pkt == NULL) {
        printf("  [ERR] No packetizer instance\n");
        return;
    }

    uint64_t start_us = frame_timer_sw_now_us();

    for (int i = 0; i < count; i++) {
        uint8_t ret = packetizer_put_byte(g_pkt, bytes[i]);
        if (ret != 0) {
            printf("  [WARN] Buffer overflow at byte %d (0x%02X)\n", i, bytes[i]);
            break;
        }

        frame_timer_sw_poll(g_timer);

        if (g_byte_gap_ms > 0 && i < count - 1) {
            frame_timer_sw_sleep_us((uint64_t)g_byte_gap_ms * 1000);
            frame_timer_sw_poll(g_timer);
        }
    }

    uint64_t elapsed_us = frame_timer_sw_now_us() - start_us;

    printf("  [TX] %d byte(s) in %llu us", count,
           (unsigned long long)elapsed_us);
    if (g_byte_gap_ms > 0) {
        printf(" (gap=%dms)", g_byte_gap_ms);
    }
    printf(" | timer counter=%u/%u\n",
           g_timer ? g_timer->counter : 0,
           (unsigned)SIM_TIMEOUT_TICKS);
}

/* ============================================================
 *  命令解析 —— hex 字符串
 * ============================================================ */
static int parse_hex_byte(const char *s, uint8_t *out) {
    if (strlen(s) != 2) return 0;
    if (!isxdigit((unsigned char)s[0])) return 0;
    if (!isxdigit((unsigned char)s[1])) return 0;

    unsigned int val;
    if (sscanf(s, "%2x", &val) != 1) return 0;
    *out = (uint8_t)val;
    return 1;
}

static int parse_hex_string(const char *input, uint8_t *buf, int max) {
    int count = 0;
    char token[8];
    const char *p = input;

    while (*p && count < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        int ti = 0;
        while (*p && *p != ' ' && *p != '\t' && ti < 7) {
            token[ti++] = *p++;
        }
        token[ti] = '\0';

        if (parse_hex_byte(token, &buf[count])) {
            count++;
        } else if (ti > 0) {
            printf("  [ERR] Invalid hex: '%s'\n", token);
            return -1;
        }
    }
    return count;
}

/* ============================================================
 *  帮助
 * ============================================================ */
static void show_help(void) {
    printf("\n");
    printf("  Commands:\n");
    printf("  ─────────────────────────────────────────────\n");
    printf("  s <hex>...     发送 hex 字节 (如: s AA BB CC)\n");
    printf("  w <ms>         等待 N 毫秒\n");
    printf("  g <ms>         设置字节间间隔 (0=无间隔)\n");
    printf("  r              重置封包器 (清缓冲)\n");
    printf("  i              显示当前状态\n");
    printf("  l <mod> <0|1>  日志模块开关 (如: l pkt 0)\n");
    printf("  v <0-4>        日志等级 (0=OFF 4=DEBUG)\n");
    printf("  h              显示此帮助\n");
    printf("  q              退出\n");
    printf("\n");
    printf("  Tips:\n");
    printf("  - 发送字节后不要操作，等 %dms 会自动超时成帧\n",
           SIM_TIMEOUT_TICKS * SIM_TICK_PERIOD_US / 1000);
    printf("  - 用 g 5 模拟低速 UART (5ms/byte)\n");
    printf("  - 用 g 0 模拟高速连续传输\n");
    printf("  - 在字节间隙中用 w 手动插入长间隔\n");
    printf("\n");
}

/* ============================================================
 *  主循环
 * ============================================================ */
int main(void) {
    printf("\n");
    printf("  ╔══════════════════════════════════════════════╗\n");
    printf("  ║     Packetizer Interactive Simulator v1.0    ║\n");
    printf("  ╠══════════════════════════════════════════════╣\n");
    printf("  ║  Tick:    %u us (= %ums)                      ║\n",
           (unsigned)SIM_TICK_PERIOD_US,
           (unsigned)SIM_TICK_PERIOD_US / 1000);
    printf("  ║  Timeout: %u ticks (= %ums)                   ║\n",
           (unsigned)SIM_TIMEOUT_TICKS,
           (unsigned)SIM_TIMEOUT_TICKS * SIM_TICK_PERIOD_US / 1000);
    printf("  ║  Type 'h' for help                          ║\n");
    printf("  ╚══════════════════════════════════════════════╝\n\n");

    /* ---- 创建软件定时器 ---- */
    g_timer = frame_timer_sw_create(SIM_TICK_PERIOD_US);
    if (g_timer == NULL) {
        printf("[FATAL] Failed to create software timer\n");
        return 1;
    }

    /* ---- 创建封包器（注入软件定时器） ---- */
    g_pkt = packetizer_timeout_create(g_timer, SIM_TIMEOUT_TICKS, on_frame_ready);
    if (g_pkt == NULL) {
        printf("[FATAL] Failed to create packetizer\n");
        frame_timer_sw_destroy(g_timer);
        return 1;
    }

    /* ---- 注册日志模块 ---- */
    log_module_register("pkt");
    log_module_register("timer");
    log_module_register("sim");

    printf("[OK] Timer + Packetizer ready.\n");
    printf("[OK] Timeout = %d ticks x %d us = %d ms\n\n",
           SIM_TIMEOUT_TICKS, SIM_TICK_PERIOD_US,
           SIM_TIMEOUT_TICKS * SIM_TICK_PERIOD_US / 1000);

    /* ============================================================
     *  主循环
     * ============================================================ */
    char   line[512];
    uint8_t hexbuf[256];

    while (g_running) {
        frame_timer_sw_poll(g_timer);

        printf("sim> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) break;

        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        char cmd = (char)tolower((unsigned char)line[0]);

        switch (cmd) {

        case 's': {
            const char *data = line + 1;
            while (*data == ' ' || *data == '\t') data++;

            int count = parse_hex_string(data, hexbuf, (int)sizeof(hexbuf));
            if (count < 0) break;
            if (count == 0) {
                printf("  Usage: s <hex bytes>  (e.g. s AA BB CC DD)\n");
                break;
            }
            send_bytes(hexbuf, count);
            break;
        }

        case 'w': {
            int ms = 0;
            if (sscanf(line + 1, "%d", &ms) == 1 && ms > 0) {
                printf("  Waiting %d ms...\n", ms);
                int steps = (ms > 100) ? (ms / 10) : ms;
                int step_ms = (ms > 100) ? 10 : 1;
                for (int i = 0; i < steps; i++) {
                    frame_timer_sw_sleep_us((uint64_t)step_ms * 1000);
                    frame_timer_sw_poll(g_timer);
                }
            } else {
                printf("  Usage: w <milliseconds>  (e.g. w 100)\n");
            }
            break;
        }

        case 'g': {
            int gap = 0;
            if (sscanf(line + 1, "%d", &gap) == 1 && gap >= 0 && gap <= 10000) {
                g_byte_gap_ms = gap;
                printf("  Byte gap set to %d ms\n", g_byte_gap_ms);
            } else {
                printf("  Usage: g <0-10000>  (milliseconds)\n");
            }
            break;
        }

        case 'r':
            if (g_pkt) {
                packetizer_reset(g_pkt);
                printf("  Packetizer reset.\n");
            }
            break;

        case 'i':
            show_status();
            break;

        case 'l': {
            char mod[32];
            int  on;
            if (sscanf(line + 1, "%31s %d", mod, &on) == 2) {
                log_module_enable(mod, on);
                printf("  Log module '%s' -> %s\n", mod, on ? "ON" : "OFF");
            } else {
                printf("  Usage: l <module_name> <0|1>\n");
            }
            break;
        }

        case 'v': {
            int lv;
            if (sscanf(line + 1, "%d", &lv) == 1 && lv >= 0 && lv <= 4) {
                log_set_level((log_level_t)lv);
                const char *names[] = {"OFF","ERROR","WARN","INFO","DEBUG"};
                printf("  Log level -> %s\n", names[lv]);
            } else {
                printf("  Usage: v <0-4>  (0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG)\n");
            }
            break;
        }

        case 'h':
            show_help();
            break;

        case 'q':
            g_running = 0;
            printf("  Bye.\n");
            break;

        default:
            printf("  Unknown command '%c'. Type 'h' for help.\n", cmd);
            break;
        }
    }

    /* ---- 清理 ---- */
    printf("\n  Cleaning up...\n");
    packetizer_timeout_destroy(g_pkt);
    frame_timer_sw_destroy(g_timer);
    printf("  Done. Total frames captured: %d\n", g_frame_count);

    return 0;
}
