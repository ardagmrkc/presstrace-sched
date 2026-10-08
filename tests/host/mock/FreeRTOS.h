#ifndef MOCK_FREERTOS_H
#define MOCK_FREERTOS_H
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define configASSERT(x) assert(x)
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
extern int g_crit_depth;
extern int g_in_isr;
#define taskENTER_CRITICAL() do { assert(!g_in_isr); g_crit_depth++; } while (0)
#define taskEXIT_CRITICAL() do { assert(g_crit_depth > 0); g_crit_depth--; } while (0)
#define portYIELD_FROM_ISR(x) ((void)(x))
#endif
