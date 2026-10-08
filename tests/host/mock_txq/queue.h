#ifndef MOCK_TXQ_QUEUE_H
#define MOCK_TXQ_QUEUE_H
#include <string.h>
#include "FreeRTOS.h"

/* Halka tampon. Gercek kuyruk gibi oge kopyalar; bekleme yoktur. */
typedef struct
{
    uint8_t *storage;
    UBaseType_t len, item_size, head, count;
} StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;

/* Son xQueueReceive cagrisindaki kritik bolum derinligi. */
extern int g_receive_crit_depth;

static inline QueueHandle_t xQueueCreateStatic(UBaseType_t len, UBaseType_t item_size,
                                               uint8_t *storage, StaticQueue_t *q)
{
    *q = (StaticQueue_t){ storage, len, item_size, 0, 0 };
    return q;
}

static inline BaseType_t xQueueSendToBack(QueueHandle_t q, const void *item, TickType_t wait)
{
    (void)wait;
    if (q->count == q->len)
    {
        return pdFAIL;
    }
    memcpy(&q->storage[((q->head + q->count) % q->len) * q->item_size], item, q->item_size);
    q->count++;
    return pdPASS;
}

static inline BaseType_t xQueueSendToBackFromISR(QueueHandle_t q, const void *item, BaseType_t *w)
{
    (void)w;
    return xQueueSendToBack(q, item, 0);
}

static inline BaseType_t xQueueReceive(QueueHandle_t q, void *buf, TickType_t wait)
{
    assert(wait == 0); /* tx_queue.c yalnizca sifir beklemeli alir */
    g_receive_crit_depth = g_crit_depth;
    if (q->count == 0)
    {
        return pdFAIL;
    }
    memcpy(buf, &q->storage[q->head * q->item_size], q->item_size);
    q->head = (q->head + 1) % q->len;
    q->count--;
    return pdPASS;
}
#endif
