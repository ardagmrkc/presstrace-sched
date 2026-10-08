#include "telemetry_task.h"
#include "main.h"
#include "protocol.h"
#include "timestamp.h"
#include "workload.h"
#include "stats.h"
#include "temp_sensor.h"
#include "command.h"
#include "tx_queue.h"
#include "button_task.h"
#include "app_trace.h"
#include "task.h"

static TaskHandle_t s_handle;
static StackType_t s_task_stack[STACK_WORDS_TELEMETRY_TASK];
static StaticTask_t s_task_tcb;

/* Pencere istatistikleri: yalnizca bu gorev yazar. */
static telemetry_stats_t s_stats;
static uint32_t s_last_start_us;
static bool s_have_last;

static void tel_stats_reset(void)
{
    s_stats = (telemetry_stats_t){ .min_us = UINT32_MAX };
    s_have_last = false;
}

void telemetry_get_stats(telemetry_stats_t *out)
{
    *out = s_stats;
}

/* Kontrol mesajlari (ACK, havuz sifirlama, dokum) BLOKLAYARAK kuyruga
 * konur: kaybolmalari pencerelerin karismasi demektir. Yalnizca kullanici
 * bir secim yaptiginda olur; UartTxTask kuyrugu bosalttikca ilerler. */
static void send_control(msg_type_t type)
{
    tx_message_t msg = { .type = type };
    msg.scenario_id = g_scenario.id;
    if (type == MSG_ACK)
    {
        msg.u.ack.variant = g_variant;
        msg.u.ack.fast_path = (BTN_FAST_PATH != 0);
        msg.u.ack.trace = (USE_SYSVIEW != 0);
    }
    (void)tx_queue_send(&msg, portMAX_DELAY);
}

/* Yer istasyonu komutu (command.c):
 *   0..6          senaryoyu degistir = yeni olcum penceresi (havuz bosalir)
 *   CMD_NOTIFY_VARIANT | surum
 *                 surumu degistir (ButtonTask onceligi + TX secim kurali);
 *                 senaryo ayni kalir, yeni olcum penceresi baslar
 *   CMD_ARG_DUMP  olcumu bitir: telemetri susar (S0), havuz dokulur
 *   CMD_ARG_QUERY yalnizca sorgu
 * Her durumda aktif senaryo ve surum ACK ile bildirilir. Sira FIFO'da
 * korunur: sifirlama ACK'ten once, dokum ACK'ten sonra islenir. */
static void handle_command(uint32_t arg)
{
    if ((arg & CMD_NOTIFY_VARIANT) != 0U)
    {
        const char v = (char)(arg & 0xFFU);
        g_variant = v;
        button_task_set_priority(PRIO_BUTTON_TASK_FOR(v));
        tel_stats_reset();
        APP_TRACE_WINDOW(g_variant, g_scenario.id);
        send_control(MSG_CMD_POOL_RESET);
        send_control(MSG_ACK);
    }
    else if (arg < SCENARIO_COUNT)
    {
        g_scenario = g_scenarios[arg];
        tel_stats_reset();
        APP_TRACE_WINDOW(g_variant, g_scenario.id);
        send_control(MSG_CMD_POOL_RESET);
        send_control(MSG_ACK);
    }
    else if (arg == CMD_ARG_DUMP)
    {
        /* Telemetri durur (S0); pencere istatistikleri dokum icin korunur. */
        g_scenario = g_scenarios[0];
        send_control(MSG_ACK);
        send_control(MSG_CMD_DUMP);
    }
    else
    {
        send_control(MSG_ACK);
    }
}

static void send_telemetry(void)
{
    /* Gercek uretim periyodu: ardisik iki periyodun baslangici arasi. */
    const uint32_t start_us = timestamp_now_us();
    uint32_t period_us = 0;
    if (s_have_last)
    {
        period_us = start_us - s_last_start_us;
        s_stats.periods++;
        s_stats.sum_us += period_us;
        if (period_us < s_stats.min_us)
        {
            s_stats.min_us = period_us;
        }
        if (period_us > s_stats.max_us)
        {
            s_stats.max_us = period_us;
        }
    }
    s_last_start_us = start_us;
    s_have_last = true;

    const uint32_t workload_iters =
        (g_scenario.extra_load_ms == 0U) ? 0U :
        (g_scenario.extra_load_ms <= 2U) ? WORKLOAD_ITERS_2MS : WORKLOAD_ITERS_5MS;

    uint32_t extra_load_us = 0;
    if (workload_iters > 0U)
    {
        uint32_t t_start = timestamp_now_us();
        (void)workload_run(workload_iters);
        extra_load_us = timestamp_now_us() - t_start;
    }

    temp_sample_t temp;
    temp_sensor_poll(&temp);

    /* S6 (kontrollu darbe): ayni periyotta art arda birden cok TEL; ortalama
     * yuk dusukken kisa sureli kuyruk birikmesi olusturur. */
    for (uint8_t i = 0U; i < g_scenario.tel_per_period; i++)
    {
        tx_message_t msg = { .type = MSG_TEL };
        msg.scenario_id = g_scenario.id;
        msg.u.tel.temp_centi_c = temp.temp_centi_c;
        msg.u.tel.temp_raw = temp.raw;
        msg.u.tel.vdda_mv = temp.vdda_mv;
        msg.u.tel.extra_load_us = extra_load_us;
        msg.u.tel.period_us = period_us;
        msg.u.tel.txq_depth = (uint8_t)tx_queue_waiting();

        /* Bloklamayan gonderim: telemetri periyodikligi, dolu bir TX
         * kuyrugu yuzunden asla gecikmemelidir. Basarisiz olursa bu ornek
         * dusurulur ve pencere sayaclarina (tel_drop, tx_queue_drop) yazilir. */
        msg.u.tel.enq_us = timestamp_now_us();
        if (tx_queue_send(&msg, 0))
        {
            s_stats.sent++;
            stats_note_tx_depth(tx_queue_waiting());
        }
        else
        {
            s_stats.dropped++;
            stats_record_tx_drop();
        }
    }
}

static void telemetry_task(void *arg)
{
    (void)arg;
    uint32_t cmd;

    /* Tek seferlik ~0,2 ms'lik baslatma; surekli yuk degildir. Donusumler
     * yalnizca telemetri acikken (S1-S6) yapilir. */
    temp_sensor_init();
    tel_stats_reset();
    send_control(MSG_ACK); /* acilis senaryosunu bildir */

    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        if (g_scenario.telemetry_hz == 0U)
        {
            /* S0: periyodik is yok, yalnizca bir komut beklenir. */
            (void)xTaskNotifyWait(0U, UINT32_MAX, &cmd, portMAX_DELAY);
            handle_command(cmd);
            last_wake = xTaskGetTickCount();
            continue;
        }

        /* Komut yalnizca periyot sinirinda uygulanir: bir periyot icindeki
         * is hic yarida kesilmez. Degisiklikte periyot yeniden baslar. */
        if (xTaskNotifyWait(0U, UINT32_MAX, &cmd, 0) == pdTRUE)
        {
            handle_command(cmd);
            last_wake = xTaskGetTickCount();
            if (g_scenario.telemetry_hz == 0U)
            {
                continue;
            }
        }

        send_telemetry();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000U / g_scenario.telemetry_hz));
    }
}

void telemetry_task_create(void)
{
    s_handle = xTaskCreateStatic(telemetry_task, "TelemetryTask", STACK_WORDS_TELEMETRY_TASK,
                                 NULL, PRIO_TELEMETRY_TASK, s_task_stack, &s_task_tcb);
    configASSERT(s_handle != NULL);
    command_init(s_handle);
}

UBaseType_t telemetry_task_stack_free(void)
{
    return uxTaskGetStackHighWaterMark(s_handle);
}
