# HVAC 多品牌网关 —— 架构设计

## 1. 整体分层

```
┌─────────────────────────────────────────────┐
│  AC 任务    事件队列 → 派发品牌事件表          │
│             BLE 命令 / 帧解析 / 定时轮询 / ACK │
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

## 2. 四种基础事件

```c
typedef enum {
    EVENT_PERIODIC_SEND,    /* 定时器到期: 发轮询帧 */
    EVENT_RX_FRAME,         /* 封包器帧完成: 解析应答 */
    EVENT_CONTROL_CMD,      /* BLE/App 命令: 组协议帧发出 */
    EVENT_NEED_ACK,         /* 协议要求: 回 ACK 握手帧 */
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
} ac_capability_t;

typedef struct {
    int (*on)        (void *self);
    int (*off)       (void *self);
    int (*set_mode)  (void *self, uint8_t mode);
    int (*set_temp)  (void *self, uint8_t temp);
    int (*set_fan)   (void *self, uint8_t level);
    int (*set_swing) (void *self, uint8_t on_off);
    int (*set_sleep) (void *self, uint8_t on_off);
    int (*poll)      (void *self);
    int (*parse)     (void *self, uint8_t *data, uint16_t len);
} ac_ops_t;

typedef struct {
    uint8_t power, mode, set_temp, room_temp;
    uint8_t fan, swing, sleep, error_code;

    const ac_capability_t *cap;
    const ac_ops_t        *ops;
    const event_handler_t *evt_table;
    bus_controller_t      *bus;
} ac_device_t;
```

---

## 5. 总线控制器

```c
typedef struct {
    phy_driver_t   *phy;
    QueueHandle_t   evt_queue;
    packetizer_t   *rx_pkt;
    TaskHandle_t    task;
    ac_device_t    *ac;

    uint8_t         busy;               /* 1=正在收发 */
    uint8_t         idle;               /* 1=总线空闲可发送 */
    uint32_t        gap_until;          /* 收发间隔保护 ticks */
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

/* 总线 tick (被定时器轮询或 ISR 尾部调用) */
void bus_idle_check(bus_controller_t *bus) {
    if (bus->busy && ticks_now >= bus->gap_until) {
        bus->busy = 0;
        bus->idle = 1;
        /* 通知 AC: 总线空闲, 可以发下一条了 */
        event_t ev = { .type = EVENT_BUS_IDLE };
        xQueueSend(bus->evt_queue, &ev, 0);
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

    /* 协议映射 */
    uint8_t        temp_offset;
    const uint8_t *mode_map;

    /* 定时器 */
    TimerHandle_t  poll_timer;
    uint16_t       poll_period_ms;
} ac_gree_t;

static const event_handler_t gree_table = {
    .on_periodic_send = gree_on_poll_send,
    .on_rx_frame      = gree_on_resp_parse,
    .on_control_cmd   = gree_on_ble_cmd,
    .on_need_ack      = NULL,                       /* 格力不需要 ACK 握手 */
};

static const ac_ops_t gree_ops = {
    .on        = gree_on,
    .off       = gree_off,
    .set_mode  = gree_set_mode,
    .set_temp  = gree_set_temp,
    .set_fan   = gree_set_fan,
    .set_swing = gree_set_swing,
    .set_sleep = NULL,                              /* 格力不支持睡眠 */
    .poll      = gree_poll,
    .parse     = gree_parse_resp,
};
```

---

## 7. 事件流转

```
ISR                             队列                      任务
──                              ──                       ──

UART_RX
  ring_put                  → EVENT_RX_FRAME    → AC 任务:
  + packetizer                                        parse → BLE 回传
                                                     需要 ACK:
                                                      → EVENT_NEED_ACK → 组 ACK 帧

定时器 tick               → EVENT_PERIODIC_SEND  → AC 任务:
                                                      组轮询帧 → bus_send

BLE 回调                  → EVENT_CONTROL_CMD   → AC 任务:
  App SET_TEMP=26                                     组格力帧 → bus_send
```

---

## 8. 任务模型

```
AC 任务   (P3): 等事件队列 → 表派发
总线任务  (P2): 等事件队列 → 收发控制
BLE 任务  (P0): TMOS/SDK 自带
```

事件队列深度: 8

---

## 9. 定时器策略

| 层 | 定时器 | 精度 |
|---|---|---|
| packetizer 字节间隔 | 硬件定时器 | us 级 |
| AC 轮询 / 超时 | FreeRTOS 软定时器 | ms 级 |

周期可运行时修改:

```c
ac->poll_period_ms = 500;    /* 发命令中: 快轮询 */
ac->poll_period_ms = 5000;   /* 空闲: 正常 */
xTimerChangePeriod(ac->poll_timer, pdMS_TO_TICKS(ac->poll_period_ms), 0);
```

---

## 10. 新增品牌

```
1. 新建 ac_xxx.c
2. 填 ac_capability_t (能力范围)
3. 填 ac_ops_t (虚表)
4. 填 event_handler_t (事件表)
5. 实现 brand_build / brand_parse (协议映射)
6. 扫描表加一行
```

框架层不改。
