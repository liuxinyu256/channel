/**
 * @file sim_gui.c
 * @brief 封包器 GUI 模拟器 —— Windows 原生窗口，输入框 + 实时回显
 *
 * 功能:
 *   - Hex 输入框（直接敲 AA BB CC）
 *   - 回显区：逐字节显示 TX，帧完成时高亮显示帧内容
 *   - 实时定时器 counter 显示
 *   - 字节间隔可调
 *
 * 编译（命令行）:
 *   gcc -std=c99 -Wall -Wextra -Isrc -I. \
 *       sim/sim_gui.c src/packetizer_timeout.c src/packet.c \
 *       src/frame_timer_sw.c src/log.c \
 *       -lgdi32 -o build/sim_gui.exe
 *
 * CMake 已配置，运行 build/sim_gui.exe 即可。
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "packetizer.h"
#include "packet.h"
#include "frame_timer_sw.h"

/* MinGW ANSI 模式：窗口类名需用 char 字符串 */
#ifndef WC_STATIC
#define WC_STATIC  "Static"
#endif
#ifndef WC_EDIT
#define WC_EDIT    "Edit"
#endif
#ifndef WC_BUTTON
#define WC_BUTTON  "Button"
#endif

/* ============================================================
 *  控件 ID
 * ============================================================ */
#define IDC_INPUT       1001
#define IDC_SEND        1002
#define IDC_GAP_EDIT    1003
#define IDC_RESET       1004
#define IDC_LOG         1005
#define IDC_STATUS      1006
#define IDC_TIMER_LABEL 1007
#define IDC_PREVIEW     1008
#define IDC_TIMER       2000

/* ============================================================
 *  模拟参数
 * ============================================================ */
#define SIM_TICK_PERIOD_US  1000
#define SIM_TIMEOUT_TICKS   10
#define TIMER_INTERVAL_MS   25

/* ============================================================
 *  全局
 * ============================================================ */
static frame_timer_t *g_timer    = NULL;
static packetizer_t  *g_pkt      = NULL;
static HWND           g_hLog     = NULL;
static HWND           g_hStatus  = NULL;
static HWND           g_hTimerLabel = NULL;
static HWND           g_hwnd     = NULL;
static int            g_byte_gap_ms = 0;
static int            g_frame_count = 0;

/* ---- 回显 buffer：暂存最近发送的字节用于展示 ---- */
static uint8_t  g_echo_buf[RX_PACKET_BUF_SIZE];
static uint16_t g_echo_len = 0;

/* ============================================================
 *  日志（追加到多行 Edit）
 * ============================================================ */
static void gui_log(const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len <= 0) return;

    int text_len = GetWindowTextLength(g_hLog);
    SendMessage(g_hLog, EM_SETSEL, (WPARAM)text_len, (LPARAM)text_len);
    SendMessage(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)buf);
    /* 自动滚到底部 */
    SendMessage(g_hLog, EM_LINESCROLL, 0, 99999);
}

/* ============================================================
 *  帧完成 → 回显区展示完整帧
 * ============================================================ */
static void on_frame_ready(uint8_t *frame, uint16_t len) {
    g_frame_count++;

    gui_log("\r\n");
    gui_log("+==================== FRAME #%d ====================+\r\n",
            g_frame_count);
    gui_log("|  Length: %u bytes\r\n", len);

    /* hex dump */
    char line[128];
    for (uint16_t offset = 0; offset < len; offset += 16) {
        int pos = 0;
        pos += snprintf(line + pos, sizeof(line) - pos, "|  %04X:  ", offset);
        for (uint16_t j = 0; j < 16 && (offset + j) < len; j++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", frame[offset + j]);
        }
        int pad = (16 - ((len - offset) < 16 ? (len - offset) : 16)) * 3;
        for (int k = 0; k < pad; k++) pos += snprintf(line + pos, sizeof(line) - pos, " ");

        pos += snprintf(line + pos, sizeof(line) - pos, " |");
        for (uint16_t j = 0; j < 16 && (offset + j) < len; j++) {
            uint8_t c = frame[offset + j];
            pos += snprintf(line + pos, sizeof(line) - pos, "%c",
                           (c >= 0x20 && c <= 0x7E) ? c : '.');
        }
        gui_log("%s\r\n", line);
    }
    gui_log("+======================================================+\r\n\r\n");

    g_echo_len = 0;
}

/* ============================================================
 *  更新状态栏 + 回显区
 * ============================================================ */
static void update_status(void) {
    if (g_timer == NULL || g_pkt == NULL) return;

    uint32_t counter = g_timer->counter;
    uint16_t rxidx   = g_pkt->Rxidx;

    /* 状态栏 */
    char status[256];
    snprintf(status, sizeof(status),
             "Timer: %u/%u ticks (%dms timeout)  |  Buffer: %u/256 bytes  |  Frames: %d  |  Gap: %dms",
             counter, (unsigned)SIM_TIMEOUT_TICKS,
             SIM_TIMEOUT_TICKS * SIM_TICK_PERIOD_US / 1000,
             rxidx, g_frame_count, g_byte_gap_ms);
    SetWindowText(g_hStatus, status);

    /* 定时器进度条式的显示 */
    char tlabel[32];
    snprintf(tlabel, sizeof(tlabel), "%u / %u", counter, (unsigned)SIM_TIMEOUT_TICKS);
    SetWindowText(g_hTimerLabel, tlabel);
}

/* ============================================================
 *  发送字节（逐 byte 走 put_byte，边发边回显）
 * ============================================================ */
static void do_send(const uint8_t *bytes, int count) {
    if (g_pkt == NULL || count <= 0) return;

    /* 回显：显示发送的 hex */
    char hex[256];
    int pos = 0;
    for (int i = 0; i < count && i < 32; i++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", bytes[i]);
    }
    if (count > 32) snprintf(hex + pos, sizeof(hex) - pos, "... +%d more", count - 32);
    gui_log("[TX] %s\r\n", hex);

    for (int i = 0; i < count; i++) {
        uint8_t byte = bytes[i];

        /* 逐字节喂入封包器 */
        uint8_t ret = packetizer_put_byte(g_pkt, byte);
        if (ret != 0) {
            gui_log("[ERR] Buffer overflow at byte %d (0x%02X)\r\n", i, byte);
            break;
        }

        /* 实时回显：每个字节到达后更新 echo buffer */
        if (g_echo_len < RX_PACKET_BUF_SIZE) {
            g_echo_buf[g_echo_len++] = byte;
        }

        /* 驱动定时器 */
        frame_timer_sw_poll(g_timer);

        /* 字节间间隔 */
        if (g_byte_gap_ms > 0 && i < count - 1) {
            frame_timer_sw_sleep_us((uint64_t)g_byte_gap_ms * 1000);
            frame_timer_sw_poll(g_timer);
            /* 间隔中如果超时成帧，echo buffer 已在回调中清空 */
        }
    }

    update_status();
}

/* ============================================================
 *  解析 hex 输入（支持 "AA BB CC" 和 "AABBCC" 两种格式）
 * ============================================================ */
static int parse_hex_input(const char *text, uint8_t *buf, int max) {
    int count = 0;
    char hex_digits[512];
    int di = 0;

    /* Step 1: 从输入中只提取 hex 字符 */
    for (const char *p = text; *p; p++) {
        if (isxdigit((unsigned char)*p)) {
            hex_digits[di++] = *p;
        }
    }
    hex_digits[di] = '\0';

    /* Step 2: 每两个 hex 字符组成一个字节 */
    for (int i = 0; i + 1 < di && count < max; i += 2) {
        char pair[3] = { hex_digits[i], hex_digits[i+1], '\0' };
        unsigned int val;
        if (sscanf(pair, "%2x", &val) == 1) {
            buf[count++] = (uint8_t)val;
        }
    }

    return count;
}

/* ============================================================
 *  窗口过程
 * ============================================================ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE: {
        HINSTANCE hi = ((LPCREATESTRUCT)lp)->hInstance;
        g_hwnd = hwnd;

        /* ---- 字体 ---- */
        HFONT hFont = CreateFont(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  FIXED_PITCH | FF_MODERN, TEXT("Consolas"));

        /* ---- 输入框标签 ---- */
        CreateWindowEx(0, WC_STATIC, TEXT("Hex Input (e.g. AA BB CC DD):"),
                       WS_CHILD | WS_VISIBLE,
                       10, 8, 250, 18,
                       hwnd, NULL, hi, NULL);

        /* ---- Hex 输入框 ---- */
        HWND hInput = CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, TEXT(""),
                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                       10, 28, 370, 26,
                       hwnd, (HMENU)IDC_INPUT, hi, NULL);
        SendMessage(hInput, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* ---- 发送按钮 ---- */
        HWND hSend = CreateWindowEx(0, WC_BUTTON, TEXT("Send"),
                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
                       390, 28, 80, 26,
                       hwnd, (HMENU)IDC_SEND, hi, NULL);

        /* ---- 字节间隔 ---- */
        CreateWindowEx(0, WC_STATIC, TEXT("Gap(ms):"),
                       WS_CHILD | WS_VISIBLE,
                       480, 8, 60, 18,
                       hwnd, NULL, hi, NULL);
        HWND hGap = CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, TEXT("0"),
                       WS_CHILD | WS_VISIBLE | ES_NUMBER,
                       480, 28, 50, 26,
                       hwnd, (HMENU)IDC_GAP_EDIT, hi, NULL);
        SendMessage(hGap, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* ---- 重置按钮 ---- */
        HWND hReset = CreateWindowEx(0, WC_BUTTON, TEXT("Reset"),
                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                       540, 28, 70, 26,
                       hwnd, (HMENU)IDC_RESET, hi, NULL);

        /* ---- 定时器显示 ---- */
        g_hTimerLabel = CreateWindowEx(0, WC_STATIC, TEXT("0 / 10"),
                       WS_CHILD | WS_VISIBLE | SS_CENTER | WS_BORDER,
                       10, 60, 100, 24,
                       hwnd, (HMENU)IDC_TIMER_LABEL, hi, NULL);
        {
            HFONT hTimerFont = CreateFont(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                           FIXED_PITCH | FF_MODERN, TEXT("Consolas"));
            SendMessage(g_hTimerLabel, WM_SETFONT, (WPARAM)hTimerFont, TRUE);
        }

        /* 回显区标签 */
        CreateWindowEx(0, WC_STATIC, TEXT("Echo preview:"),
                       WS_CHILD | WS_VISIBLE,
                       120, 60, 200, 18,
                       hwnd, NULL, hi, NULL);

        CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, TEXT(""),
                       WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
                       120, 78, 480, 22,
                       hwnd, (HMENU)IDC_PREVIEW, hi, NULL);
        SendMessage(GetDlgItem(hwnd, IDC_PREVIEW), WM_SETFONT, (WPARAM)hFont, TRUE);

        /* ---- 状态栏 ---- */
        g_hStatus = CreateWindowEx(0, WC_STATIC, TEXT("Ready - Type hex bytes, press Enter to send"),
                       WS_CHILD | WS_VISIBLE | SS_SUNKEN,
                       10, 108, 600, 22,
                       hwnd, (HMENU)IDC_STATUS, hi, NULL);

        /* ---- 回显/日志区（只读、多行、滚动） ---- */
        g_hLog = CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, TEXT(""),
                       WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                       ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                       10, 136, 600, 300,
                       hwnd, (HMENU)IDC_LOG, hi, NULL);
        SendMessage(g_hLog, WM_SETFONT, (WPARAM)hFont, TRUE);

        /* ---- 创建模拟器 ---- */
        g_timer = frame_timer_sw_create(SIM_TICK_PERIOD_US);
        g_pkt   = packetizer_timeout_create(g_timer, SIM_TIMEOUT_TICKS, on_frame_ready);

        /* 启动定时器 poll */
        SetTimer(hwnd, IDC_TIMER, TIMER_INTERVAL_MS, NULL);

        /* 初始化日志 */
        gui_log("+------------------------------------------------------+\r\n");
        gui_log("|   Packetizer Simulator v1.0                         |\r\n");
        gui_log("|   Tick: 1ms    Timeout: 10 ticks (10ms)             |\r\n");
        gui_log("|   Type hex bytes -> Enter/Send -> watch echo        |\r\n");
        gui_log("|   Idle > 10ms between bytes -> frame complete       |\r\n");
        gui_log("+------------------------------------------------------+\r\n\r\n");

        SetFocus(hInput);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        /* 定时器 label 着色 */
        HDC hdc = (HDC)wp;
        HWND hCtrl = (HWND)lp;
        if (hCtrl == g_hTimerLabel) {
            /* 背景：计数器接近阈值时变黄/红 */
            uint32_t counter = g_timer ? g_timer->counter : 0;
            if (counter == 0) {
                SetBkColor(hdc, RGB(230, 255, 230));  /* 绿 */
            } else if (counter < SIM_TIMEOUT_TICKS / 2) {
                SetBkColor(hdc, RGB(240, 255, 240));  /* 浅绿 */
            } else if (counter < SIM_TIMEOUT_TICKS) {
                SetBkColor(hdc, RGB(255, 255, 200));  /* 黄 */
            } else {
                SetBkColor(hdc, RGB(255, 220, 220));  /* 红 */
            }
            return (LRESULT)GetStockObject(DC_BRUSH);
        }
        return 0;
    }

    case WM_TIMER:
        if (g_timer) frame_timer_sw_poll(g_timer);
        update_status();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);

        switch (id) {

        case IDC_SEND: {
            char text[512];
            GetWindowText(GetDlgItem(hwnd, IDC_INPUT), text, sizeof(text));

            uint8_t bytes[256];
            int count = parse_hex_input(text, bytes, (int)sizeof(bytes));

            if (count > 0) {
                do_send(bytes, count);
                SetWindowText(GetDlgItem(hwnd, IDC_INPUT), TEXT(""));
                /* 回显提示 */
                char preview[128];
                int p = 0;
                for (int i = 0; i < count && i < 8; i++)
                    p += snprintf(preview + p, sizeof(preview) - p, "%02X ", bytes[i]);
                if (count > 8) snprintf(preview + p, sizeof(preview) - p, "... (%d bytes)", count);
                SetWindowText(GetDlgItem(hwnd, IDC_PREVIEW), TEXT(""));
                gui_log("[ECHO] -> %s\r\n", preview);
            } else {
                gui_log("[?] Empty or invalid hex\r\n");
            }
            SetFocus(GetDlgItem(hwnd, IDC_INPUT));
            return 0;
        }

        /* 输入框实时预览：边输入边显示将发送的字节 */
        case IDC_INPUT: {
            if (HIWORD(wp) == EN_CHANGE) {
                char text[512];
                GetWindowText(GetDlgItem(hwnd, IDC_INPUT), text, sizeof(text));
                uint8_t bytes[256];
                int count = parse_hex_input(text, bytes, (int)sizeof(bytes));
                if (count > 0) {
                    char preview[128];
                    int p = 0;
                    p += snprintf(preview + p, sizeof(preview) - p, "Will send: ");
                    for (int i = 0; i < count && i < 12; i++)
                        p += snprintf(preview + p, sizeof(preview) - p, "%02X ", bytes[i]);
                    if (count > 12)
                        snprintf(preview + p, sizeof(preview) - p, "... (%d bytes total)", count);
                    else if (count > 0)
                        snprintf(preview + p, sizeof(preview) - p, "(%d bytes)", count);
                    SetWindowText(GetDlgItem(hwnd, IDC_PREVIEW), preview);
                } else {
                    SetWindowText(GetDlgItem(hwnd, IDC_PREVIEW), TEXT(""));
                }
            }
            return 0;
        }

        case IDC_GAP_EDIT: {
            char text[16];
            GetWindowText(GetDlgItem(hwnd, IDC_GAP_EDIT), text, sizeof(text));
            int gap = atoi(text);
            if (gap < 0) gap = 0;
            if (gap > 10000) gap = 10000;
            g_byte_gap_ms = gap;
            return 0;
        }

        case IDC_RESET:
            if (g_pkt) {
                packetizer_reset(g_pkt);
                g_echo_len = 0;
                gui_log("--- Reset ---\r\n");
                update_status();
            }
            SetFocus(GetDlgItem(hwnd, IDC_INPUT));
            return 0;
        }
        return 0;
    }

    case WM_GETDLGCODE:
        if (wp == IDC_INPUT) return DLGC_WANTALLKEYS;
        return 0;

    case WM_KEYDOWN:
        /* Enter in input → send */
        if (wp == VK_RETURN && GetFocus() == GetDlgItem(hwnd, IDC_INPUT)) {
            SendMessage(hwnd, WM_COMMAND, IDC_SEND, 0);
            return 0;
        }
        /* ESC → clear input */
        if (wp == VK_ESCAPE) {
            SetWindowText(GetDlgItem(hwnd, IDC_INPUT), TEXT(""));
            SetFocus(GetDlgItem(hwnd, IDC_INPUT));
            return 0;
        }
        return 0;

    case WM_SIZE: {
        int w = LOWORD(lp);
        int h = HIWORD(lp);
        if (w < 400) w = 400;
        if (h < 350) h = 350;

        SetWindowPos(GetDlgItem(hwnd, IDC_INPUT),    NULL, 10, 28, w - 240, 26, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(hwnd, IDC_SEND),     NULL, w - 220, 28, 80, 26, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(hwnd, IDC_GAP_EDIT), NULL, w - 130, 28, 50, 26, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(hwnd, IDC_RESET),    NULL, w - 72, 28, 60, 26, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(hwnd, IDC_PREVIEW),  NULL, 120, 78, w - 135, 22, SWP_NOZORDER);
        SetWindowPos(g_hStatus, NULL, 10, 108, w - 25, 22, SWP_NOZORDER);
        SetWindowPos(g_hLog, NULL, 10, 136, w - 25, h - 175, SWP_NOZORDER);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, IDC_TIMER);
        packetizer_timeout_destroy(g_pkt);
        frame_timer_sw_destroy(g_timer);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ============================================================
 *  入口
 * ============================================================ */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = TEXT("PacketizerSimGUI");

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, TEXT("Window registration failed"), TEXT("Error"), MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_COMPOSITED,  /* double-buffer: eliminate flicker */
        TEXT("PacketizerSimGUI"),
        TEXT("Packetizer Simulator - UART + Timer"),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 660, 520,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBox(NULL, TEXT("Window creation failed"), TEXT("Error"), MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
