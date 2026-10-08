#ifndef MOCK_TXQ_FREERTOS_H
#define MOCK_TXQ_FREERTOS_H
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define configASSERT(x) assert(x)
extern int g_crit_depth;
#define taskENTER_CRITICAL() do { g_crit_depth++; } while (0)
#define taskEXIT_CRITICAL() do { assert(g_crit_depth > 0); g_crit_depth--; } while (0)
#endif
