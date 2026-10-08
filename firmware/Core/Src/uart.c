#include "uart.h"
#include "main.h"
#include "timestamp.h"
#include "command.h"
#include "protocol.h"
#include "uart_tx_task.h"

/*
 * UART'in tek hakemi.
 *
 * Hatta ayni anda tek satir olabilir; satiri kimin baslattigi s_owner'da
 * tutulur: NONE, TASK (UartTxTask: TEL, ACK, REC, CNT, END ve yedek yoldaki BTN)
 * ya da BTN (hizli yol: EXTI0 ISR'sinden ya da TC ISR'sinden baslatilan buton
 * satiri).
 *
 * Neden: S5'te ButtonTask (2) ve UartTxTask (1), TelemetryTask'in (3) 5 ms'lik
 * isi bitmeden CPU alamaz; buton satiri isin sonunu ve o periyodun TEL'ini
 * beklerdi. Gorev oncelikleri degismeden butonun zaman kritik yolu NVIC
 * duzlemine tasindi:
 *  - EXTI0 ISR'si hat bossa buton satirini hemen baslatir, mesgulse mandal
 *    birakir;
 *  - TC ISR'si hat bosalinca once mandaldaki satiri baslatir, gorevi sonra
 *    uyandirir. TEL hicbir zaman bekleyen bir butonun onune gecemez.
 *
 * Tutarlilik: s_owner, mandal, s_btn_air ve s_seq yalnizca su yerlerde
 * okunur/yazilir:
 *  - EXTI0 ve USART2 ISR'leri: ikisinin de NVIC onceligi 5; ayni oncelikteki
 *    kesmeler birbirini kesemez;
 *  - UartTxTask'ta taskENTER_CRITICAL icinde: BASEPRI, oncelik 5..15
 *    kesmelerini, yani bu ikisini de maskeler.
 * "Hat bos mu?" kontrolu ile sahiplenme ayni atomik adimdadir; iki taraf ayni
 * anda aktarim baslatamaz, baytlar karisamaz.
 *
 * seq (TEL/BTN/ACK sira numarasi) da burada, satir hatta baslatilirken artar:
 * hattaki sira ile numara sirasi her zaman aynidir.
 */

typedef enum
{
    OWNER_NONE = 0,
    OWNER_TASK,
    OWNER_BTN,
} owner_t;

#define NOTIFY_TX_DONE   (1UL << 0) /* gorevin kendi aktarimi bitti (TC) */
#define NOTIFY_LINE_FREE (1UL << 1) /* hat bosaldi ve mandalda basis yok */

/* Hizli yolda TC'si gelmeyen bir buton aktarimi bu sureden sonra iptal
 * edilir (64 bayt 230400'de 2,78 ms surer). */
#define BTN_STUCK_US 50000U

typedef struct
{
    bool valid;
    uint16_t event_id;
    uint8_t scenario_id;
    uint32_t t[5];
} btn_slot_t;

static TaskHandle_t s_notify_task = NULL;
static volatile bool s_ready = false;

/* Hakem durumu (yalnizca kilit altinda ya da oncelik 5 ISR'lerinde). */
static volatile owner_t s_owner = OWNER_NONE;
static uint16_t s_seq;
static btn_slot_t s_btn_latch;                   /* hat mesgulken bekleyen tek basis */
static btn_slot_t s_btn_air;                     /* hattaki buton satiri (s_owner == OWNER_BTN) */
static uint8_t s_btn_frame[PROTOCOL_FRAME_SIZE]; /* statik: aktarim ISR'den sonra da surer */

/* TX motoru: TXE ISR'si bayt bayt okur. */
static const uint8_t *volatile s_tx_buf = NULL;
static volatile size_t s_tx_len = 0;
static volatile size_t s_tx_idx = 0;

/* Gorev aktarimi: gorev yazar (baslatmadan once), TC ISR'si okur ve sifirlar. */
static volatile uint32_t s_in_flight_tag = 0; /* 0 = ucusta gorev aktarimi yok */
static volatile uint32_t s_done_tag = 0;
static volatile uint32_t s_done_t4_us = 0;

/* Sayaclar (acilistan beri). */
static volatile uint32_t s_spurious_tc = 0;
static volatile uint32_t s_btn_direct = 0;
static volatile uint32_t s_btn_latched = 0;

#if USE_SYSVIEW
/* SystemView: gorev aktarimi bir BTN satiriysa olay kimligi. Gorev bir
 * sonrakini verir; uart_start_claimed onu aktarimla birlikte ucusa alir,
 * TC kesmesi (ya da iptal) birakir. 0 = BTN degil. */
static uint16_t s_trace_next_btn;
static volatile uint16_t s_trace_btn_in_flight;

void uart_trace_next_btn(uint16_t event_id)
{
    s_trace_next_btn = event_id;
}
#endif

/* fCK: USART2 APB1 uzerinde, 42 MHz (bkz. system_clock.c).
 * Kayan noktali sayi kullanmadan, x1000 sabit noktali aritmetikle
 * mantissa/fraction hesabi (16x oversampling, RM0090 27.3.4). */
static uint16_t compute_brr(uint32_t pclk_hz, uint32_t baud)
{
    uint32_t usartdiv_x1000 = (uint32_t)(((uint64_t)pclk_hz * 1000ULL) / (16ULL * baud));
    uint32_t mantissa = usartdiv_x1000 / 1000U;
    uint32_t frac_x1000 = usartdiv_x1000 - (mantissa * 1000U);
    uint32_t fraction = (frac_x1000 * 16U + 500U) / 1000U;
    if (fraction > 15U)
    {
        mantissa++;
        fraction = 0U;
    }
    return (uint16_t)((mantissa << 4) | (fraction & 0xFU));
}

void uart_init(TaskHandle_t notify_task)
{
    s_notify_task = notify_task;

    /* --- GPIO: PA2=TX, PA3=RX, AF7 (USART2), push-pull, yuksek hiz --- */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    uint32_t tx_pin = UART_TX_GPIO_PIN, rx_pin = UART_RX_GPIO_PIN;

    /* TX pini (PA2) AF'ye USART2 acildiktan SONRA alinir (asagida). */
    UART_RX_GPIO_PORT->MODER = (UART_RX_GPIO_PORT->MODER & ~(0x3UL << (rx_pin * 2U))) |
                               (0x2UL << (rx_pin * 2U)); /* 10 = alternate function */

    UART_TX_GPIO_PORT->OSPEEDR |= (0x3UL << (tx_pin * 2U)); /* very high speed */
    UART_RX_GPIO_PORT->OSPEEDR |= (0x3UL << (rx_pin * 2U));

    UART_TX_GPIO_PORT->OTYPER &= ~(1UL << tx_pin); /* push-pull */
    UART_TX_GPIO_PORT->PUPDR &= ~(0x3UL << (tx_pin * 2U));
    UART_RX_GPIO_PORT->PUPDR = (UART_RX_GPIO_PORT->PUPDR & ~(0x3UL << (rx_pin * 2U))) |
                              (0x1UL << (rx_pin * 2U)); /* pull-up on RX */

    UART_TX_GPIO_PORT->AFR[tx_pin / 8U] =
        (UART_TX_GPIO_PORT->AFR[tx_pin / 8U] & ~(0xFUL << ((tx_pin % 8U) * 4U))) |
        ((uint32_t)UART_GPIO_AF << ((tx_pin % 8U) * 4U));
    UART_RX_GPIO_PORT->AFR[rx_pin / 8U] =
        (UART_RX_GPIO_PORT->AFR[rx_pin / 8U] & ~(0xFUL << ((rx_pin % 8U) * 4U))) |
        ((uint32_t)UART_GPIO_AF << ((rx_pin % 8U) * 4U));

    /* --- USART2: UART_BAUDRATE 8N1 (main.h; su an 230400; 115200
     *     ile de calisir). TX kesme guduml; RX yalnizca yer
     *     istasyonunun senaryo komutu (5 bayt) icin acik. --- */
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    UART_PERIPH->CR1 = 0;
    UART_PERIPH->CR2 = 0; /* 1 stop bit */
    UART_PERIPH->CR3 = 0;
    UART_PERIPH->BRR = compute_brr(42000000UL, UART_BAUDRATE);
    UART_PERIPH->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE |
                       USART_CR1_UE; /* 8N1: M=0, PCE=0 (varsayilan) */

    /* RM0090: verici kapaliyken TX pini port ayarina doner, acikken hat bosta
     * yuksektir. Pin verici acildiktan sonra AF'ye alinir; tersi sirada
     * acilistaki ara seviye yer istasyonunda bozuk bir bayt olarak gorulebilir
     * ve ilk satiri (acilis ACK'i) bozar. */
    UART_TX_GPIO_PORT->MODER = (UART_TX_GPIO_PORT->MODER & ~(0x3UL << (tx_pin * 2U))) |
                               (0x2UL << (tx_pin * 2U));

    NVIC_SetPriority(USART2_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(USART2_IRQn);

    /* Hizli yol en son acilir: bundan once gelen basislar ButtonTask yoluna
     * gider (uart_btn_submit_from_isr -> REJECTED). */
    s_ready = true;
}

/* ---- Kilit altindaki yardimcilar ------------------------------------------
 * "Kilit altinda": oncelik 5 ISR'si icinde ya da taskENTER_CRITICAL ile. */

static void tx_begin_locked(const uint8_t *buf, size_t len)
{
    s_tx_buf = buf;
    s_tx_len = len;
    s_tx_idx = 0;

    /* Bosta TC zaten 1'dir. Bayrak temizlenir ve TCIE ancak son bayt
     * yazildiktan sonra (TXE ISR'si) acilir; aksi halde onceki bosta
     * kalmanin bayragi bu cercevenin t4'u olurdu. */
    UART_PERIPH->SR &= ~USART_SR_TC;
    UART_PERIPH->CR1 |= USART_CR1_TXEIE;
}

/* Hizli yol sonucunu UartTxTask'a iletir (kayit havuzunun tek sahibi o).
 * Gorev baglaminda (kritik bolum icinde) cagrildiginda da FromISR API'si
 * guvenlidir: yalnizca kesme maskesini ic ice kullanir; alici cagiran gorevin
 * kendisi oldugu icin woken yok sayilabilir. */
static void report_btn_locked(const btn_slot_t *b, uint8_t known, bool aborted, BaseType_t *woken)
{
    uart_btn_result_t r = {
        .event_id = b->event_id,
        .scenario_id = b->scenario_id,
        .known = known,
        .aborted = aborted,
    };
    for (uint32_t i = 0; i < 5U; i++)
    {
        r.t[i] = b->t[i];
    }
    uart_tx_task_btn_done_from_isr(&r, woken);
}

/* Mandaldaki basisi hatta baslatir. Kilit altinda ve s_owner == NONE iken
 * cagrilir. Kodlama basarisizsa (olmamali) sonuc tx_error olarak raporlanir
 * ve hat bos kalir. */
static void btn_launch_locked(BaseType_t *woken)
{
    s_btn_air = s_btn_latch;
    s_btn_latch.valid = false;

    tx_message_t m = { .type = MSG_BTN, .scenario_id = s_btn_air.scenario_id };
    m.u.btn.event_id = s_btn_air.event_id;
    m.u.btn.t0 = s_btn_air.t[0];
    m.u.btn.t1 = s_btn_air.t[1];
    m.u.btn.t2 = s_btn_air.t[2];

    const size_t len = protocol_encode(&m, s_seq, s_btn_frame);
    if (len == 0U)
    {
        s_btn_air.valid = false;
        report_btn_locked(&s_btn_air, 3U, false, woken);
        return;
    }
    s_seq++;

    s_btn_air.t[3] = timestamp_now_us(); /* t3: baslatmadan hemen once */
    s_owner = OWNER_BTN;
    tx_begin_locked(s_btn_frame, len);
}

/* Hatti birakir (gorev ya da takilan buton aktarimi). Bekleyen basis varsa
 * gorevden once o baslar. */
static void release_line_locked(BaseType_t *woken)
{
    s_in_flight_tag = 0;
#if USE_SYSVIEW
    s_trace_btn_in_flight = 0U;
#endif
    s_owner = OWNER_NONE;
    if (s_btn_latch.valid)
    {
        btn_launch_locked(woken);
    }
}

/* Hizli yolda TC'si BTN_STUCK_US icinde gelmeyen buton aktarimini iptal eder.
 * Gorev baglamindan (uart_claim_line zaman asiminda) cagrilir. */
static bool btn_recover_stuck(void)
{
    bool recovered = false;
    BaseType_t woken = pdFALSE;

    taskENTER_CRITICAL();
    if (s_owner == OWNER_BTN &&
        (uint32_t)(timestamp_now_us() - s_btn_air.t[3]) >= BTN_STUCK_US)
    {
        UART_PERIPH->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_TCIE);
        s_btn_air.valid = false;
        report_btn_locked(&s_btn_air, 4U, true, &woken);
        release_line_locked(&woken);
        recovered = true;
    }
    taskEXIT_CRITICAL();

    return recovered;
}

/* ---- Gorev yolu ------------------------------------------------------------ */

bool uart_claim_line(TickType_t timeout, uint16_t *seq)
{
    TimeOut_t to;
    TickType_t remaining = timeout;
    bool recovered = false;

    vTaskSetTimeOutState(&to);
    for (;;)
    {
        taskENTER_CRITICAL();
        if (s_owner == OWNER_NONE && !s_btn_latch.valid)
        {
            s_owner = OWNER_TASK;
            *seq = s_seq;
            taskEXIT_CRITICAL();

            /* Onceki aktarimlardan kalmis bildirim bitleri bu aktarimi
             * kapatamasin. Hat bizde oldugu surece ISR bu bitleri yeniden
             * kuramaz: TC yalnizca baslatilmis bir aktarim icin gelir. */
            (void)ulTaskNotifyValueClear(NULL, NOTIFY_TX_DONE | NOTIFY_LINE_FREE);
            return true;
        }
        taskEXIT_CRITICAL();

        if (xTaskCheckForTimeOut(&to, &remaining) != pdFALSE)
        {
            /* Hattaki buton satiri takildiysa bir kez kurtar ve yeniden dene. */
            if (!recovered && btn_recover_stuck())
            {
                recovered = true;
                vTaskSetTimeOutState(&to);
                remaining = timeout;
                continue;
            }
            return false;
        }

        /* Bit kilit disinda kurulmus olsa bile kaybolmaz: bekleme hemen doner. */
        (void)xTaskNotifyWait(0U, NOTIFY_LINE_FREE, NULL, remaining);
    }
}

void uart_start_claimed(const uint8_t *buf, size_t len, uint32_t tag, bool consumes_seq)
{
    taskENTER_CRITICAL();
    configASSERT(s_owner == OWNER_TASK);
    if (consumes_seq)
    {
        s_seq++;
    }
    s_done_tag = 0;
    s_in_flight_tag = tag; /* once ISR'ye yayinla, sonra baslat */
#if USE_SYSVIEW
    s_trace_btn_in_flight = s_trace_next_btn;
    s_trace_next_btn = 0U;
#endif
    tx_begin_locked(buf, len);
    taskEXIT_CRITICAL();
}

void uart_release_line(void)
{
    BaseType_t woken = pdFALSE;

    taskENTER_CRITICAL();
    if (s_owner == OWNER_TASK)
    {
        release_line_locked(&woken);
    }
    taskEXIT_CRITICAL();
}

static void uart_abort_task_tx(void)
{
    BaseType_t woken = pdFALSE;

    taskENTER_CRITICAL();
    /* TC tam bu arada geldiyse hat artik gorevde degildir (belki mandaldaki
     * buton baslamistir): o aktarima dokunulmaz. */
    if (s_owner == OWNER_TASK)
    {
        UART_PERIPH->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_TCIE);
        release_line_locked(&woken);
    }
    taskEXIT_CRITICAL();
}

uart_tx_result_t uart_wait_tx_done(uint32_t tag, TickType_t timeout, uint32_t *t4_us)
{
    TimeOut_t to;
    TickType_t remaining = timeout;

    vTaskSetTimeOutState(&to);
    for (;;)
    {
        uint32_t bits = 0U;
        (void)xTaskNotifyWait(0U, NOTIFY_TX_DONE, &bits, remaining);
        if ((bits & NOTIFY_TX_DONE) != 0U)
        {
            if (s_done_tag != tag)
            {
                s_spurious_tc++;
                return UART_TX_TAG_MISMATCH;
            }
            *t4_us = s_done_t4_us;
            return UART_TX_DONE;
        }
        if (xTaskCheckForTimeOut(&to, &remaining) != pdFALSE)
        {
            uart_abort_task_tx();
            return UART_TX_TIMEOUT;
        }
    }
}

/* ---- Hizli yol ------------------------------------------------------------- */

uart_btn_submit_t uart_btn_submit_from_isr(uint16_t event_id, uint8_t scenario_id,
                                           uint32_t t0_us, BaseType_t *woken)
{
    const uint32_t t1_us = timestamp_now_us(); /* t1: hizli yola giris */

    if (!s_ready || s_btn_latch.valid)
    {
        return UART_BTN_REJECTED;
    }

    s_btn_latch.valid = true;
    s_btn_latch.event_id = event_id;
    s_btn_latch.scenario_id = scenario_id;
    s_btn_latch.t[0] = t0_us;
    s_btn_latch.t[1] = t1_us;
    s_btn_latch.t[3] = 0U;
    s_btn_latch.t[4] = 0U;
    s_btn_latch.t[2] = timestamp_now_us(); /* t2: hat karari oncesi */

    if (s_owner != OWNER_NONE)
    {
        s_btn_latched++;
        return UART_BTN_LATCHED; /* TC ISR'si hat bosalinca baslatir */
    }

    s_btn_direct++;
    btn_launch_locked(woken);
    return UART_BTN_STARTED;
}

/* ---- Sayaclar -------------------------------------------------------------- */

uint32_t uart_get_spurious_tc(void)
{
    return s_spurious_tc;
}

uint32_t uart_get_btn_direct(void)
{
    return s_btn_direct;
}

uint32_t uart_get_btn_latched(void)
{
    return s_btn_latched;
}

/* ---- ISR ------------------------------------------------------------------- */

void uart_isr_handler(void)
{
    uint32_t sr = UART_PERIPH->SR;
    uint32_t cr1 = UART_PERIPH->CR1;
    BaseType_t higher_prio_woken = pdFALSE;
    const bool tc = ((sr & USART_SR_TC) != 0U) && ((cr1 & USART_CR1_TCIE) != 0U);

    /* SystemView: her USART2 kesmesi (TC, TXE, RX) kaydedilir. Giris, TC
     * dalinda t4 alindiktan sonra, digerlerinde hemen yazilir. */

    /* TC once: ayni anda RX bayti da bekliyorsa t4 onun yuzunden gecikmesin. */
    if (tc)
    {
        /* t4: son bit sonrasi ISR gozlem zamani. Ilk is: zaman. */
        const uint32_t t4_us = timestamp_now_us();
#if USE_SYSVIEW
        traceISR_ENTER(); /* zamandan SONRA: t4 izleme yuzunden kaymaz */
#endif
        uint32_t notify = 0U;

        UART_PERIPH->CR1 &= ~USART_CR1_TCIE;
        UART_PERIPH->SR &= ~USART_SR_TC;

        if (s_owner == OWNER_TASK)
        {
            uint32_t tag = s_in_flight_tag; /* bir kez oku */
            s_in_flight_tag = 0;
            if (tag == 0U)
            {
                s_spurious_tc++; /* damga hicbir kayda yazilmaz */
            }
            else
            {
                s_done_t4_us = t4_us;
                s_done_tag = tag;
                notify |= NOTIFY_TX_DONE;
#if USE_SYSVIEW
                if (s_trace_btn_in_flight != 0U)
                {
                    APP_TRACE_BTN_TX_TC(s_trace_btn_in_flight);
                    s_trace_btn_in_flight = 0U;
                }
#endif
            }
        }
        else if (s_owner == OWNER_BTN)
        {
            s_btn_air.t[4] = t4_us;
            s_btn_air.valid = false;
            report_btn_locked(&s_btn_air, 5U, false, &higher_prio_woken);
        }
        else
        {
            s_spurious_tc++; /* ucusta aktarim yok */
        }

        /* Hat bosaldi. Bekleyen basis varsa gorev uyanmadan ONCE o baslar. */
        s_owner = OWNER_NONE;
        if (s_btn_latch.valid)
        {
            btn_launch_locked(&higher_prio_woken);
        }
        if (s_owner == OWNER_NONE)
        {
            notify |= NOTIFY_LINE_FREE;
        }

        if (notify != 0U && s_notify_task != NULL)
        {
            (void)xTaskNotifyFromISR(s_notify_task, notify, eSetBits, &higher_prio_woken);
        }
    }
#if USE_SYSVIEW
    else
    {
        traceISR_ENTER(); /* TXE ve RX */
    }
#endif

    if ((sr & USART_SR_TXE) && (cr1 & USART_CR1_TXEIE))
    {
        if (s_tx_idx < s_tx_len)
        {
            UART_PERIPH->DR = s_tx_buf[s_tx_idx++];
        }
        if (s_tx_idx >= s_tx_len)
        {
            UART_PERIPH->CR1 &= ~USART_CR1_TXEIE;
            UART_PERIPH->CR1 |= USART_CR1_TCIE; /* son baytin fiziksel olarak
                                                    bitmesini bekle */
        }
    }

    /* RXNEIE, tasma (ORE) durumunda da kesme uretir. SR okunduktan sonra
     * DR okumak ikisini de temizler; temizlenmezse kesme durmadan tekrarlar. */
    if ((sr & (USART_SR_RXNE | USART_SR_ORE)) != 0U)
    {
        uint8_t b = (uint8_t)UART_PERIPH->DR;
        command_rx_byte_from_isr(b, &higher_prio_woken);
    }

    /* Tek cikis: kesme cikisini SystemView'e de bildirir. */
    portYIELD_FROM_ISR(higher_prio_woken);
}
