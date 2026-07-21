#define FAKE_FREERTOS
/**
 * ac_mock.c —— 模拟空调品牌 (无硬件依赖, 测试用)
 *
 * 在 AC 任务里直接回假帧模拟硬件应答。
 * 扫描: 回型号 0x01
 * 周期: AC 任务直接投 RX_FRAME 模拟设备应答
 */

#include "ac.h"
#include <string.h>

typedef struct { ac_device_t base; } ac_mock_t;
static ac_mock_t g_inst;

static const ac_ability_t ability = {
    .temp_min=18,.temp_max=30,.fan_levels=3,.mode_mask=0x1F,
    .has_swing=1,.has_sleep=1,.has_eco=1,
};

static void ev_periodic(void *ctx) {
    ac_device_t *ac=(ac_device_t *)ctx;
    uint8_t resp[]={0xBB,ac->power,ac->mode,ac->set_temp,25,ac->fan,ac->swing,0xCC};
    event_t ev={.type=EVENT_RX_FRAME,.data=resp,.len=sizeof(resp)};
    ac_post(ac,&ev);
}
static void ev_rx_frame(void *ctx, uint8_t *d, uint16_t n) {
    ac_device_t *ac=(ac_device_t *)ctx;
    if(n<8||d[0]!=0xBB||d[7]!=0xCC)return;
    ac->power=d[1];ac->mode=d[2];ac->set_temp=d[3];
    ac->room_temp=d[4];ac->fan=d[5];ac->swing=d[6];
}
static void ev_control(void *ctx, uint8_t cmd, uint8_t val) {
    ac_device_t *ac=(ac_device_t *)ctx;
    switch(cmd){case 0:ac->set_temp=val;break;case 1:ac->mode=val;break;case 2:ac->fan=val;break;case 3:ac->swing=val;break;}
    ev_periodic(ctx);  /* 立即回模拟 ACK */
}
static void ev_ack(void *ctx){(void)ctx;}
static void ev_scan(void *ctx) {
    ac_device_t *ac=(ac_device_t *)ctx;
    uint8_t resp[]={0xBB,0x01,0x00,0xCC};
    event_t ev={.type=EVENT_RX_FRAME,.data=resp,.len=4};
    ac_post(ac,&ev);
}

static const event_handler_t table={
    .on_periodic_send=ev_periodic,.on_rx_frame=ev_rx_frame,
    .on_control_cmd=ev_control,.on_need_ack=ev_ack,.on_scan=ev_scan,
};

ac_device_t *ac_mock_create(void) {
    memset(&g_inst,0,sizeof(g_inst));
    ac_init(&g_inst.base,NULL,&ability,&table,2000);
    return &g_inst.base;
}
