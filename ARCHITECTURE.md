# HVAC 多品牌网关 —— 架构设计

## 1. 整体分层

```
┌─────────────────────────────────────────────┐
│  AC 任务    事件队列 → 派发品牌事件表          │
│             BLE 命令 / 帧解析 / 定时查询 / ACK │
├─────────────────────────────────────────────┤
│  总线任务    半双工收发                        │
│             TX 逐字节发送 / RX 帧投递          │
├─────────────────────────────────────────────┤
│  phy_driver  RS485 / TTL / HBS / IR          │
│              open / close / write / set_rx_cb │
├─────────────────────────────────────────────┤
│  packetizer  超时封包 / 定长封包               │
│              ring_read / ring_write           │
├─────────────────────────────────────────────┤
│  ring_t      标准环形缓冲区 (RX/TX 通用)        │
└─────────────────────────────────────────────┘
```

---

## 2. 事件处理表 (直接调用, 不排队)

```c
typedef enum {
    EVENT_PERIODIC_SEND,    /* 定时器到期: 发查询帧 */
    EVENT_RX_FRAME,         /* 封包器帧完成: 解析应答 */
    EVENT_CONTROL_CMD,      /* BLE/App 命令: 组协议帧发出 */
    EVENT_NEED_ACK,         /* 协议要求: 回 ACK 握手帧 */
    EVENT_SCAN_AC,        /* 上电扫描: 轮询AC品牌匹配 */
} event_type_t;

typedef struct {
    event_type_t  type;
    uint8_t      *data;
    uint16_t      len;
    uint8_t       cmd_val;
    uint8_t       cmd_arg;
} event_t;
```

---

## 3. 统一事件表 (框架定义, 品牌填空)

```c
typedef struct {
    void (*on_periodic_send)(void *ctx);
    void (*on_rx_frame)     (void *ctx, uint8_t *data, uint16_t len);
    void (*on_control_cmd)  (void *ctx, uint8_t cmd, uint8_t val);
    void (*on_need_ack)     (void *ctx);
    void (*on_scan)    (void *ctx);   /* 扫描: 发品牌识别帧 */
} event_handler_t;
```

框架调之前判空: `if (table->on_need_ack) table->on_need_ack(ctx);`

---

## 4. 空调设备基类

```c
typedef struct {
    uint8_t temp_min, temp_max;
    uint8_t fan_levels;
    uint8_t mode_mask;      /* bit0=制冷 bit1=制热 ... */
    uint8_t has_swing, has_sleep, has_eco;
} ac_ability_t;

typedef struct {
    uint8_t power, mode, set_temp, room_temp;
    uint8_t fan, swing, sleep, error_code;

    const ac_ability_t *ability;
    const event_handler_t *evt_table;
    bus_controller_t      *bus;
    
    TimerHandle_t  poll_timer;
    uint16_t       poll_period_ms;
} ac_device_t;
```

---

## 5. 总线控制器

```c
typedef struct {
    phy_driver_t   *phy;
    ring_t          tx_queue;           /* 待发帧队列 */
    TaskHandle_t    tx_task;            /* TX 任务句柄 */
    packetizer_t   *rx_pkt;             /* RX 封包器 */
    ac_device_t    *ac;

    uint8_t         busy;
    uint8_t         idle;
    uint32_t        gap_until;
} bus_controller_t;
```

总线忙/空闲检测:

```c
/* 总线收到或发完一字节 → 重置 gap 倒计时 → 标记忙 */
void bus_touch(bus_controller_t *bus) {
    bus->gap_until = ticks_now + bus->gap_ms;
    bus->idle      = 0;
    bus->busy      = 1;
}

/* 总线 tick (ISR 尾部调用) */
void bus_idle_check(bus_controller_t *bus) {
    if (bus->busy && ticks_now >= bus->gap_until) {
        bus->busy = 0;
        bus->idle = 1;
    }
}
```

检测逻辑:
- 总线上有动作 (收/发) → `bus_touch` 重置 idle 计时
- 连续 `gap_ms` 没动作 → `bus_idle_check` 标记空闲 → 投 `EVENT_BUS_IDLE`

总线事件表固定:

```c
void bus_on_rx_byte(void *ctx, uint8_t byte);      /* ring_put + pkt */
void bus_on_tx_done(void *ctx);                     /* 下一字节 / 完成 */
void bus_on_rx_frame(void *ctx, uint8_t *d, uint16_t n); /* → ac */
void bus_on_timeout(void *ctx);                     /* 重发 / 报错 */
```

---

## 6. 品牌注册 (格力示例)

```c
typedef struct {
    ac_device_t    base;

    /* 协议映射表: 抽象值 → 协议字节 */
    const uint8_t *temp_table;      /* [16..30] → 协议温度字节 */
    const uint8_t *mode_table;      /* [COOL..AUTO] → 协议模式字节 */
    const uint8_t *fan_table;       /* [1..N] → 协议风量字节 */
    const uint8_t *swing_table;     /* [0,1] → 协议摆风字节 */
} ac_gree_t;

/* 格力映射表 */
static const uint8_t gree_temp[]  = { 0x10,0x11,0x12,...,0x1E };  /* 16~30 */
static const uint8_t gree_mode[]  = { 0x02,0x04,0x01,0x08,0x00 }; /* 冷热送除自 */
static const uint8_t gree_fan[]   = { 0x01,0x02,0x03,0x00 };       /* 低中高自 */
static const uint8_t gree_swing[] = { 0x00,0x0F };                 /* 关开 */

static const event_handler_t gree_table = {
    .on_periodic_send = gree_on_poll_send,
    .on_rx_frame      = gree_on_resp_parse,
    .on_control_cmd   = gree_on_ble_cmd,
    .on_need_ack      = NULL,                       /* 格力不需要 ACK 握手 */
};

/* ac_device_t 只有两张表: cap + evt_table */
```

---

## 7. 事件流转

```
ISR                             队列                      任务
──                              ──                       ──

RX 任务
  UART ISR → ring_put + packetizer → 帧完成
           → on_rx_frame → parse → BLE 回传
             需要 ACK → 直接调 on_need_ack

定时器回调
  tick → on_periodic_send → 组查询帧 → bus_enqueue

BLE 回调
  App 命令 → on_control_cmd → 组协议帧 → bus_enqueue

扫描
  上电 → on_scan → 发识别帧 → bus_enqueue

TX 任务
  等帧队列 + bus_idle → 逐字节 phy_write
```

---

## 8. 任务模型

```
TX 任务   (P2): 等帧队列 + 总线空闲 → 逐字节发送
RX 任务   (P2): UART 字节 → ring_put + 组帧 → 直接调 on_rx_frame
BLE 任务  (P0): TMOS/SDK 自带 (回调直接调 on_control_cmd)
```

帧队列深度: 8

---

## 9. 定时器策略

| 层 | 定时器 | 精度 |
|---|---|---|
| packetizer 字节间隔 | 硬件定时器 | us 级 |
| AC 定时查询 / 超时 | FreeRTOS 软定时器 | ms 级 |

周期可运行时修改:

```c
ac->poll_period_ms = 500;    /* 发命令中: 快速发送 */
ac->poll_period_ms = 5000;   /* 空闲: 正常 */
xTimerChangePeriod(ac->poll_timer, pdMS_TO_TICKS(ac->poll_period_ms), 0);
```

---

## 10. 新增品牌

```
1. 新建 ac_xxx.c
2. 填 ac_ability_t (能力范围)
3. 填 event_handler_t 四事件 (品牌协议逻辑)
4. 扫描表加一行
```

框架层不改。
