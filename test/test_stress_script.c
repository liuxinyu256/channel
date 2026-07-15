/**
 * @file test_stress_script.c
 * @brief 批量压力测试 —— 可脚本化的封包器验证工具
 *
 * 支持三种模式：
 *   1. 自动生成随机帧（-n 帧数 -l 帧长）
 *   2. 从文件读取 hex 数据（-f 文件）
 *   3. 固定模式循环（-p 模式）
 *
 * 编译（命令行）:
 *   gcc -std=c99 -Wall -Wextra -Isrc -I. \
 *       test/test_stress_script.c src/packetizer_timeout.c src/packet.c \
 *       src/frame_timer_sw.c \
 *       -o build/test_stress && build/test_stress -n 10000 -l 128
 *
 * 用法示例:
 *   build/test_stress -n 10000 -l 128              # 10000 帧 x 128 字节
 *   build/test_stress -n 5000  -l 255 -g 5         # 5ms 字节间隔
 *   build/test_stress -n 1000  -l 64  -t 5  -u 500 # 5tick 超时, 500us/tick
 *   build/test_stress -f test_data.hex             # 从文件读
 *   build/test_stress -n 20000 -l 128 -q           # 静默模式（只输出结果）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "packetizer.h"
#include "packet.h"            /* struct packetizer 定义 */
#include "frame_timer_sw.h"

/* ============================================================
 *  默认参数
 * ============================================================ */
#define DEFAULT_FRAME_COUNT    1000
#define DEFAULT_FRAME_LEN      128
#define DEFAULT_TIMEOUT_TICKS  10
#define DEFAULT_TICK_PERIOD_US 1000
#define DEFAULT_BYTE_GAP_MS    0

/* ============================================================
 *  全局统计
 * ============================================================ */
typedef struct {
    int      total_frames_sent;
    int      total_frames_captured;
    int      total_bytes_sent;
    int      mismatches;
    int      length_errors;
    int      overflows;
    uint64_t start_time_us;
    uint64_t end_time_us;
} stats_t;

static stats_t g_stats;

/* ============================================================
 *  帧捕获
 * ============================================================ */
static uint8_t  g_captured[RX_PACKET_BUF_SIZE];
static uint16_t g_captured_len;
static int      g_frame_flag;

static void capture_cb(uint8_t *frame, uint16_t len) {
    uint16_t n = (len > RX_PACKET_BUF_SIZE) ? RX_PACKET_BUF_SIZE : len;
    memcpy(g_captured, frame, n);
    g_captured_len = n;
    g_frame_flag   = 1;
    g_stats.total_frames_captured++;
}

/* ============================================================
 *  自动生成测试帧（可复现 LCG）
 * ============================================================ */
static void generate_frame(uint8_t *buf, int len, int seed) {
    unsigned int state = (unsigned int)(seed + 1) * 1103515245U + 12345U;
    for (int i = 0; i < len; i++) {
        state = state * 1103515245U + 12345U;
        buf[i] = (uint8_t)((state >> 16) & 0xFF);
    }
    buf[0]     = (uint8_t)(seed & 0xFF);
    buf[len-1] = (uint8_t)((seed >> 8) & 0xFF);
}

/* ============================================================
 *  喂一帧数据 + 等待超时
 *  返回: 0=成功, -1=溢出, -2=超时未触发帧
 * ============================================================ */
static int feed_frame(packetizer_t *pkt, frame_timer_t *timer,
                       const uint8_t *data, int len,
                       int byte_gap_ms, int timeout_ticks, int tick_period_us) {
    g_frame_flag = 0;

    for (int i = 0; i < len; i++) {
        uint8_t ret = packetizer_put_byte(pkt, data[i]);
        if (ret != 0) {
            g_stats.overflows++;
            return -1;
        }
        g_stats.total_bytes_sent++;

        frame_timer_sw_poll(timer);

        if (byte_gap_ms > 0 && i < len - 1) {
            frame_timer_sw_sleep_us((uint64_t)byte_gap_ms * 1000);
            frame_timer_sw_poll(timer);
        }
    }

    /* 等待超时触发 */
    int wait_us = (timeout_ticks + 5) * tick_period_us;
    int waited  = 0;
    int step_us = tick_period_us;

    while (!g_frame_flag && waited < wait_us) {
        frame_timer_sw_sleep_us((uint64_t)step_us);
        frame_timer_sw_poll(timer);
        waited += step_us;
    }

    return g_frame_flag ? 0 : -2;
}

/* ============================================================
 *  验证帧内容
 * ============================================================ */
static int verify_frame(const uint8_t *expected, int expected_len) {
    if (g_captured_len != (uint16_t)expected_len) {
        g_stats.length_errors++;
        return 0;
    }
    if (memcmp(g_captured, expected, (size_t)expected_len) != 0) {
        g_stats.mismatches++;
        return 0;
    }
    return 1;
}

/* ============================================================
 *  模式 1: 自动生成随机帧
 * ============================================================ */
static int run_auto_mode(int frame_count, int frame_len,
                          int byte_gap_ms, int timeout_ticks, int tick_period_us,
                          int verbose) {
    frame_timer_t *timer = frame_timer_sw_create((uint32_t)tick_period_us);
    if (!timer) { printf("[FATAL] Timer create failed\n"); return 1; }

    packetizer_t *pkt = packetizer_timeout_create(timer, (uint16_t)timeout_ticks, capture_cb);
    if (!pkt) { frame_timer_sw_destroy(timer); return 1; }

    uint8_t *expected = (uint8_t *)malloc((size_t)frame_len);
    if (!expected) {
        packetizer_timeout_destroy(pkt);
        frame_timer_sw_destroy(timer);
        return 1;
    }

    g_stats.total_frames_sent = frame_count;
    g_stats.start_time_us = frame_timer_sw_now_us();

    int errors = 0;
    for (int i = 0; i < frame_count; i++) {
        generate_frame(expected, frame_len, i);

        int ret = feed_frame(pkt, timer, expected, frame_len,
                             byte_gap_ms, timeout_ticks, tick_period_us);
        if (ret == -1) {
            errors++;
            if (verbose) printf("  [%d] OVERFLOW\n", i);
            packetizer_reset(pkt);
        } else if (ret == -2) {
            errors++;
            if (verbose) printf("  [%d] NO_TIMEOUT\n", i);
            packetizer_reset(pkt);
        } else if (!verify_frame(expected, frame_len)) {
            errors++;
            if (verbose) printf("  [%d] MISMATCH (got len=%d)\n", i, (int)g_captured_len);
        }

        if (verbose && (i + 1) % 1000 == 0) {
            printf("  ... %d/%d frames, %d errors\n", i + 1, frame_count, errors);
        }
    }

    g_stats.end_time_us = frame_timer_sw_now_us();

    free(expected);
    packetizer_timeout_destroy(pkt);
    frame_timer_sw_destroy(timer);

    return errors;
}

/* ============================================================
 *  模式 2: 从 hex 文件读取
 * ============================================================ */
static int run_file_mode(const char *filename,
                          int byte_gap_ms, int timeout_ticks, int tick_period_us) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("[FATAL] Cannot open '%s'\n", filename);
        return 1;
    }

    frame_timer_t *timer = frame_timer_sw_create((uint32_t)tick_period_us);
    if (!timer) { fclose(f); return 1; }

    packetizer_t *pkt = packetizer_timeout_create(timer, (uint16_t)timeout_ticks, capture_cb);
    if (!pkt) { frame_timer_sw_destroy(timer); fclose(f); return 1; }

    g_stats.start_time_us = frame_timer_sw_now_us();

    char line[1024];
    int  line_no = 0;
    int  errors  = 0;

    while (fgets(line, (int)sizeof(line), f)) {
        line_no++;

        /* 跳过注释和空行 */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        /* 去掉末尾换行 */
        size_t l = strlen(p);
        while (l > 0 && (p[l-1] == '\n' || p[l-1] == '\r')) p[--l] = '\0';

        /* 解析 hex 字符串 */
        uint8_t bytes[RX_PACKET_BUF_SIZE];
        int     count = 0;
        char    *tok = strtok(p, " \t");
        while (tok && count < RX_PACKET_BUF_SIZE) {
            unsigned int val;
            if (sscanf(tok, "%2x", &val) == 1) {
                bytes[count++] = (uint8_t)val;
            } else {
                printf("  [WARN] Line %d: bad hex '%s', skipping\n", line_no, tok);
            }
            tok = strtok(NULL, " \t");
        }

        if (count == 0) continue;

        g_stats.total_frames_sent++;

        int ret = feed_frame(pkt, timer, bytes, count,
                             byte_gap_ms, timeout_ticks, tick_period_us);
        if (ret == 0) {
            if (!verify_frame(bytes, count)) {
                errors++;
                printf("  [FAIL] Line %d: MISMATCH\n", line_no);
            }
        } else {
            errors++;
            printf("  [FAIL] Line %d: %s\n", line_no,
                   ret == -1 ? "OVERFLOW" : "NO_TIMEOUT");
        }
    }

    g_stats.end_time_us = frame_timer_sw_now_us();

    fclose(f);
    packetizer_timeout_destroy(pkt);
    frame_timer_sw_destroy(timer);

    return errors;
}

/* ============================================================
 *  模式 3: 固定 hex 模式循环
 * ============================================================ */
static int run_pattern_mode(const char *pattern, int repeat,
                             int byte_gap_ms, int timeout_ticks, int tick_period_us) {
    uint8_t bytes[RX_PACKET_BUF_SIZE];
    int     pat_len = 0;

    const char *p = pattern;
    char token[4];
    while (*p && pat_len < RX_PACKET_BUF_SIZE) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        token[0] = *p++;
        token[1] = (*p && *p != ' ' && *p != '\t') ? *p++ : '\0';
        token[2] = '\0';

        unsigned int val;
        if (sscanf(token, "%2x", &val) == 1) {
            bytes[pat_len++] = (uint8_t)val;
        }
    }

    if (pat_len == 0) {
        printf("[FATAL] Empty pattern\n");
        return 1;
    }

    frame_timer_t *timer = frame_timer_sw_create((uint32_t)tick_period_us);
    if (!timer) return 1;

    packetizer_t *pkt = packetizer_timeout_create(timer, (uint16_t)timeout_ticks, capture_cb);
    if (!pkt) { frame_timer_sw_destroy(timer); return 1; }

    g_stats.total_frames_sent = repeat;
    g_stats.start_time_us = frame_timer_sw_now_us();

    int errors = 0;
    for (int i = 0; i < repeat; i++) {
        int ret = feed_frame(pkt, timer, bytes, pat_len,
                             byte_gap_ms, timeout_ticks, tick_period_us);
        if (ret != 0) {
            errors++;
            packetizer_reset(pkt);
        } else if (!verify_frame(bytes, pat_len)) {
            errors++;
        }

        if ((i + 1) % 5000 == 0) {
            printf("  ... %d/%d iterations, %d errors\n", i + 1, repeat, errors);
        }
    }

    g_stats.end_time_us = frame_timer_sw_now_us();

    packetizer_timeout_destroy(pkt);
    frame_timer_sw_destroy(timer);

    return errors;
}

/* ============================================================
 *  报告
 * ============================================================ */
static void print_report(int errors) {
    double elapsed_s = (double)(g_stats.end_time_us - g_stats.start_time_us) / 1000000.0;
    double bytes_per_sec = (elapsed_s > 0.0)
        ? (double)g_stats.total_bytes_sent / elapsed_s : 0.0;
    double frames_per_sec = (elapsed_s > 0.0)
        ? (double)g_stats.total_frames_sent / elapsed_s : 0.0;

    printf("\n");
    printf("  ╔══════════════════════════════════════════╗\n");
    printf("  ║        STRESS TEST REPORT               ║\n");
    printf("  ╠══════════════════════════════════════════╣\n");
    printf("  ║  Frames sent:      %8d            ║\n", g_stats.total_frames_sent);
    printf("  ║  Frames captured:  %8d            ║\n", g_stats.total_frames_captured);
    printf("  ║  Bytes sent:       %8d            ║\n", g_stats.total_bytes_sent);
    printf("  ║  Overflows:        %8d            ║\n", g_stats.overflows);
    printf("  ║  Length errors:    %8d            ║\n", g_stats.length_errors);
    printf("  ║  Mismatches:       %8d            ║\n", g_stats.mismatches);
    printf("  ╠══════════════════════════════════════════╣\n");
    printf("  ║  Time:             %8.3f s         ║\n", elapsed_s);
    printf("  ║  Throughput:       %8.0f B/s      ║\n", bytes_per_sec);
    printf("  ║  Frame rate:       %8.0f fps      ║\n", frames_per_sec);
    printf("  ╚══════════════════════════════════════════╝\n");

    if (g_stats.total_frames_captured == g_stats.total_frames_sent
        && errors == 0 && g_stats.overflows == 0) {
        printf("\n  *** ALL TESTS PASSED ***\n\n");
    } else {
        printf("\n  *** %d FAILURES ***\n\n", errors + g_stats.overflows);
    }
}

/* ============================================================
 *  入口
 * ============================================================ */
static void usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("Modes (choose one):\n");
    printf("  -n <count> -l <len>   Auto-generate N frames of L bytes each\n");
    printf("  -f <file>             Read hex frames from file (one per line)\n");
    printf("  -p <hex> -n <count>   Repeat pattern N times\n");
    printf("\n");
    printf("Timing options:\n");
    printf("  -g <ms>      Byte gap in ms (default: 0)\n");
    printf("  -t <ticks>   Timeout ticks (default: 10)\n");
    printf("  -u <us>      Tick period in us (default: 1000)\n");
    printf("\n");
    printf("Output:\n");
    printf("  -v           Verbose (show per-error output)\n");
    printf("  -q           Quiet (only final report)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s -n 10000 -l 128              # 10000 frames of 128 bytes\n", prog);
    printf("  %s -n 5000  -l 255 -g 5         # with 5ms byte gap\n", prog);
    printf("  %s -p \"AA BB CC\" -n 2000       # repeat pattern\n", prog);
    printf("  %s -f test_data.hex             # from file\n", prog);
}

int main(int argc, char **argv) {
    int    frame_count    = 0;
    int    frame_len      = DEFAULT_FRAME_LEN;
    int    byte_gap_ms    = DEFAULT_BYTE_GAP_MS;
    int    timeout_ticks  = DEFAULT_TIMEOUT_TICKS;
    int    tick_period_us = DEFAULT_TICK_PERIOD_US;
    int    verbose        = 0;
    int    quiet          = 0;
    char   *filename      = NULL;
    char   *pattern       = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            frame_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            frame_len = atoi(argv[++i]);
            if (frame_len < 1 || frame_len > 256) {
                printf("Frame length must be 1-256\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            filename = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            pattern = argv[++i];
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            byte_gap_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            timeout_ticks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            tick_period_us = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (filename == NULL && pattern == NULL && frame_count == 0) {
        frame_count = DEFAULT_FRAME_COUNT;
    }

    if (!quiet) {
        printf("\n  Stress Test Configuration:\n");
        if (filename) {
            printf("    Mode:   File (%s)\n", filename);
        } else if (pattern) {
            printf("    Mode:   Pattern '%s' x %d\n", pattern, frame_count);
        } else {
            printf("    Mode:   Auto %d frames x %d bytes\n", frame_count, frame_len);
        }
        printf("    Timeout:%d ticks x %d us = %d ms\n",
               timeout_ticks, tick_period_us,
               timeout_ticks * tick_period_us / 1000);
        printf("    Gap:    %d ms between bytes\n", byte_gap_ms);
        printf("\n");
    }

    memset(&g_stats, 0, sizeof(g_stats));

    int errors = 0;
    if (filename) {
        errors = run_file_mode(filename, byte_gap_ms, timeout_ticks, tick_period_us);
    } else if (pattern) {
        errors = run_pattern_mode(pattern, frame_count, byte_gap_ms,
                                   timeout_ticks, tick_period_us);
    } else {
        errors = run_auto_mode(frame_count, frame_len, byte_gap_ms,
                                timeout_ticks, tick_period_us, verbose);
    }

    print_report(errors);

    return (errors + g_stats.overflows == 0) ? 0 : 1;
}
