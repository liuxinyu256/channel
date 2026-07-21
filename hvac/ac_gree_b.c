/**
 * ac_gree_b.c —— 格力 Model B (复用 A 事件表, 只改能力)
 *
 * B 和 A 协议帧完全一样, B 温度下限 10, 多了 ECO。
 * 表格复用: extern gree_table (来自 ac_gree.c)
 */

#include "ac.h"
#include <string.h>

extern const event_handler_t gree_table;

static const ac_ability_t ability = {
    .temp_min=10,.temp_max=30,.fan_levels=3,.mode_mask=0x1F,
    .has_swing=1,.has_sleep=1,.has_eco=1,
};

typedef struct { ac_device_t base; } ac_gree_b_t;
static ac_gree_b_t g_inst;

ac_device_t *ac_gree_b_create(void) {
    memset(&g_inst,0,sizeof(g_inst));
    ac_init(&g_inst.base,NULL,&ability,&gree_table,5000);
    return &g_inst.base;
}
