#include "button.h"
#include "main.h"
#include "timestamp.h"
#include "stats.h"
#include "uart.h"
#include "app_trace.h"

static QueueHandle_t s_evt_queue = NULL;

/* Yalnizca ISR yazar/okur (init disinda). */
static uint32_t s_last_edge_us;
static bool s_have_edge;
static bool s_level; /* son kenarda okunan seviye; grubun son kenarinda yerlesmis seviyedir */
static uint16_t s_event_id;

/* ISR yazar, gorevler okur: 32-bit hizali okuma Cortex-M4'te atomiktir. */
static volatile uint32_t s_repeat_count;
static volatile uint32_t s_drop_count;
static volatile uint32_t s_accepted_count;
static volatile uint32_t s_fallback_count;

static bool button_is_high(void)
{
    return (BUTTON_GPIO_PORT->IDR & (1UL << BUTTON_GPIO_PIN)) != 0U;
}

static uint16_t next_id(void)
{
    return ++s_event_id;
}

/* Olayi ButtonTask'a verir. Kuyruk doluysa olay kaybolmaz: kimligi ve t0'i
 * olcum sonunda REC(btn_drop) olur. */
static void queue_event_from_isr(const button_event_t *evt, BaseType_t *wake)
{
    if (xQueueSendFromISR(s_evt_queue, evt, wake) != pdPASS)
    {
        APP_TRACE_BTN_DROP(evt->event_id, APP_DROP_AT_BUTTON_QUEUE);
        s_drop_count++;
        stats_droplog_add_from_isr(evt->event_id, g_scenario.id, evt->t0_us);
    }
}

/* Iki kenar da buraya gelir. Oncesinde en az BUTTON_REPEAT_WINDOW_US boyunca
 * kenar olmayan kenar (veya ilk kenar) yeni bir grubun, yani bir basmanin ya
 * da birakmanin ilk kenaridir; pencere icindeki kenarlar sicramadir, sayilir
 * ve pencereyi yeniden baslatir.
 *
 * Grubun yonu, ISR'nin ilk kenarda okudugu seviyeden DEGIL, gruptan onceki
 * yerlesmis seviyeden belirlenir: buton birakilmis (dusuk) yerlestiyse yeni
 * grup bir basistir. Ilk kenarda okunan seviye guvenilmez: kesme gecikirse
 * (ayni oncelikteki USART2 kesmesi, kritik bolum) okuma sicramaya denk gelir
 * ve basis birakma sanilip kaybolurdu. Yerlesmis seviye, bir onceki grubun
 * son kenarinda okunan seviyedir; o kenardan sonra hat degismemistir. Boylece
 * gecikmis kesmede birlesen kenarlar ve pencere icinde biten kisa basislar da
 * bir sonraki basisi kaybettirmez. */
static bool accept_edge(uint32_t now_us, bool high)
{
    const bool settled_high = s_level; /* gruptan onceki yerlesmis seviye */
    s_level = high;

    if (s_have_edge && (uint32_t)(now_us - s_last_edge_us) < BUTTON_REPEAT_WINDOW_US)
    {
        s_last_edge_us = now_us;
        s_repeat_count++;
        return false;
    }
    s_have_edge = true;
    s_last_edge_us = now_us;

    /* Birakilmis yerlesmisse basis (olay), basili yerlesmisse birakma. */
    return !settled_high;
}

void button_init(QueueHandle_t evt_queue)
{
    s_evt_queue = evt_queue;

    /* --- GPIO: PA0 giris, pull yok (kartta harici pull-down; B1 aktif-HIGH,
     *     yani basis kenari YUKSELEN kenardir) --- */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    BUTTON_GPIO_PORT->MODER &= ~(0x3UL << (BUTTON_GPIO_PIN * 2U)); /* 00 = input */
    BUTTON_GPIO_PORT->PUPDR &= ~(0x3UL << (BUTTON_GPIO_PIN * 2U)); /* 00 = no pull */

    /* --- Gozlem LED'i: PD12 push-pull cikis --- */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    LED_GPIO_PORT->MODER = (LED_GPIO_PORT->MODER & ~(0x3UL << (LED_GPIO_PIN * 2U))) |
                           (0x1UL << (LED_GPIO_PIN * 2U)); /* 01 = output */
    LED_GPIO_PORT->BSRR = (1UL << (LED_GPIO_PIN + 16U));   /* baslangicta sondurulu */

    /* Aciliste basili tutulan buton, birakilip yeniden basilana kadar olay
     * uretmez. s_have_edge = false: ilk kenar her zaman yeni grup sayilir. */
    s_level = button_is_high();
    s_have_edge = false;

    /* --- EXTI0'i PA0'a bagla (SYSCFG_EXTICR1, EXTI0 alani = 0000b = PA) --- */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI0;

    /* Iki kenar da dinlenir: birakma kenari filtrenin buton durumunu
     * takip edebilmesi icin gereklidir. */
    EXTI->IMR  |= (1UL << BUTTON_EXTI_LINE);
    EXTI->RTSR |= (1UL << BUTTON_EXTI_LINE);
    EXTI->FTSR |= (1UL << BUTTON_EXTI_LINE);
    EXTI->PR    = (1UL << BUTTON_EXTI_LINE);

    /* FromISR cagiran kesme, FreeRTOS sistem cagrisi tavaninda veya daha
     * dusuk aciliyette olmalidir (Cortex-M: kucuk sayi = daha acil).
     * configASSERT tanimli oldugu icin yanlis oncelik, ilk FromISR
     * cagrisinda assert'e duser. Kesme EN SON acilir. */
    NVIC_SetPriority(EXTI0_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_ClearPendingIRQ(EXTI0_IRQn);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

/* Filtrenin kabul ettigi basis: olay kimligi ve t0 verilir, olay yoluna
 * gonderilir. */
static void handle_press(uint32_t t0_us, BaseType_t *wake)
{
    button_event_t evt = { next_id(), t0_us }; /* t0 = kabul edilen basis kenari */
    s_accepted_count++;
    APP_TRACE_BTN_ACCEPT(evt.event_id, g_scenario.id);

    LED_GPIO_PORT->ODR ^= (1UL << LED_GPIO_PIN);

#if BTN_FAST_PATH
    /* Hizli yol (uart.c): buton satiri gorevleri beklemeden, hat bossa
     * hemen, mesgulse hattaki satir biter bitmez gonderilir. Gorev
     * oncelikleri degismez; bu yol NVIC duzleminde (oncelik 5) calisir. */
    if (uart_btn_submit_from_isr(evt.event_id, g_scenario.id, evt.t0_us, wake) ==
        UART_BTN_REJECTED)
    {
        /* Yedek yol: mandal dolu (bir satir zaten bekliyor) ya da UART henuz
         * hazir degil. Olay ButtonTask'a gider. */
        s_fallback_count++;
        queue_event_from_isr(&evt, wake);
    }
#else
    /* A/B/C deneyi: her basis ButtonTask -> TX kuyrugu -> UartTxTask
     * yolundan gecer; gorev onceligi ve mesaj sirasi olculen degiskenlerdir. */
    queue_event_from_isr(&evt, wake);
#endif
}

void button_exti_isr_handler(void)
{
    /* Once zaman, sonra bayrak: damga kenara en yakin an olsun. SystemView
     * kesme girisi de zamandan SONRA kaydedilir: t0 izleme yuzunden kaymaz. */
    const uint32_t now_us = timestamp_now_us();
    traceISR_ENTER();
    BaseType_t wake = pdFALSE;

    if ((EXTI->PR & (1UL << BUTTON_EXTI_LINE)) != 0U)
    {
        EXTI->PR = (1UL << BUTTON_EXTI_LINE); /* yazarak temizlenir */
        if (accept_edge(now_us, button_is_high()))
        {
            handle_press(now_us, &wake);
        }
    }

    /* Tek cikis: portYIELD_FROM_ISR kesme cikisini SystemView'e de bildirir
     * (traceISR_EXIT ya da traceISR_EXIT_TO_SCHEDULER). Erken return bu
     * kaydi atlar ve kesme izde acik kalirdi; sicramalar da bu yoldan cikar. */
    portYIELD_FROM_ISR(wake);
}

uint32_t button_get_accepted_count(void)
{
    return s_accepted_count;
}

uint32_t button_get_repeat_count(void)
{
    return s_repeat_count;
}

uint32_t button_get_drop_count(void)
{
    return s_drop_count;
}

uint32_t button_get_fallback_count(void)
{
    return s_fallback_count;
}
