#include "main.h"
#include "system_clock.h"
#include "timestamp.h"
#include "protocol.h"
#include "telemetry_task.h"
#include "button_task.h"
#include "uart_tx_task.h"
#include "tx_queue.h"
#include "FreeRTOS.h"
#include "task.h"

/* S0..S5 PressTrace serisinin yuk senaryolari; S6 kontrollu darbe deneyi:
 * 100 ms'de 4 TEL. A/B/C olcumleri S5 ve S6 ile alinir. */
const scenario_config_t g_scenarios[SCENARIO_COUNT] = {
    {0, 0,   0, 0}, /* S0: kapali, referans */
    {1, 10,  0, 1}, /* S1: 10 Hz */
    {2, 50,  0, 1}, /* S2: 50 Hz */
    {3, 100, 0, 1}, /* S3: 100 Hz */
    {4, 100, 2, 1}, /* S4: 100 Hz + ~2 ms ek is */
    {5, 100, 5, 1}, /* S5: 100 Hz + ~5 ms ek is */
    {6, 10,  0, 4}, /* S6: 100 ms'de 4 TEL */
};

#if (ACTIVE_SCENARIO < 0) || (ACTIVE_SCENARIO > 6)
#error "ACTIVE_SCENARIO 0..6 araliginda olmalidir (S0..S6)"
#endif

scenario_config_t g_scenario;
volatile char g_variant;

int main(void)
{
    system_clock_config();

    /* 4 oncelik bitinin tamami preemption, alt oncelik yok (HAL'deki
     * NVIC_PRIORITYGROUP_4). FreeRTOS portu bunu varsayar; kesme
     * oncelikleri bu satirdan sonra atanir. */
    NVIC_SetPriorityGrouping(3U);

    timestamp_init();

    /* SystemView (USE_SYSVIEW 1): SEGGER_SYSVIEW_Conf(). Saat ayarindan
     * sonra (zaman damgasi 168 MHz DWT sayaci), gorevlerden once (gorev
     * listesi olusturulurken toplanir). USE_SYSVIEW 0 iken bos makro. */
    traceSTART();

    /* Acilis senaryosu; sonrasinda yer istasyonu komutuyla TelemetryTask
     * degistirir. */
    g_scenario = g_scenarios[ACTIVE_SCENARIO];
    g_variant = VARIANT;

    tx_queue_init();

    telemetry_task_create();
    button_task_create();
    uart_tx_task_create();

    vTaskStartScheduler();

    /* Buraya ulasilmaz: tum bellek statik (heap yok), idle gorevinin
     * bellegini cekirdek saglar ve timer gorevi kapali. */
    for (;;)
    {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
