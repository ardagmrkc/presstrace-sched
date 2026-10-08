#include "stats.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

static uint32_t s_tx_drop;
static uint32_t s_tx_queue_max;

static drop_record_t s_droplog[DROPLOG_SIZE];
static uint32_t s_droplog_count;
static uint32_t s_droplog_overflow;

void stats_window_reset(void)
{
    taskENTER_CRITICAL();
    s_tx_drop = 0;
    s_tx_queue_max = 0;
    s_droplog_count = 0;
    s_droplog_overflow = 0;
    taskEXIT_CRITICAL();
}

void stats_record_tx_drop(void)
{
    taskENTER_CRITICAL();
    s_tx_drop++;
    taskEXIT_CRITICAL();
}

void stats_note_tx_depth(uint32_t depth)
{
    taskENTER_CRITICAL();
    if (depth > s_tx_queue_max)
    {
        s_tx_queue_max = depth;
    }
    taskEXIT_CRITICAL();
}

uint32_t stats_get_tx_drop(void)
{
    uint32_t v;
    taskENTER_CRITICAL();
    v = s_tx_drop;
    taskEXIT_CRITICAL();
    return v;
}

uint32_t stats_get_tx_queue_max(void)
{
    uint32_t v;
    taskENTER_CRITICAL();
    v = s_tx_queue_max;
    taskEXIT_CRITICAL();
    return v;
}

/* Cagiran kritik bolumdeyken kullanilir. */
static void droplog_put(const drop_record_t *r)
{
    if (s_droplog_count < DROPLOG_SIZE)
    {
        s_droplog[s_droplog_count++] = *r;
    }
    else
    {
        s_droplog_overflow++;
    }
}

void stats_droplog_add_from_isr(uint16_t event_id, uint8_t scenario_id, uint32_t t0)
{
    drop_record_t r = { .event_id = event_id, .scenario_id = scenario_id,
                        .status = REC_STATUS_BTN_DROP, .known = 1U, .t = { t0, 0U, 0U } };
    UBaseType_t mask = taskENTER_CRITICAL_FROM_ISR();
    droplog_put(&r);
    taskEXIT_CRITICAL_FROM_ISR(mask);
}

void stats_droplog_add(uint16_t event_id, uint8_t scenario_id, uint32_t t0, uint32_t t1, uint32_t t2)
{
    drop_record_t r = { .event_id = event_id, .scenario_id = scenario_id,
                        .status = REC_STATUS_TX_DROP, .known = 3U, .t = { t0, t1, t2 } };
    taskENTER_CRITICAL();
    droplog_put(&r);
    taskEXIT_CRITICAL();
}

uint32_t stats_droplog_count(void)
{
    uint32_t v;
    taskENTER_CRITICAL();
    v = s_droplog_count;
    taskEXIT_CRITICAL();
    return v;
}

bool stats_droplog_get(uint32_t index, drop_record_t *out)
{
    bool ok = false;
    taskENTER_CRITICAL();
    if (index < s_droplog_count)
    {
        *out = s_droplog[index];
        ok = true;
    }
    taskEXIT_CRITICAL();
    return ok;
}

uint32_t stats_droplog_overflow(void)
{
    uint32_t v;
    taskENTER_CRITICAL();
    v = s_droplog_overflow;
    taskEXIT_CRITICAL();
    return v;
}
