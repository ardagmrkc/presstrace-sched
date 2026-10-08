#ifndef MOCK_TXQ_SEMPHR_H
#define MOCK_TXQ_SEMPHR_H
#include "FreeRTOS.h"

/* Sayacli semafor. Bekleme yoktur: sonsuz beklemede sayac 0 ise test
 * kilitlenmek yerine assert ile durur. */
typedef struct
{
    UBaseType_t count, max;
} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t max, UBaseType_t init,
                                                               StaticSemaphore_t *s)
{
    *s = (StaticSemaphore_t){ init, max };
    return s;
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait)
{
    if (s->count == 0)
    {
        assert(wait != portMAX_DELAY); /* gercek sistemde gorev burada bloklanirdi */
        return pdFAIL;
    }
    s->count--;
    return pdPASS;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    if (s->count == s->max)
    {
        return pdFAIL;
    }
    s->count++;
    return pdPASS;
}

static inline BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t s, BaseType_t *w)
{
    (void)w;
    return xSemaphoreTake(s, 0);
}

static inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *w)
{
    (void)w;
    return xSemaphoreGive(s);
}

#define uxSemaphoreGetCount(s) ((s)->count)
#endif
