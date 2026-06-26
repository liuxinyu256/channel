# Channel 数据通信框架 — 架构与使用指南

## 1. 分层架构

```
┌──────────────────────────────────────────────────┐
│  channel（数据通道层）                             │
│  - 通道状态机 (IDLE → RUNNING → STOPPING → ERROR) │
│  - 收发回调管理                                   │
│  - 绑定封包器 + 定时器                            │
└────────────┬─────────────────────────────────────┘
             │ 依赖注入
             ▼
┌──────────────────────────────────────────────────┐
│  packetizer（封包器层）                            │
│  - 策略模式：超时封包 / 分隔符 / 长度前缀 ...       │
│  - put_byte → strategy.on_byte → is_frame_complete │
│  - 不自己创建定时器，外部注入                       │
└──────┬──────────────────┬────────────────────────┘
       │ 依赖注入          │ 依赖注入
       ▼                  ▼
┌──────────────┐  ┌─────────────────────────────────┐
│ packetizer   │  │  frame_timer（定时器层）           │
│ strategy     │  │  - 基类持有 counter + threshold   │
│ (封包策略)    │  │  - ops 多态（硬件/软件定时器）      │
└──────────────┘  └─────────┬───────────────────────┘
                            │
              ┌─────────────┴─────────────┐
              ▼                           ▼
  ┌───────────────────┐     ┌───────────────────────┐
  │ frame_timer_hw    │     │ frame_timer_sw（待实现）│
  │ 硬件定时器         │     │ 软件定时器              │
  │ 驱动源：硬件 ISR   │     │ 驱动源：systick 钩子    │
  └────────┬──────────┘     └───────────────────────┘
           │
           ▼
  ┌───────────────────────────────────────────────┐
  │  timer_hw_adapter（硬件适配器）                  │
  │  换平台只改此表                                  │
  └────────┬──────────────────────────────────────┘
           │
           ▼
  ┌───────────────────────────────────────────────┐
  │  timer1.c（BSP 层，裸寄存器操作）                 │
  └───────────────────────────────────────────────┘
```

### 各层职责

| 层 | 文件 | 职责 |
|----|------|------|
| 数据通道 | `channel.h` / `channel.c` | 通道状态机，绑定封包器和定时器，暴露收发接口给应用层 |
| 封包器基类 | `packet.h` / `packet.c` | 帧缓冲管理、`put_byte` 通用逻辑、策略调度 |
| 封包策略 | `packetizer_timeout.c` | 具体策略实现：怎样算收到一帧、收到字节时做什么 |
| 定时器基类 | `frame_timer.h` | 定义 counter / threshold / cb / ctx + ops 虚表 |
| 硬件定时器 | `frame_timer_hw.h` / `frame_timer_hw.c` | ISR 驱动 counter 递增，适配器表隔离平台差异 |
| BSP | `timer1.h` / `timer1.c` | 裸寄存器操作（当前空桩，待填入平台代码） |

---

## 2. 核心设计模式

### 2.1 C 风格继承

```
frame_timer_hw_inst_t          frame_timer_sw_inst_t
  ┌──────────────┐               ┌──────────────┐
  │ base (基类)   │  ← 第一个成员  │ base (基类)   │
  │  - ops        │               │  - ops        │
  │  - cb / ctx   │               │  - cb / ctx   │
  │  - counter    │  ← 共用计数器  │  - counter    │
  │  - threshold  │               │  - threshold  │
  ├──────────────┤               ├──────────────┤
  │ hw_id        │  ← 子类字段   │ (sw 特有)     │
  │ tick_period  │               │               │
  └──────────────┘               └──────────────┘
```

- `(frame_timer_t*)&inst == &inst.base`，向上/向下转型无需 offsetof。
- `counter` 在基类，硬件 ISR 和软件 systick 两种驱动源操作同一个字段。

### 2.2 多态操作表（vtable）

```c
// 调用者只持有基类指针，不关心底层实现
frame_timer_t *t = frame_timer_hw_create(...);
t->ops->start(t);   // → hw_start()
t->ops->stop(t);    // → hw_stop()
```

绑定关系：
```
frame_timer_t.ops ──▶ hw_timer_ops { hw_start, hw_stop, hw_restart }
                  ▶  sw_timer_ops { sw_start, sw_stop, sw_restart }（待实现）
```

### 2.3 依赖注入（Dependency Injection）

封包器**不内部创建**定时器，由外部注入：

```c
frame_timer_t *timer = frame_timer_hw_create(timeout_cb, pkt, 10000, 0);
packetizer_t  *pkt   = packetizer_timeout_create(10000, timer);
```

好处：生产环境用硬件定时器，单元测试可注入模拟定时器，无需修改封包器代码。

### 2.4 静态实例池 + 位图分配

```c
static frame_timer_hw_inst_t hw_instances[4];  // 4 个硬件定时器槽
static uint8_t               hw_occupied_map;  // 位图：bit=1 表示已占用
```

嵌入式场景不用 `malloc`，确定性好，无碎片。

### 2.5 平台适配器表

```c
static const timer_hw_adapter_t hw_adapter[4] = {
    [0] = { timer1_start, timer1_stop, timer1_init, ... },
    [1] = { timer2_start, timer2_stop, timer2_init, ... },  // 新增通道
};
```

换 MCU 或新增硬件定时器通道，只改此表，其余代码不变。

---

## 3. 数据流

### 3.1 接收帧流程

```
硬件收到字节
    │
    ▼
packetizer_put_byte(pkt, byte)
    │
    ├── Rxbuf[Rxidx++] = byte          // 存入帧缓冲
    ├── strategy->on_byte(pkt)          // 通知策略："来了一个字节"
    │       │
    │       └── timer->ops->restart()   // 超时策略：重置定时器
    │
    └── strategy->is_frame_complete()   // 基类判断：帧收完了吗？
            │
            └── YES → on_frame_finish() // 通知 channel 层
```

### 3.2 定时器超时流程

```
硬件 ISR 触发
    │
    ▼
frame_timer_hw_isr(hw_id)
    ├── clear_isr_flag()               // 清中断标志（平台相关）
    ├── ++base.counter                 // 递增基类计数器（平台无关）
    └── counter >= threshold ?
            ├── YES → counter = 0; cb(ctx)  // 超时回调
            └── NO  → return
```

软件定时器流程完全相同，只是计数器递增来源从 ISR 变成 systick 钩子。

---

## 4. 使用方法

### 4.1 创建硬件定时器

```c
#include "frame_timer.h"

void my_timeout_handler(void *ctx) {
    // 超时处理逻辑
}

frame_timer_t *timer = frame_timer_hw_create(
    my_timeout_handler,  // 超时回调
    NULL,                // 回调上下文
    10000,               // 超时时间 10ms（微秒）
    0                    // 硬件定时器通道号
);
if (timer == NULL) {
    // 无可用实例，处理错误
}
```

### 4.2 创建超时封包器

```c
#include "packet.h"
#include "frame_timer.h"

// 1. 先创建定时器
frame_timer_t *timer = frame_timer_hw_create(timeout_cb, some_ctx, 10000, 0);

// 2. 创建封包器，注入定时器
packetizer_t *pkt = packetizer_timeout_create(10000, timer);
if (pkt == NULL) {
    // 无可用实例
}

// 3. 设置帧完成回调
set_frame_finish_callback(pkt, on_frame_ready);

// 4. 喂字节
packetizer_put_byte(pkt, byte);  // 每收到一个字节调用一次
```

### 4.3 收帧时序图

```
时间 →

字节到达:  [B1] [B2] [B3] ........... [B4]
            │    │    │                    │
put_byte    ▼    ▼    ▼                    ▼
           ┌───┐┌───┐┌───┐              ┌───┐
定时器      │ R ││ R ││ R │  ... 超时... │ R │
           └───┘└───┘└───┘              └───┘
                                  │
                                  ▼
                          is_frame_complete → 帧完成回调
```

R = restart（每次收到字节重启定时器）。帧间隔超过阈值 → 判定为一帧结束。

### 4.4 销毁

```c
packetizer_timeout_destroy(pkt);   // 归还封包器槽位
frame_timer_hw_destroy(timer);     // 停止硬件定时器，归还槽位
```

---

## 5. 如何新增一种封包策略

以新增"分隔符封包策略"（如 `\r\n` 结尾）为例：

### 5.1 注册策略类型

```c
// packet.h
typedef enum {
    packetizer_type_Timeout,    // 超时封包（已有）
    packetizer_type_Delimiter,  // 分隔符封包（新增）
} packetizer_type;
```

### 5.2 创建策略文件

```c
// packetizer_delimiter.c

#include "packet.h"
#include <string.h>

/* ---- 子类结构体 ---- */
typedef struct {
    packetizer_t  base;
    uint8_t       delimiter[4];   // 分隔符序列，如 "\r\n"
    uint8_t       delim_len;      // 分隔符长度
    uint8_t       match_idx;      // 当前已匹配的分隔符位置
} packetizer_Delimiter_t;

#define MAX_DELIMITER_INSTANCES 4

static packetizer_Delimiter_t delim_instances[MAX_DELIMITER_INSTANCES];
static uint8_t                delim_occupied_map = 0U;

/* ---- 策略钩子实现 ---- */
static void delim_on_byte(packetizer_t *pkt) {
    packetizer_Delimiter_t *self = (packetizer_Delimiter_t *)pkt;
    uint8_t last_byte = pkt->Rxbuf[pkt->Rxidx - 1];

    if (last_byte == self->delimiter[self->match_idx]) {
        self->match_idx++;
    } else {
        self->match_idx = 0;
    }
}

static uint8_t delim_is_frame_complete(packetizer_t *pkt) {
    packetizer_Delimiter_t *self = (packetizer_Delimiter_t *)pkt;
    return (self->match_idx >= self->delim_len) ? 1 : 0;
}

/* ---- Ops 实现 ---- */
static void delim_init(packetizer_t *pkt) {
    packetizer_Delimiter_t *self = (packetizer_Delimiter_t *)pkt;
    self->base.Rxidx = 0;
    self->match_idx  = 0;
    memset(self->base.Rxbuf, 0, sizeof(self->base.Rxbuf));
}

static void delim_reset(packetizer_t *pkt) {
    delim_init(pkt);  // 两者逻辑相同
}

/* ---- 绑定虚表 ---- */
static const packet_ops_t delim_ops = {
    .init  = delim_init,
    .reset = delim_reset,
};

static const packet_strategy_t delim_strategy = {
    .on_byte           = delim_on_byte,
    .is_frame_complete = delim_is_frame_complete,
};

/* ---- 工厂函数 ---- */
packetizer_t* packetizer_delimiter_create(const uint8_t *delim, uint8_t delim_len) {
    for (uint8_t i = 0; i < MAX_DELIMITER_INSTANCES; i++) {
        if (!(delim_occupied_map & (1 << i))) {
            delim_occupied_map |= (1 << i);
            delim_instances[i].base.ops      = &delim_ops;
            delim_instances[i].base.strategy = &delim_strategy;
            delim_instances[i].base.type     = packetizer_type_Delimiter;
            delim_instances[i].delim_len     = delim_len;
            memcpy(delim_instances[i].delimiter, delim, delim_len);
            return &delim_instances[i].base;
        }
    }
    return NULL;
}
```

### 5.3 声明工厂函数

```c
// packet.h 中追加
packetizer_t* packetizer_delimiter_create(const uint8_t *delim, uint8_t delim_len);
void          packetizer_delimiter_destroy(packetizer_t *pkt);
```

### 5.4 使用方式（和超时封包器完全一致）

```c
packetizer_t *pkt = packetizer_delimiter_create((uint8_t*)"\r\n", 2);
set_frame_finish_callback(pkt, on_frame_ready);
packetizer_put_byte(pkt, byte);  // 上层调用不变
```

### 关键约束

新增策略时只需实现两个表（`packet_ops_t` + `packet_strategy_t`），上层 `packetizer_put_byte()` 和 `get_frame()` 等基类方法**无需修改**。这正是策略模式的价值——扩展新策略不触动已有代码。
