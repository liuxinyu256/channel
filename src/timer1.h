#ifndef __timer1_h
#define __timer1_h

uint16_t timer1_init(void); //初始化定时器1，返回 tick 周期（us）

void timer1_start(void);//只写定时器1的启动

void timer1_stop(void);//只写定时器1的暂停

void timer1_restart(void); //只写定时器1的清空定时器的计数值

void timer1_clear_isr_flag(void);//只写定时器1的清空定时中断标志位


#endif
