/**
 * @file frame_timer_sw.c
 * @brief 软件定时器实现 —— poll 驱动，零依赖（无 pthread / 无后台线程）
 *
 * 时间源：
 *   Windows:  QueryPerformanceCounter (100ns 精度)
 *   Linux:    clock_gettime(CLOCK_MONOTONIC) (1ns 精度)
 *
 * 架构：
 *   主循环调用 poll() → 计算 elapsed → 逐 tick 递增 counter → 触发 cb
 *   每次 tick 触发一次回调（和硬件 ISR 行为一致），
 *   一次 poll 可能触发多个 tick（如果上次 poll 后过了很久）。
 */

#include "frame_timer_sw.h"
#include <stddef.h>

/* ============================================================
 *  跨平台时间函数
 * ============================================================ */
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>

  static uint64_t get_time_us(void) {
      static LARGE_INTEGER freq = {0};
      static int          freq_ok = 0;
      if (!freq_ok) {
          QueryPerformanceFrequency(&freq);
          freq_ok = 1;
      }
      LARGE_INTEGER count;
      QueryPerformanceCounter(&count);
      return (uint64_t)((count.QuadPart * 1000000ULL) / freq.QuadPart);
  }

  void frame_timer_sw_sleep_us(uint64_t us) {
      if (us > 2000) {
          Sleep((DWORD)(us / 1000));
      } else {
          /* 短睡眠：忙等保证精度 */
          uint64_t deadline = get_time_us() + us;
          while (get_time_us() < deadline) {
              /* spin */
          }
      }
  }
#else
  #define _POSIX_C_SOURCE 199309L
  #include <time.h>
  #include <unistd.h>

  static uint64_t get_time_us(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (uint64_t)ts.tv_sec * 1000000ULL
           + (uint64_t)ts.tv_nsec / 1000ULL;
  }

  void frame_timer_sw_sleep_us(uint64_t us) {
      if (us > 2000) {
          usleep((useconds_t)us);
      } else {
          uint64_t deadline = get_time_us() + us;
          while (get_time_us() < deadline) {
              /* spin */
          }
      }
  }
#endif

uint64_t frame_timer_sw_now_us(void) {
    return get_time_us();
}

/* ============================================================
 *  实例池（与硬件定时器相同的位图分配模式）
 * ============================================================ */
#define FRAME_TIMER_SW_MAX  4

typedef struct {
    frame_timer_t base;            /* [基类] ops / cb / ctx / counter      */
    uint8_t       sw_id;           /* [子类] 实例编号                      */
    uint32_t      tick_period_us;  /* [子类] 每次 tick 的微秒间隔          */
    int           running;         /* [子类] 是否正在计时                  */
    uint64_t      next_tick_us;    /* [子类] 下次 tick 的目标时间戳         */
} frame_timer_sw_inst_t;

static frame_timer_sw_inst_t sw_instances[FRAME_TIMER_SW_MAX];
static uint8_t               sw_occupied_map = 0U;

/* ============================================================
 *  ops 回调实现
 *
 *  与硬件定时器行为一致：
 *    start   → counter=0, running=1, 记录当前时间
 *    restart → counter=0（running 不变）
 *    stop    → counter=0, running=0
 * ============================================================ */

static void sw_start(frame_timer_t *t) {
    frame_timer_sw_inst_t *sw = (frame_timer_sw_inst_t *)t;
    t->counter   = 0;
    sw->running  = 1;
    sw->next_tick_us = get_time_us() + sw->tick_period_us;
}

static void sw_restart(frame_timer_t *t) {
    frame_timer_sw_inst_t *sw = (frame_timer_sw_inst_t *)t;
    t->counter   = 0;
    sw->next_tick_us = get_time_us() + sw->tick_period_us;
    /* running 不变 —— restart 不清 running */
}

static void sw_stop(frame_timer_t *t) {
    frame_timer_sw_inst_t *sw = (frame_timer_sw_inst_t *)t;
    t->counter  = 0;
    sw->running = 0;
}

static void sw_set_callback(frame_timer_t *t, timer_callback cb, void *ctx) {
    t->cb  = cb;
    t->ctx = ctx;
}

static const frame_timer_ops_t sw_timer_ops = {
    .start        = sw_start,
    .restart      = sw_restart,
    .stop         = sw_stop,
    .set_callback = sw_set_callback,
};

/* ============================================================
 *  工厂函数
 * ============================================================ */
frame_timer_t* frame_timer_sw_create(uint32_t tick_period_us) {
    for (uint8_t i = 0; i < FRAME_TIMER_SW_MAX; i++) {
        if (!(sw_occupied_map & (1U << i))) {
            sw_occupied_map |= (uint8_t)(1U << i);

            frame_timer_sw_inst_t *inst = &sw_instances[i];
            inst->base.ops       = &sw_timer_ops;
            inst->base.cb        = NULL;
            inst->base.ctx       = NULL;
            inst->base.counter   = 0;
            inst->sw_id          = i;
            inst->tick_period_us = (tick_period_us > 0) ? tick_period_us : 1000;
            inst->running        = 0;
            inst->next_tick_us   = 0;

            return &inst->base;
        }
    }
    return NULL;
}

void frame_timer_sw_destroy(frame_timer_t *t) {
    if (t == NULL) return;
    frame_timer_sw_inst_t *inst = (frame_timer_sw_inst_t *)t;
    uint8_t id = inst->sw_id;
    if (id >= FRAME_TIMER_SW_MAX) return;

    inst->running = 0;
    t->cb  = NULL;
    t->ctx = NULL;
    sw_occupied_map &= (uint8_t)~(1U << id);
}

/* ============================================================
 *  poll —— 主循环驱动入口
 *
 *  检查当前时间是否超过了 next_tick_us，
 *  每超过一个 tick_period_us 就触发一次 tick（counter++, cb 调用）。
 *  一次 poll 可能触发多个 tick（追赶模式）。
 *
 *  安全措施：
 *    - 回调可能停止定时器（如超时触发），循环中检查 running
 *    - 防无限追赶：最多追赶 1000 tick 后跳到当前时间
 * ============================================================ */
void frame_timer_sw_poll(frame_timer_t *t) {
    if (t == NULL) return;
    frame_timer_sw_inst_t *sw = (frame_timer_sw_inst_t *)t;
    if (!sw->running) return;

    uint64_t now = get_time_us();
    int      tick_count = 0;

    /* 追赶模式：每次 tick 触发一次回调，赶上实时 */
    while (now >= sw->next_tick_us && sw->running) {
        t->counter++;
        sw->next_tick_us += sw->tick_period_us;

        if (t->cb) {
            t->cb(t->ctx);
        }

        /* 回调可能停止了定时器（如超时触发），需重新检查 */
        if (!sw->running) break;

        /* 防止长时间未 poll 导致的无限追赶 */
        if (++tick_count > 1000) {
            sw->next_tick_us = now + sw->tick_period_us;
            break;
        }
    }
}

void frame_timer_sw_poll_all(void) {
    for (uint8_t i = 0; i < FRAME_TIMER_SW_MAX; i++) {
        if (sw_occupied_map & (1U << i)) {
            frame_timer_sw_poll(&sw_instances[i].base);
        }
    }
}
