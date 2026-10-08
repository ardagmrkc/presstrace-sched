/* button.c sicrama filtresi icin bilgisayar (host) testi.
 *
 * Derleme (depo kokunden):
 *   gcc -std=c11 -Wall -Wextra -I tests/host/mock_btn -I firmware/Core/Inc
 *       tests/host/test_button_filter.c firmware/Core/Src/button.c
 *       -o test_button_filter
 *   ./test_button_filter
 *
 * Icerik: gercek button.c, taklit GPIO/EXTI register'lari. Her kenar, EXTI0
 * ISR'si cagrilarak verilir; ISR'nin o an okudugu pin seviyesi (IDR) kenarla
 * birlikte belirlenir. Boylece kesme gecikmesi yuzunden seviyenin sicrama
 * sirasinda okunmasi (ilk ornek yanlis) ve gecikmis kesmede birden cok
 * kenarin tek cagriya birlesmesi canlandirilir. Her fiziksel basis tam bir
 * olay uretmelidir. */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "button.h"

GPIO_TypeDef g_gpioa, g_gpiod;
RCC_TypeDef g_rcc;
SYSCFG_TypeDef g_syscfg;
EXTI_TypeDef g_exti;
scenario_config_t g_scenario = { 5 };
uint32_t g_now_us;

static button_event_t s_events[64];
static unsigned s_n_events;
static int s_fail;

BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken)
{
    (void)q;
    (void)woken;
    if (s_n_events < 64)
    {
        memcpy(&s_events[s_n_events++], item, sizeof(button_event_t));
    }
    return pdPASS;
}

/* Bir kenar: zaman (ms) ve ISR'nin okudugu seviye. */
static void edge(double t_ms, int level)
{
    g_now_us = (uint32_t)(t_ms * 1000.0 + 0.5);
    g_gpioa.IDR = level ? 1u : 0u;
    g_exti.PR = 1u;
    button_exti_isr_handler();
}

static void start(const char *name, int level_at_init)
{
    printf("%s\n", name);
    g_gpioa.IDR = level_at_init ? 1u : 0u;
    button_init((QueueHandle_t)1);
    s_n_events = 0;
}

static void expect(unsigned n, double first_t0_ms)
{
    int ok = (s_n_events == n);
    if (ok && n > 0 && first_t0_ms >= 0.0)
    {
        ok = (s_events[0].t0_us == (uint32_t)(first_t0_ms * 1000.0 + 0.5));
    }
    printf("   %s: %u olay (beklenen %u)\n", ok ? "OK  " : "HATA", s_n_events, n);
    if (!ok)
    {
        s_fail++;
    }
}

int main(void)
{
    double t = 1000.0;

    start("1) Temiz basis / birakma x3", 0);
    for (int i = 0; i < 3; i++, t += 600.0)
    {
        edge(t, 1);
        edge(t + 150.0, 0);
    }
    expect(3, 1000.0);

    start("2) Sicrayan basis: ISR ilk kenarda seviyeyi dusuk okur", 0);
    t += 1000.0;
    edge(t, 0);         /* yukselen kenar, ama okuma aninda kontak geri sekti */
    edge(t + 0.1, 1);
    edge(t + 0.3, 0);
    edge(t + 0.5, 1);   /* yerlesti: basili */
    edge(t + 150.0, 0); /* birakma */
    expect(1, t);

    start("3) Sicrayan birakma: ISR ilk kenarda seviyeyi yuksek okur, sonraki basis", 0);
    t += 1000.0;
    edge(t, 1);          /* temiz basis */
    edge(t + 150.0, 1);  /* dusen kenar, okuma aninda geri sekti */
    edge(t + 150.1, 0);  /* yerlesti: birakildi */
    edge(t + 700.0, 1);  /* sonraki basis */
    edge(t + 850.0, 0);
    expect(2, t);

    start("4) Kisa basis (< 30 ms), sonraki basis", 0);
    t += 1000.0;
    edge(t, 1);
    edge(t + 20.0, 0);   /* birakma sicrama penceresi icinde */
    edge(t + 500.0, 1);
    edge(t + 650.0, 0);
    expect(2, t);

    start("5) Gecikmis kesme: basis+birakma tek cagriya birlesti", 0);
    t += 1000.0;
    edge(t, 0);          /* ISR calistiginda buton zaten birakilmis */
    edge(t + 600.0, 1);  /* sonraki basis */
    edge(t + 750.0, 0);
    expect(2, t);

    start("6) Aciliste basili tutulan buton olay uretmez, sonraki basis uretir", 1);
    t += 1000.0;
    edge(t, 0);          /* ilk birakma */
    edge(t + 500.0, 1);
    edge(t + 650.0, 0);
    expect(1, t + 500.0);

    start("7) Basili tutarken 30 ms'den seyrek kontak gurultusu yeni basis sayilmaz", 0);
    t += 1000.0;
    edge(t, 1);
    edge(t + 200.0, 1);  /* basiliyken tek gurultu kenari, seviye hala yuksek */
    edge(t + 400.0, 0);  /* birakma */
    expect(1, t);

    start("8) Sicramalar tek olay: 5 kenar 2 ms icinde", 0);
    t += 1000.0;
    edge(t, 1);
    edge(t + 0.4, 0);
    edge(t + 0.8, 1);
    edge(t + 1.2, 0);
    edge(t + 1.6, 1);
    edge(t + 200.0, 0);
    expect(1, t);

    printf("\n%s (%d hata)\n", s_fail ? "TESTLER BASARISIZ" : "TUM TESTLER GECTI", s_fail);
    return s_fail ? 1 : 0;
}
