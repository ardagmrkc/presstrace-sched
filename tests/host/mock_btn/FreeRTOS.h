#ifndef MOCK_BTN_FREERTOS_H
#define MOCK_BTN_FREERTOS_H
#include <stddef.h> /* gercek FreeRTOS.h gibi: NULL ve size_t buradan (glibc stdint.h vermez) */
#include <stdint.h>
typedef long BaseType_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define traceISR_ENTER()
#define portYIELD_FROM_ISR(x) ((void)(x))
#endif
