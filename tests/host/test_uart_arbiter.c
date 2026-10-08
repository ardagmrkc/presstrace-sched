/* uart.c hakem mantigi icin bilgisayar (host) testi.
 *
 * Derleme (depo kokunden), iki cerceve modu icin ayri ayri:
 *   gcc -std=c11 -Wall -Wextra -DPROTOCOL_COMPACT=0 -I tests/host/mock
 *       -I firmware/Core/Inc tests/host/test_uart_arbiter.c
 *       firmware/Core/Src/uart.c firmware/Core/Src/protocol.c -o test_uart_arbiter
 *   ./test_uart_arbiter
 *   (ayni komut -DPROTOCOL_COMPACT=1 ile: kisa ikili TEL/BTN cercevesi)
 *
 * Icerik: gercek uart.c + protocol.c, taklit USART register'lari, taklit
 * FreeRTOS. Donanim, gorev beklerken (xTaskNotifyWait) bayt bayt ilerletilir;
 * basislar belirli bayt anlarinda EXTI ISR'si olarak enjekte edilir. Her
 * senaryoda hattaki baytlarin bosluksuz gecerli cercevelere (64 baytlik satir
 * ya da CRC'si dogru ikili cerceve) bolundugu ve seq sirasinin bozulmadigi
 * dogrulanir. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"
#include "uart.h"
#include "protocol.h"
#include "uart_tx_task.h"

USART_TypeDef g_usart;
GPIO_TypeDef g_gpioa;
RCC_TypeDef g_rcc;
int g_crit_depth;
int g_in_isr;
uint32_t g_now = 1000000U;

static uint32_t g_notify_bits;
static bool g_notify_pending;
static uint32_t g_last_notify_value;
static uint32_t g_last_notify_cr1;

static uint8_t g_wire[64 * 64];
static size_t g_wire_len;
static uart_btn_result_t g_results[32];
static int g_n_results;

static uint32_t g_hw_dead_until;
static int g_press_at[4] = { -1, -1, -1, -1 };
static uart_btn_submit_t g_press_res[4];
static int g_bytes_total;
static uint16_t g_next_event = 1;
static uint32_t g_tag;

static int g_fail;
#define CHECK(c, msg) do { if (!(c)) { printf("  HATA: %s (satir %d)\n", msg, __LINE__); g_fail++; } } while (0)

void uart_tx_task_btn_done_from_isr(const uart_btn_result_t *r, BaseType_t *w)
{
    (void)w;
    g_results[g_n_results++] = *r;
}

BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t v, eNotifyAction a, BaseType_t *w)
{
    (void)t; (void)a; (void)w;
    g_notify_bits |= v;
    g_notify_pending = true;
    g_last_notify_value = v;
    g_last_notify_cr1 = g_usart.CR1;
    return pdPASS;
}

static uart_btn_submit_t press(void)
{
    BaseType_t w = pdFALSE;
    g_in_isr++;
    uart_btn_submit_t r = uart_btn_submit_from_isr(g_next_event++, 5U, g_now, &w);
    g_in_isr--;
    return r;
}

static void isr(void)
{
    g_in_isr++;
    uart_isr_handler();
    g_in_isr--;
}

/* Donanimi bir bayt suresi (230400'de ~43 us) ilerletir. */
static bool hw_step(void)
{
    for (int i = 0; i < 4; i++)
    {
        if (g_press_at[i] >= 0 && g_bytes_total == g_press_at[i])
        {
            g_press_at[i] = -1;
            g_press_res[i] = press();
        }
    }
    g_now += 43U;
    if ((int32_t)(g_now - g_hw_dead_until) < 0)
    {
        return false; /* donanim "takili": TXE/TC uretmiyor */
    }
    if (g_usart.CR1 & USART_CR1_TXEIE)
    {
        g_usart.SR |= USART_SR_TXE;
        g_usart.DR = 0xFFFFU;
        isr();
        if (g_usart.DR != 0xFFFFU)
        {
            g_wire[g_wire_len++] = (uint8_t)g_usart.DR;
            g_bytes_total++;
        }
        return true;
    }
    if (g_usart.CR1 & USART_CR1_TCIE)
    {
        g_usart.SR |= USART_SR_TC;
        isr();
        return true;
    }
    return false;
}

static void drain(void)
{
    for (int i = 0; i < 100000 && hw_step(); i++)
    {
    }
}

BaseType_t xTaskNotifyWait(uint32_t clr_entry, uint32_t clr_exit, uint32_t *val, TickType_t ticks)
{
    assert(g_crit_depth == 0 && g_in_isr == 0);
    if (!g_notify_pending)
    {
        g_notify_bits &= ~clr_entry;
    }
    const uint32_t deadline = g_now + ticks * 1000U;
    while (!g_notify_pending)
    {
        if (!hw_step())
        {
            g_now += 1000U;
        }
        if ((int32_t)(g_now - deadline) >= 0 && !g_notify_pending)
        {
            return pdFALSE;
        }
    }
    if (val != NULL)
    {
        *val = g_notify_bits;
    }
    g_notify_bits &= ~clr_exit;
    g_notify_pending = false;
    return pdTRUE;
}

uint32_t ulTaskNotifyValueClear(TaskHandle_t t, uint32_t bits)
{
    (void)t;
    uint32_t prev = g_notify_bits;
    g_notify_bits &= ~bits;
    return prev;
}

void vTaskSetTimeOutState(TimeOut_t *to)
{
    to->start_us = g_now;
}

BaseType_t xTaskCheckForTimeOut(TimeOut_t *to, TickType_t *remaining)
{
    uint32_t elapsed = (g_now - to->start_us) / 1000U;
    if (elapsed >= *remaining)
    {
        *remaining = 0;
        return pdTRUE;
    }
    *remaining -= elapsed;
    to->start_us = g_now;
    return pdFALSE;
}

/* UartTxTask'in send_frame'i gibi: sahiplen -> kodla -> baslat -> bekle. */
static int task_send_tel(void)
{
    uint16_t seq = 0;
    if (!uart_claim_line(50, &seq))
    {
        return -1;
    }
    tx_message_t m = { .type = MSG_TEL, .scenario_id = 5 };
    m.u.tel.temp_centi_c = 3150;
    m.u.tel.period_us = 10000;
    uint8_t frame[PROTOCOL_FRAME_SIZE];
    const size_t len = protocol_encode(&m, seq, frame);
    assert(len > 0U);
    uart_start_claimed(frame, len, ++g_tag, true);
    uint32_t t4 = 0;
    return (int)uart_wait_tx_done(g_tag, 50, &t4);
}

/* Hattaki cercevenin uzunlugu (TEL/BTN), basis anlarini satira gore koymak icin. */
static size_t frame_len(msg_type_t type)
{
    tx_message_t m = { .type = type, .scenario_id = 5 };
    uint8_t frame[PROTOCOL_FRAME_SIZE];
    return protocol_encode(&m, 0, frame);
}

static uint32_t le(const uint8_t *p, int n)
{
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; i--)
    {
        v = (v << 8) | p[i];
    }
    return v;
}

/* Hattaki baytlarin bosluksuz gecerli cercevelere bolundugunu ve seq'in
 * birer arttigini dogrular. Beklenen seq bosluk sayisini dondurur. */
static int check_wire(const char *expect, int *gaps)
{
    int bad = 0;
    long last = -1;
    *gaps = 0;
    char got[64] = { 0 };
    size_t i = 0;
    for (size_t pos = 0; pos < g_wire_len; i++)
    {
        const uint8_t *f = &g_wire[pos];
        long seq;
        if (f[0] == PROTOCOL_BIN_SYNC)
        {
            const size_t n = (f[1] == PROTOCOL_BIN_TYPE_TEL) ? PROTOCOL_BIN_TEL_SIZE
                           : (f[1] == PROTOCOL_BIN_TYPE_BTN) ? PROTOCOL_BIN_BTN_SIZE : 0U;
            if (n == 0U || pos + n > g_wire_len || protocol_crc8(f, n - 1U) != f[n - 1U])
            {
                printf("  bozuk ikili cerceve @%zu\n", pos);
                return bad + 1;
            }
            got[i] = (char)f[1];
            seq = (long)le(f + ((f[1] == PROTOCOL_BIN_TYPE_TEL) ? 2 : 5), 2);
            pos += n;
        }
        else
        {
            const char *l = (const char *)f;
            if (pos + 64U > g_wire_len || l[63] != '\n' ||
                (strncmp(l, "TEL,", 4) != 0 && strncmp(l, "BTN,", 4) != 0))
            {
                printf("  bozuk satir @%zu: %.63s\n", pos, l);
                return bad + 1;
            }
            got[i] = l[0];
            if (l[0] == 'T')
            {
                seq = strtol(l + 4, NULL, 10);
            }
            else
            {
                const char *p = strstr(l, "PRESSED,");
                seq = strtol(p + 8, NULL, 10);
            }
            pos += 64U;
        }
        if (last >= 0 && seq != last + 1)
        {
            *gaps += (int)(seq - last - 1);
            if (seq <= last)
            {
                printf("  seq geriye gitti: %ld -> %ld\n", last, seq);
                bad++;
            }
        }
        last = seq;
    }
    if (expect != NULL && strcmp(got, expect) != 0)
    {
        printf("  satir sirasi '%s', beklenen '%s'\n", got, expect);
        bad++;
    }
    return bad;
}

static void reset_capture(void)
{
    g_wire_len = 0;
    g_n_results = 0;
}

int main(void)
{
    int gaps;
    uart_init((TaskHandle_t)0x1234);
    const int tel_len = (int)frame_len(MSG_TEL);
    const int btn_len = (int)frame_len(MSG_BTN);
    printf("Cerceve: %s, TEL %d bayt, BTN %d bayt\n\n",
           PROTOCOL_COMPACT ? "kisa ikili" : "64 bayt ASCII", tel_len, btn_len);

    printf("1) Bos hatta gorev TEL gonderir\n");
    reset_capture();
    CHECK(task_send_tel() == UART_TX_DONE, "TEL tamamlanmali");
    CHECK(check_wire("T", &gaps) == 0 && gaps == 0, "hat: tek TEL");

    printf("2) Bos hatta basis: EXTI ISR'si satiri hemen baslatir\n");
    reset_capture();
    uint32_t t0 = g_now;
    CHECK(press() == UART_BTN_STARTED, "STARTED olmali");
    drain();
    CHECK(g_n_results == 1 && g_results[0].known == 5 && !g_results[0].aborted, "sonuc t0..t4");
    uint32_t R = g_results[0].t[4] - g_results[0].t[0];
    printf("   R = %u us (%d bayt x 43 us = %d)\n", R, btn_len, btn_len * 43);
    CHECK(g_results[0].t[0] == t0 && g_results[0].t[3] - t0 < 100U, "t3 hemen");
    CHECK(check_wire("B", &gaps) == 0 && gaps == 0, "hat: tek BTN");

    printf("3) TEL hattayken basis: mandal; TC ISR'si gorevi uyandirmadan ONCE butonu baslatir\n");
    reset_capture();
    g_press_at[0] = g_bytes_total + tel_len / 2; /* TEL'in ortasinda */
    CHECK(task_send_tel() == UART_TX_DONE, "TEL tamamlanmali");
    CHECK(g_press_res[0] == UART_BTN_LATCHED, "LATCHED olmali");
    CHECK((g_last_notify_value & (1U << 0)) != 0U, "TX_DONE bildirimi");
    CHECK((g_last_notify_value & (1U << 1)) == 0U, "buton basladigi icin LINE_FREE olmamali");
    CHECK((g_last_notify_cr1 & USART_CR1_TXEIE) != 0U, "bildirim aninda buton satiri baslamis olmali");
    drain();
    CHECK(g_n_results == 1 && g_results[0].known == 5, "buton sonucu");
    printf("   bekleme (t3-t2) = %u us, R = %u us\n", g_results[0].t[3] - g_results[0].t[2],
           g_results[0].t[4] - g_results[0].t[0]);
    CHECK(check_wire("TB", &gaps) == 0 && gaps == 0, "hat: TEL sonra BTN, seq sirali");

    printf("4) Buton hattayken ikinci basis mandala, ucuncusu reddedilir (yedek yol)\n");
    reset_capture();
    CHECK(press() == UART_BTN_STARTED, "1. basis STARTED");
    g_press_at[0] = g_bytes_total + btn_len / 4; /* ikisi de ilk BTN hattayken */
    g_press_at[1] = g_bytes_total + btn_len / 2;
    drain();
    CHECK(g_press_res[0] == UART_BTN_LATCHED, "2. basis LATCHED");
    CHECK(g_press_res[1] == UART_BTN_REJECTED, "3. basis REJECTED");
    CHECK(g_n_results == 2, "iki buton sonucu");
    CHECK(check_wire("BB", &gaps) == 0 && gaps == 0, "hat: iki BTN");

    printf("5) Buton hattayken gorev TEL ister: hat bosalana kadar bekler\n");
    reset_capture();
    CHECK(press() == UART_BTN_STARTED, "STARTED");
    CHECK(task_send_tel() == UART_TX_DONE, "TEL tamamlanmali");
    CHECK(check_wire("BT", &gaps) == 0 && gaps == 0, "hat: BTN sonra TEL");

    printf("6) Mandal bekliyorken gorev hatti alamaz (TEL butonun onune gecemez)\n");
    reset_capture();
    g_press_at[0] = g_bytes_total + 5; /* TEL'in 5. baytinda basis */
    CHECK(task_send_tel() == UART_TX_DONE, "1. TEL");
    CHECK(task_send_tel() == UART_TX_DONE, "2. TEL");
    CHECK(check_wire("TBT", &gaps) == 0 && gaps == 0, "hat: TEL, BTN, TEL");

    printf("7) Takilan buton aktarimi: gorev zaman asiminda kurtarir\n");
    g_n_results = 0; /* hat kaydi korunur: seq boslugu onceki satira gore gorulur */
    g_hw_dead_until = g_now + 60000U;
    CHECK(press() == UART_BTN_STARTED, "STARTED");
    CHECK(task_send_tel() == UART_TX_DONE, "kurtarmadan sonra TEL gitmeli");
    CHECK(g_n_results == 1 && g_results[0].aborted && g_results[0].known == 4, "buton sonucu: iptal (timeout)");
    CHECK(check_wire("TBTT", &gaps) == 0 && gaps == 1, "hat: yalnizca TEL, gitmeyen BTN icin 1 seq boslugu");

    printf("8) TEL zaman asimi + mandal: iptalde bekleyen buton baslatilir\n");
    reset_capture();
    g_press_at[0] = g_bytes_total + 3;
    g_hw_dead_until = 0;
    {
        uint16_t seq;
        CHECK(uart_claim_line(50, &seq), "claim");
        tx_message_t m = { .type = MSG_TEL, .scenario_id = 5 };
        uint8_t frame[PROTOCOL_FRAME_SIZE];
        const size_t len = protocol_encode(&m, seq, frame);
        assert(len > 0U);
        uart_start_claimed(frame, len, ++g_tag, true);
        /* 4 bayt sonra donanim takilsin */
        for (int i = 0; i < 5; i++) hw_step();
        g_hw_dead_until = g_now + 70000U;
        uint32_t t4;
        CHECK(uart_wait_tx_done(g_tag, 50, &t4) == UART_TX_TIMEOUT, "TEL zaman asimi");
        CHECK(g_press_res[0] == UART_BTN_LATCHED, "basis mandalda");
        CHECK((g_usart.CR1 & USART_CR1_TXEIE) != 0U, "iptalde mandaldaki buton baslamali");
        g_hw_dead_until = 0;
        drain();
        CHECK(g_n_results == 1 && g_results[0].known == 5, "buton tamamlandi");
    }

    CHECK(g_crit_depth == 0, "kritik bolum dengeli");
    printf("\n%s (%d hata)\n", g_fail == 0 ? "TUM TESTLER GECTI" : "TEST BASARISIZ", g_fail);
    return g_fail != 0;
}
