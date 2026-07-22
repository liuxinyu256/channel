/**
 * ac_gree.c —— 格力空调协议驱动
 */

#include "ac.h"
#include "bus.h"
#include "packet.h"
#include "packetizer_timeout.h"
#include "frame_timer.h"
#include <string.h>

static uint8_t g_scanning;

typedef struct {
    ac_device_t    base;
    packetizer_t  *rx_pkt;
    frame_timer_t *timer;
    uint8_t        temp_base;
} ac_gree_t;

static ac_gree_t g_inst;

static const ac_ability_t ability = {
    .temp_min=16,.temp_max=30,.fan_levels=3,.mode_mask=0x1F,
    .has_swing=1,.has_sleep=1,.has_eco=0,
};

static const uint8_t mode_tbl[]={0x02,0x04,0x01,0x08,0x00};
static const uint8_t fan_tbl[] ={0x01,0x02,0x03,0x00};

static uint8_t build(uint8_t cmd, uint8_t val, uint8_t *f) {
    uint8_t s=0; f[0]=0xAA; f[1]=0x01;
    switch(cmd){
    case 0:f[2]=0x10;f[3]=g_inst.temp_base+val;break;
    case 1:f[2]=0x11;f[3]=mode_tbl[val];break;
    case 2:f[2]=0x12;f[3]=fan_tbl[val];break;
    case 3:f[2]=0x13;f[3]=val?0x0F:0x00;break;
    default:return 0;
    }
    int i;for(i=0;i<4;i++)s+=f[i];f[4]=~s;f[5]=0x55;return 6;
}

static void on_frame(uint8_t *d, uint16_t n) {
    BaseType_t w=pdFALSE;
    event_t ev={.type=EVENT_RX_FRAME,.data=d,.len=n};
    ac_post_from_isr(&g_inst.base,&ev,&w);portYIELD_FROM_ISR(w);
}

static void on_rx_byte(uint8_t b, void *ctx) {
    bus_controller_t *bus=(bus_controller_t *)ctx;
    packetizer_put_byte(g_inst.rx_pkt,b);
    bus_on_rx_byte(bus,b);
}

/* ---- 事件 ---- */
static void ev_periodic(void *ctx){
    ac_device_t *ac=(ac_device_t *)ctx;
    if (g_scanning) {
        /* 扫描中: 发格力识别帧 */
        uint8_t f[]={0xAA,0x01,0xFF,0x00,0x00,0x55};
        bus_send(ac->bus,f,6);
    } else {
        /* 正常: 发查询帧 */
        uint8_t f[]={0xAA,0x01,0x20,0x00,0xDE,0x55};
        bus_send(ac->bus,f,6);
    }
}

static int ev_rx_frame(void *ctx, uint8_t *d, uint16_t n){
    ac_device_t *ac=(ac_device_t *)ctx;
    if(n<6||d[0]!=0xAA||d[5]!=0x55)return 0;

    if (g_scanning) {
        g_scanning = 0;
        ac_set_poll_period(ac,5000);
        return 1;   /* 确认匹配, 触发品牌锁定 */
    }

    if(d[2]==0x20){
        ac->power=d[3]>>7;ac->mode=d[3]&0x0F;ac->set_temp=d[4]>>1;
    }
    return 1;
}

static void ev_control(void *ctx, uint8_t cmd, uint8_t val){
    ac_device_t *ac=(ac_device_t *)ctx;
    uint8_t f[8];uint8_t n=build(cmd,val,f);
    if(n)bus_send(ac->bus,f,n);
}

static void ev_ack(void *ctx){(void)ctx;}
static void ev_scan(void *ctx){
    (void)ctx;
    g_scanning = 1;
}

const event_handler_t gree_table={
    .on_periodic_send=ev_periodic,
    .on_rx_frame=ev_rx_frame,
    .on_control_cmd=ev_control,
    .on_need_ack=ev_ack,
    .on_scan=ev_scan,
};

ac_device_t *ac_gree_create(bus_controller_t *bus, frame_timer_t *timer,
                             uint8_t temp_base){
    g_scanning = 0;
    memset(&g_inst,0,sizeof(g_inst));
    g_inst.temp_base=temp_base;
    ac_init(&g_inst.base,bus,&ability,&gree_table,200);   /* 扫描期 200ms */
    g_inst.rx_pkt=packetizer_timeout_create(timer,5,on_frame);
    g_inst.timer=timer;
    bus->phy->set_rx_cb(bus->phy,on_rx_byte,bus);
    return &g_inst.base;
}
