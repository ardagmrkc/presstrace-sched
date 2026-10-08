#ifndef MOCK_TASK_H
#define MOCK_TASK_H
#include "FreeRTOS.h"
typedef struct { uint32_t start_us; } TimeOut_t;
typedef enum { eNoAction = 0, eSetBits } eNotifyAction;
BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t v, eNotifyAction a, BaseType_t *w);
BaseType_t xTaskNotifyWait(uint32_t clr_entry, uint32_t clr_exit, uint32_t *val, TickType_t ticks);
uint32_t ulTaskNotifyValueClear(TaskHandle_t t, uint32_t bits);
void vTaskSetTimeOutState(TimeOut_t *to);
BaseType_t xTaskCheckForTimeOut(TimeOut_t *to, TickType_t *remaining);
#endif
