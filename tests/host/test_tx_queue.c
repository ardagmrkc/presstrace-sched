/* tx_queue.c secim kurali ve kabul siniri icin bilgisayar (host) testi.
 *
 * Derleme (depo kokunden):
 *   gcc -std=c11 -Wall -Wextra -I tests/host/mock_txq -I firmware/Core/Inc
 *       tests/host/test_tx_queue.c firmware/Core/Src/tx_queue.c -o test_tx_queue
 *   ./test_tx_queue
 *
 * Icerik: gercek tx_queue.c, taklit kuyruk ve sayacli semafor. Uc surum
 * sirayla (g_variant) denenir. Kontrol edilen: A/B'de FIFO sirasi, C'de
 * bekleyen BTN'in TEL'lerin onune gecmesi, kontrol mesajlarinin sirasi, uc
 * surumde de toplam 16 siniri, secimin kritik bolum icinde yapilmasi ve
 * calisirken C'den A'ya geciste acil kuyrukta kalan BTN'in kaybolmamasi. */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "protocol.h"
#include "tx_queue.h"

int g_crit_depth;
int g_receive_crit_depth;
volatile char g_variant;

static int g_fail;
#define CHECK(c, msg) do { if (!(c)) { printf("  HATA: %s (satir %d)\n", msg, __LINE__); g_fail++; } } while (0)

static tx_message_t tel(uint32_t id)
{
    tx_message_t m = { .type = MSG_TEL, .scenario_id = 5 };
    m.u.tel.enq_us = id;
    return m;
}

static tx_message_t btn(uint16_t id)
{
    tx_message_t m = { .type = MSG_BTN, .scenario_id = 5 };
    m.u.btn.event_id = id;
    return m;
}

static tx_message_t ctrl(msg_type_t type)
{
    tx_message_t m = { .type = type, .scenario_id = 5 };
    return m;
}

static bool send(tx_message_t m)
{
    return tx_queue_send(&m, 0);
}

/* Kuyrugu bosaltir; alinan sirayi "T1 B1 A R" bicimine yazar. */
static void drain(char *out, size_t size)
{
    out[0] = '\0';
    while (tx_queue_waiting() > 0U)
    {
        tx_message_t m;
        char item[16];
        tx_queue_receive(&m);
        switch (m.type)
        {
        case MSG_TEL: snprintf(item, sizeof(item), "T%lu", (unsigned long)m.u.tel.enq_us); break;
        case MSG_BTN: snprintf(item, sizeof(item), "B%u", (unsigned)m.u.btn.event_id); break;
        case MSG_ACK: snprintf(item, sizeof(item), "A"); break;
        case MSG_CMD_POOL_RESET: snprintf(item, sizeof(item), "R"); break;
        default: snprintf(item, sizeof(item), "?"); break;
        }
        if (out[0] != '\0')
        {
            strncat(out, " ", size - strlen(out) - 1U);
        }
        strncat(out, item, size - strlen(out) - 1U);
    }
}

static void expect_order(const char *expect)
{
    char got[256];
    drain(got, sizeof(got));
    if (strcmp(got, expect) != 0)
    {
        printf("  sira '%s', beklenen '%s'\n", got, expect);
        g_fail++;
    }
}

static void run_variant(char v)
{
    printf("--- surum %c ---\n", v);
    g_variant = v;

    printf("1) TEL1 TEL2 BTN1 TEL3 BTN2 sirayla kuyruga girer\n");
    tx_queue_init();
    CHECK(send(tel(1)) && send(tel(2)) && send(btn(1)) && send(tel(3)) && send(btn(2)), "gonderim");
    CHECK(tx_queue_waiting() == 5U, "5 bekleyen");
    if (v == 'C')
    {
        expect_order("B1 B2 T1 T2 T3"); /* bekleyen BTN, onundeki TEL'lerin onune gecer */
    }
    else
    {
        expect_order("T1 T2 B1 T3 B2"); /* ortak FIFO */
    }

    printf("2) Secim kritik bolum icinde yapilir\n");
    tx_queue_init();
    CHECK(send(tel(1)), "gonderim");
    {
        tx_message_t m;
        tx_queue_receive(&m);
        CHECK(g_receive_crit_depth > 0, "xQueueReceive kritik bolumde");
        CHECK(g_crit_depth == 0, "kritik bolum dengeli");
    }

    printf("3) Kontrol mesajlari TEL'lerle ayni sirada kalir\n");
    tx_queue_init();
    CHECK(send(tel(1)) && send(ctrl(MSG_CMD_POOL_RESET)) && send(ctrl(MSG_ACK)) && send(tel(2)),
          "gonderim");
    expect_order("T1 R A T2");

    printf("4) Toplam siniri 16: TEL de BTN de 17. mesajda duser\n");
    tx_queue_init();
    for (uint32_t i = 1U; i <= UART_TX_QUEUE_LEN; i++)
    {
        CHECK(send(tel(i)), "16 TEL kabul");
    }
    CHECK(!send(tel(17)), "17. TEL duser");
    CHECK(!send(btn(1)), "kuyruk doluyken BTN de duser (ortak sinir)");
    CHECK(tx_queue_waiting() == UART_TX_QUEUE_LEN, "16 bekleyen");
    {
        tx_message_t m;
        tx_queue_receive(&m);
        CHECK(m.type == MSG_TEL && m.u.tel.enq_us == 1U, "ilk TEL1");
    }
    CHECK(send(btn(2)), "bir yer acilinca BTN girer");
    CHECK(!send(tel(18)), "yine dolu");
    {
        tx_message_t m;
        tx_queue_receive(&m);
        if (v == 'C')
        {
            CHECK(m.type == MSG_BTN && m.u.btn.event_id == 2U, "C: BTN2, 14 TEL'in onune gecer");
        }
        else
        {
            CHECK(m.type == MSG_TEL && m.u.tel.enq_us == 2U, "A/B: sirada TEL2 var");
        }
    }
    {
        char rest[256];
        drain(rest, sizeof(rest));
    }
    CHECK(tx_queue_waiting() == 0U, "bosaldi");

    printf("5) Kesmeden gonderim ayni siniri kullanir\n");
    tx_queue_init();
    for (uint32_t i = 1U; i < UART_TX_QUEUE_LEN; i++)
    {
        CHECK(send(tel(i)), "15 TEL");
    }
    {
        BaseType_t w = pdFALSE;
        tx_message_t m = ctrl(MSG_BTN_DONE);
        CHECK(tx_queue_send_from_isr(&m, &w), "16. yer kesmeden");
        CHECK(!tx_queue_send_from_isr(&m, &w), "17. yer yok");
    }
}

int main(void)
{
    printf("tx_queue testi\n");
    run_variant('A');
    run_variant('B');
    run_variant('C');

    printf("--- calisirken surum degisimi ---\n");
    printf("6) C'de acil kuyruktaki BTN, A'ya gecildikten sonra da gonderilir\n");
    g_variant = 'C';
    tx_queue_init();
    CHECK(send(tel(1)) && send(btn(1)), "C'de gonderim");
    g_variant = 'A';
    CHECK(send(tel(2)) && send(btn(2)), "A'da gonderim");
    expect_order("B1 T1 T2 B2"); /* kalan BTN1 once, sonra FIFO */

    CHECK(g_crit_depth == 0, "kritik bolum dengeli");
    printf("\n%s (%d hata)\n", g_fail == 0 ? "TUM TESTLER GECTI" : "TEST BASARISIZ", g_fail);
    return g_fail != 0;
}
