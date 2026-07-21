#ifndef FAKE_FREERTOS_H
#define FAKE_FREERTOS_H

#include <stdint.h>
#include <string.h>

typedef void *QueueHandle_t, *TaskHandle_t, *TimerHandle_t;
typedef long  BaseType_t, UBaseType_t, TickType_t;

#define pdFALSE       0
#define pdTRUE        1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0
#define portYIELD_FROM_ISR(x)

extern char g_queue_buf[256];
#define xQueueCreate(n,sz) ((QueueHandle_t)g_queue_buf)
static inline int xQueueSend(void *q,const void *e,int t){(void)q;(void)t;memcpy(q,e,64);return 1;}
static inline int xQueueReceive(void *q,void *e,int t){(void)t;memcpy(e,q,64);return 1;}
static inline int xQueueSendFromISR(void *q,const void *e,long *w){(void)w;memcpy(q,e,64);return 1;}
static inline int xTaskCreate(void(*f)(void*),const char*,int s,void*p,int pr,TaskHandle_t*t){(void)f;(void)s;(void)p;(void)pr;(void)t;return 1;}
static inline int xTaskNotifyGive(TaskHandle_t t){(void)t;return 1;}
static inline void vTaskStartScheduler(void){}
static inline void *pvTimerGetTimerID(TimerHandle_t t){return t;}
static inline int xTimerChangePeriod(TimerHandle_t t,int p,int b){(void)t;(void)p;(void)b;return 1;}

#endif
