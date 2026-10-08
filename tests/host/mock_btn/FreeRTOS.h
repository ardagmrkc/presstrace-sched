#ifndef MOCK_BTN_FREERTOS_H
#define MOCK_BTN_FREERTOS_H
#include <stdint.h>
typedef long BaseType_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define traceISR_ENTER()
#define portYIELD_FROM_ISR(x) ((void)(x))
#endif
