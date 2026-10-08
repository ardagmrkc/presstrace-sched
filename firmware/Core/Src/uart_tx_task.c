#include "uart_tx_task.h"
#include "main.h"
#include "uart.h"
#include "button.h"
#include "protocol.h"
#include "timestamp.h"
#include "stats.h"
#include "telemetry_task.h"
#include "button_task.h"
#include "tx_queue.h"
#include "task.h"
#include <string.h>

/*
 * Olcum kaydi havuzu. Tek sahibi bu gorevdir: buton ISR'si, ButtonTask ve
 * UART TC ISR'si havuza hic dokunmaz. Hizli yoldan gonderilen BTN satirinin
 * sonucu TC ISR'sinden MSG_BTN_DONE olarak gelir; yedek yoldaki (ButtonTask)
 * BTN'in kaydini bu gorev kendisi acar ve kapatir; kuyruga giremeyen olaylar
 * stats.c'deki dusen olay kaydina gider. Kayit, olay kimligiyle olcum
 * penceresi boyunca bellekte kapanir; olcum sirasinda hatta sonuc basilmaz
 * (basilan her bayt sonraki olaylarin kuyruk gecikmesini degistirirdi).
 * "Olcumu bitir" komutuyla tek seferde dokulur.
 */
#define RECORD_POOL_SIZE   64U
#define TX_DONE_TIMEOUT_MS 50U /* 64 bayt = 5,56 ms @115200, 2,78 ms @230400; kacan TC gorevi kilitlemesin */

typedef enum
{
    REC_FREE = 0,
    REC_TX_ARMED,  /* t0..t2 yazildi, gonderim kuruluyor, TC bekleniyor */
    REC_CLOSED,    /* status ile kapandi */
} rec_state_t;

typedef struct
{
    uint16_t id;
    uint8_t scenario_id;
    rec_state_t state;
    rec_status_t status;
    uint8_t known; /* gecerli zaman sayisi (t0..) */
    uint32_t t[5];
} measure_record_t;

static StackType_t s_task_stack[STACK_WORDS_UART_TX_TASK];
static StaticTask_t s_task_tcb;
static measure_record_t s_pool[RECORD_POOL_SIZE];

/* Pencere sayaclari: yalnizca bu gorev yazar/okur. Diger modullerin acilistan
 * beri biriken sayaclari icin pencere basindaki deger (base) saklanir. */
static uint32_t s_pool_overflow;
static uint32_t s_tx_timeout;
static uint32_t s_tx_start_fail;
static uint32_t s_encode_error;
static uint32_t s_base_spurious;
static uint32_t s_base_accepted;
static uint32_t s_base_repeat;
static uint32_t s_base_btn_drop;
#if BTN_FAST_PATH
static uint32_t s_base_btn_direct;
static uint32_t s_base_btn_latched;
static uint32_t s_base_btn_fallback;
static uint32_t s_base_btn_result_lost;
#endif

/* TEL bekleme suresi: kuyruga girisi (enq_us) ile hatta baslamasi (t3)
 * arasi; kuyruk sirasi ve hattin bosalmasini bekleme dahildir. */
static uint32_t s_tel_wait_n;
static uint64_t s_tel_wait_sum_us;
static uint32_t s_tel_wait_max_us;

/* TC ISR'si yazar (hizli yol sonucu kuyruga giremedi); gorev okur. */
static volatile uint32_t s_btn_result_lost;

static uint32_t s_tag;

static uint32_t next_tag(void)
{
    if (++s_tag == 0U)
    {
        s_tag = 1U; /* 0 = "ucusta aktarim yok" */
    }
    return s_tag;
}

static void window_reset(void)
{
    memset(s_pool, 0, sizeof(s_pool)); /* REC_FREE = 0 */
    s_pool_overflow = 0;
    s_tx_timeout = 0;
    s_tx_start_fail = 0;
    s_encode_error = 0;
    s_base_spurious = uart_get_spurious_tc();
    s_base_accepted = button_get_accepted_count();
    s_base_repeat = button_get_repeat_count();
    s_base_btn_drop = button_get_drop_count();
#if BTN_FAST_PATH
    s_base_btn_direct = uart_get_btn_direct();
    s_base_btn_latched = uart_get_btn_latched();
    s_base_btn_fallback = button_get_fallback_count();
    s_base_btn_result_lost = s_btn_result_lost;
#endif
    s_tel_wait_n = 0;
    s_tel_wait_sum_us = 0;
    s_tel_wait_max_us = 0;
    stats_window_reset();
}

/* Kayitlar yalnizca pencere basinda toptan bosaltildigi icin ilk bos slot,
 * kronolojik siradaki bir sonraki slottur. Havuz doluysa eski kayit
 * EZILMEZ; olcum dokulene kadar veridir. */
static measure_record_t *pool_alloc(void)
{
    for (uint32_t i = 0; i < RECORD_POOL_SIZE; i++)
    {
        if (s_pool[i].state == REC_FREE)
        {
            return &s_pool[i];
        }
    }
    return NULL;
}

typedef enum
{
    SEND_OK = 0,
    SEND_START_FAIL,
    SEND_TIMEOUT,
    SEND_WRONG_TC,
    SEND_ENCODE_FAIL,
} send_result_t;

/* Hatti sahiplenir, satiri kodlar, gonderir ve kendi TC'sini bekler.
 * Hizli yoldaki bir buton satiri hattaysa ya da mandalda bekliyorsa once o
 * biter. t3 kodlamadan SONRA, baslatma cagrisindan hemen once alinir. Cerceve
 * tamponu bu fonksiyonun yiginindadir ve TC (ya da zaman asiminda iptal)
 * gelene kadar yasar. */
static send_result_t send_frame(const tx_message_t *msg, uint32_t *t3_out, uint32_t *t4_out)
{
    uint8_t frame[PROTOCOL_FRAME_SIZE];
    const uint32_t tag = next_tag();
    uint16_t seq = 0U;

    if (!uart_claim_line(pdMS_TO_TICKS(TX_DONE_TIMEOUT_MS), &seq))
    {
        s_tx_start_fail++;
        return SEND_START_FAIL; /* hat alinamadi: aktarim yok, bildirim beklenmez */
    }
    const size_t len = protocol_encode(msg, seq, frame); /* 64 ya da kisa ikili cerceve */
    if (len == 0U)
    {
        uart_release_line();
        s_encode_error++; /* 63 bayta sigmadi: kesilip gonderilmez */
        return SEND_ENCODE_FAIL;
    }

    const uint32_t t3_us = timestamp_now_us(); /* t3: baslatma cagrisindan hemen once */
    if (t3_out != NULL)
    {
        *t3_out = t3_us;
    }
#if USE_SYSVIEW
    if (msg->type == MSG_BTN)
    {
        APP_TRACE_BTN_TX_START(msg->u.btn.event_id);
        uart_trace_next_btn(msg->u.btn.event_id); /* TC kesmesi BTN TX_TC (t4) yazar */
    }
#endif
    uart_start_claimed(frame, len, tag, protocol_has_seq(msg->type));

    uint32_t t4_us = 0;
    uart_tx_result_t res = uart_wait_tx_done(tag, pdMS_TO_TICKS(TX_DONE_TIMEOUT_MS), &t4_us);
    if (res == UART_TX_TIMEOUT)
    {
        s_tx_timeout++;
        return SEND_TIMEOUT;
    }
    if (res != UART_TX_DONE)
    {
        return SEND_WRONG_TC; /* uart.c sahte TC olarak saydi */
    }
    if (t4_out != NULL)
    {
        *t4_out = t4_us;
    }
    return SEND_OK;
}

static void send_button(const tx_message_t *msg)
{
    measure_record_t *r = pool_alloc();
    if (r == NULL)
    {
        s_pool_overflow++; /* olay yine canli gonderilir ama kaydi tutulamaz */
    }
    else
    {
        r->id = msg->u.btn.event_id;
        r->scenario_id = msg->scenario_id;
        r->t[0] = msg->u.btn.t0;
        r->t[1] = msg->u.btn.t1;
        r->t[2] = msg->u.btn.t2;
        r->known = 3U;
        r->state = REC_TX_ARMED;
    }

    uint32_t t3 = 0;
    uint32_t t4 = 0;
    send_result_t res = send_frame(msg, &t3, &t4);

    if (r == NULL)
    {
        return;
    }
    switch (res)
    {
    case SEND_OK:
        r->t[3] = t3;
        r->t[4] = t4;
        r->known = 5U;
        /* Deney protokolu: zaman asimi 1 s. TC gelmis olsa da R >= 1 s ise timeout. */
        r->status = (t4 - r->t[0] < EXPERIMENT_TIMEOUT_US) ? REC_STATUS_OK : REC_STATUS_TIMEOUT;
        break;
    case SEND_TIMEOUT:
        r->t[3] = t3;
        r->known = 4U;
        r->status = REC_STATUS_TIMEOUT;
        break;
    case SEND_WRONG_TC:
        r->t[3] = t3;
        r->known = 4U;
        r->status = REC_STATUS_TX_ERROR;
        break;
    default: /* SEND_START_FAIL / SEND_ENCODE_FAIL: gonderim hic baslamadi, t3 yok */
        r->status = REC_STATUS_TX_ERROR;
        break;
    }
    r->state = REC_CLOSED; /* kayit kendi olay kimligiyle kapanir */
}

static void send_tel(const tx_message_t *msg)
{
    uint32_t t3 = 0;
    send_result_t res = send_frame(msg, &t3, NULL);

    /* t3 yalnizca aktarim baslatildiysa vardir. */
    if (res == SEND_OK || res == SEND_TIMEOUT || res == SEND_WRONG_TC)
    {
        const uint32_t wait_us = t3 - msg->u.tel.enq_us;
        s_tel_wait_n++;
        s_tel_wait_sum_us += wait_us;
        if (wait_us > s_tel_wait_max_us)
        {
            s_tel_wait_max_us = wait_us;
        }
    }
}

/* Hizli yoldan gonderilmis BTN satirinin sonucunu havuza yazar. Durum TC
 * ISR'sinde belirlenmistir (uart_tx_task_btn_done_from_isr). */
static void record_btn_done(const tx_message_t *msg)
{
    measure_record_t *r = pool_alloc();
    if (r == NULL)
    {
        s_pool_overflow++;
        return;
    }
    r->id = msg->u.rec.event_id;
    r->scenario_id = msg->scenario_id;
    memcpy(r->t, msg->u.rec.t, sizeof(r->t));
    r->known = msg->u.rec.known;
    r->status = msg->u.rec.status;
    r->state = REC_CLOSED;
}

void uart_tx_task_btn_done_from_isr(const uart_btn_result_t *res, BaseType_t *woken)
{
    tx_message_t m = { .type = MSG_BTN_DONE, .scenario_id = res->scenario_id };
    m.u.rec.event_id = res->event_id;
    m.u.rec.known = res->known;
    memcpy(m.u.rec.t, res->t, sizeof(m.u.rec.t));

    if (res->aborted)
    {
        m.u.rec.status = REC_STATUS_TIMEOUT; /* TC gelmedi, aktarim iptal edildi */
    }
    else if (res->known < 5U)
    {
        m.u.rec.status = REC_STATUS_TX_ERROR; /* satir kodlanamadi, baslamadi */
    }
    else
    {
        /* Deney protokolu: zaman asimi 1 s. TC gelmis olsa da R >= 1 s ise timeout. */
        m.u.rec.status = (res->t[4] - res->t[0] < EXPERIMENT_TIMEOUT_US) ? REC_STATUS_OK
                                                                         : REC_STATUS_TIMEOUT;
    }

    if (!tx_queue_send_from_isr(&m, woken))
    {
        s_btn_result_lost++;
    }
}

static void send_cnt(cnt_id_t id, uint32_t value)
{
    tx_message_t m = { .type = MSG_CNT, .scenario_id = g_scenario.id };
    m.u.cnt.id = id;
    m.u.cnt.value = value;
    (void)send_frame(&m, NULL, NULL);
}

/* Telemetri durmus (S0) ve kuyruk bosalmisken cagrilir: once kayitlar,
 * sonra pencere sayaclari, en son END. */
static void pool_dump(void)
{
    uint32_t sent = 0;

    for (uint32_t i = 0; i < RECORD_POOL_SIZE; i++)
    {
        const measure_record_t *r = &s_pool[i];
        if (r->state != REC_CLOSED)
        {
            continue;
        }
        tx_message_t m = { .type = MSG_REC, .scenario_id = r->scenario_id };
        m.u.rec.event_id = r->id;
        m.u.rec.status = r->status;
        m.u.rec.known = r->known;
        memcpy(m.u.rec.t, r->t, sizeof(m.u.rec.t));
        if (send_frame(&m, NULL, NULL) == SEND_OK)
        {
            sent++;
        }
    }

    const uint32_t drops = stats_droplog_count();
    for (uint32_t i = 0; i < drops; i++)
    {
        drop_record_t d;
        if (!stats_droplog_get(i, &d))
        {
            break;
        }
        tx_message_t m = { .type = MSG_REC, .scenario_id = d.scenario_id };
        m.u.rec.event_id = d.event_id;
        m.u.rec.status = d.status;
        m.u.rec.known = d.known;
        memcpy(m.u.rec.t, d.t, sizeof(d.t));
        if (send_frame(&m, NULL, NULL) == SEND_OK)
        {
            sent++;
        }
    }

    send_cnt(CNT_ACCEPTED, button_get_accepted_count() - s_base_accepted);
    send_cnt(CNT_REPEAT, button_get_repeat_count() - s_base_repeat);
    send_cnt(CNT_BTN_QUEUE_DROP, button_get_drop_count() - s_base_btn_drop);
    send_cnt(CNT_TX_QUEUE_DROP, stats_get_tx_drop());
    send_cnt(CNT_TX_QUEUE_MAX, stats_get_tx_queue_max());
    send_cnt(CNT_POOL_OVERFLOW, s_pool_overflow);
    send_cnt(CNT_DROPLOG_OVERFLOW, stats_droplog_overflow());
    send_cnt(CNT_TX_TIMEOUT, s_tx_timeout);
    send_cnt(CNT_TX_START_FAIL, s_tx_start_fail);
    send_cnt(CNT_SPURIOUS_TC, uart_get_spurious_tc() - s_base_spurious);
    send_cnt(CNT_ENCODE_ERROR, s_encode_error);
#if BTN_FAST_PATH
    send_cnt(CNT_BTN_DIRECT, uart_get_btn_direct() - s_base_btn_direct);
    send_cnt(CNT_BTN_LATCHED, uart_get_btn_latched() - s_base_btn_latched);
    send_cnt(CNT_BTN_FALLBACK, button_get_fallback_count() - s_base_btn_fallback);
    send_cnt(CNT_BTN_RESULT_LOST, s_btn_result_lost - s_base_btn_result_lost);
#endif

    telemetry_stats_t ts;
    telemetry_get_stats(&ts);
    send_cnt(CNT_TEL_SENT, ts.sent);
    send_cnt(CNT_TEL_DROP, ts.dropped);
    if (ts.periods > 0U) /* telemetri kapaliysa periyot yok: satir gonderilmez */
    {
        send_cnt(CNT_TEL_PERIOD_MIN_US, ts.min_us);
        send_cnt(CNT_TEL_PERIOD_AVG_US, (uint32_t)(ts.sum_us / ts.periods));
        send_cnt(CNT_TEL_PERIOD_MAX_US, ts.max_us);
    }
    if (s_tel_wait_n > 0U)
    {
        send_cnt(CNT_TEL_WAIT_AVG_US, (uint32_t)(s_tel_wait_sum_us / s_tel_wait_n));
        send_cnt(CNT_TEL_WAIT_MAX_US, s_tel_wait_max_us);
    }

    /* Yiginlarda acilistan beri en az bos kalan alan (word); boyut secimi
     * icin. Ayni olcum penceresinden bagimsizdir, acilistan beri birikir. */
    send_cnt(CNT_STACK_HWM_TELEMETRY, telemetry_task_stack_free());
    send_cnt(CNT_STACK_HWM_BUTTON, button_task_stack_free());
    send_cnt(CNT_STACK_HWM_UART_TX, uxTaskGetStackHighWaterMark(NULL));

    tx_message_t e = { .type = MSG_END, .scenario_id = g_scenario.id };
    e.u.end.records = sent;
    (void)send_frame(&e, NULL, NULL);
}

static void uart_tx_task(void *arg)
{
    (void)arg;

    /* uart_init, ISR'nin bildirim gonderecegi gorevi (bu gorev) bilmelidir;
     * bu yuzden gorev govdesi icinden, kendi handle'imizla cagriyoruz. */
    uart_init(xTaskGetCurrentTaskHandle());
    window_reset();

    tx_message_t msg;
    for (;;)
    {
        tx_queue_receive(&msg); /* C'de bekleyen BTN once gelir (tx_queue.c) */

        switch (msg.type)
        {
        case MSG_CMD_POOL_RESET:
            window_reset();
            break;
        case MSG_CMD_DUMP:
            pool_dump();
            break;
        case MSG_BTN: /* ButtonTask uzerinden gelen basis */
            send_button(&msg);
            break;
        case MSG_BTN_DONE: /* hizli yol: satir zaten gitti, yalnizca kayit */
            record_btn_done(&msg);
            break;
        case MSG_TEL:
            send_tel(&msg);
            break;
        default: /* ACK */
            (void)send_frame(&msg, NULL, NULL);
            break;
        }
    }
}

void uart_tx_task_create(void)
{
    TaskHandle_t handle = xTaskCreateStatic(uart_tx_task, "UartTxTask", STACK_WORDS_UART_TX_TASK,
                                            NULL, PRIO_UART_TX_TASK, s_task_stack, &s_task_tcb);
    configASSERT(handle != NULL);
    (void)handle;
}
