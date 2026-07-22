#include <stdint.h>

uint16_t timer1_init(void)//初始化定时器1，返回 tick 周期（us）
{
    /* TODO: 填入平台定时器初始化代码，返回真实 tick 周期
     * 例如 STM32: TIM1 72MHz / 7200 预分频 → tick = 100us → return 100
     */
    return 1000;  /* 默认 1ms */
}

void timer1_start(void)//只写定时器1的启动
{

}

void timer1_stop(void)//只写定时器1的暂停
{

}

void timer1_restart(void) //只写定时器1的清空定时器的计数值
{
    
}

void timer1_clear_isr_flag(void)//只写定时器1的清空定时中断标志位
{

    
}
