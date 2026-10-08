#include "button_task.h"
#include "main.h"
#include "button.h"
#include "protocol.h"
#include "timestamp.h"
#include "stats.h"
#include "tx_queue.h"
#include "app_trace.h"
#include "task.h"
#include "queue.h"

static TaskHandle_t s_handle;
static QueueHandle_t s_evt_queue;
static StaticQueue_t s_evt_queue_ctrl;
static uint8_t s_evt_queue_items[BUTTON_EVENT_QUEUE_LEN * sizeof(button_event_t)];
static StackType_t s_task_stack[STACK_WORDS_BUTTON_TASK];
static StaticTask_t s_task_tcb;

static void button_task(void *arg)
{
    (void)arg;
    button_event_t evt;

    for (;;)
    {
        if (xQueueReceive(s_evt_queue, &evt, portMAX_DELAY) != pdPASS)
        {
            continue;
        }

        /* t1: ButtonTask olayi aldiktan hemen sonra (olay aktarimi + CPU
         * beklemesi dahil edilmis olur, cunku bu satir schedule edilmeden
         * once ISR->kuyruk->context-switch zinciri tamamlanmis olmalidir). */
        uint32_t t1_us = timestamp_now_us();
        APP_TRACE_BTN_TASK(evt.event_id);

        tx_message_t msg = { .type = MSG_BTN };
        msg.scenario_id = g_scenario.id;
        msg.u.btn.event_id = evt.event_id;
        msg.u.btn.t0 = evt.t0_us;
        msg.u.btn.t1 = t1_us;

        /* t2: TX kuyruguna verilmeden hemen once. */
        msg.u.btn.t2 = timestamp_now_us();
        APP_TRACE_BTN_READY(evt.event_id);

        if (tx_queue_send(&msg, 0))
        {
            const UBaseType_t depth = tx_queue_waiting();
            APP_TRACE_BTN_ENQUEUE(evt.event_id, depth);
            stats_note_tx_depth(depth);
        }
        else
        {
            /* Olcum zinciri burada kirilir, ama olay kaybolmaz: kimligi ve
             * t0..t2 olcum sonunda REC(tx_drop) olarak gonderilir. */
            APP_TRACE_BTN_DROP(evt.event_id, APP_DROP_AT_TX_QUEUE);
            stats_record_tx_drop();
            stats_droplog_add(evt.event_id, msg.scenario_id, msg.u.btn.t0, msg.u.btn.t1,
                              msg.u.btn.t2);
        }
    }
}

void button_task_create(void)
{
    /* Sira: kuyruk -> gorev -> kesme. ISR'nin yazacagi kuyruk, kesme
     * acilmadan once hazir olmalidir. */
    s_evt_queue = xQueueCreateStatic(BUTTON_EVENT_QUEUE_LEN, sizeof(button_event_t),
                                     s_evt_queue_items, &s_evt_queue_ctrl);
    configASSERT(s_evt_queue != NULL);

    s_handle = xTaskCreateStatic(button_task, "ButtonTask", STACK_WORDS_BUTTON_TASK, NULL,
                                 PRIO_BUTTON_TASK_FOR(VARIANT), s_task_stack, &s_task_tcb);
    configASSERT(s_handle != NULL);

    button_init(s_evt_queue); /* GPIO/EXTI kurulur, NVIC en son acilir */
}

void button_task_set_priority(UBaseType_t prio)
{
    vTaskPrioritySet(s_handle, prio);
}

UBaseType_t button_task_stack_free(void)
{
    return uxTaskGetStackHighWaterMark(s_handle);
}
