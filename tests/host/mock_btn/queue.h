#ifndef MOCK_BTN_QUEUE_H
#define MOCK_BTN_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken);
#endif
