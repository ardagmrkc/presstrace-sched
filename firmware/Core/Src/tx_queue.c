#include "tx_queue.h"
#include "main.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/*
 * s_slots: bos yer sayisi, 16'dan baslar. Gonderen once bir yer alir,
 * UartTxTask mesaji aldiktan sonra yeri geri verir. C'de iki fiziksel kuyruk
 * olsa da toplam bekleyen mesaj boylece 16'yi gecemez.
 *
 * s_items: bekleyen mesaj sayisi. UartTxTask bunu bekler; iki kuyrugu ayni
 * anda dinlemesi gerekmez. Gonderen mesaji kuyruga koyduktan SONRA verir:
 * UartTxTask uyandiginda kuyruklardan birinde mutlaka bir mesaj vardir.
 *
 * Her fiziksel kuyruk 16 mesajliktir; toplam 16 ile sinirli oldugu icin yer
 * almis bir gonderenin kuyruk yazmasi hicbir zaman basarisiz olmaz.
 *
 * Surum calisirken degisebilir (g_variant). Acil kuyruga yalnizca C aktifken
 * BTN girer; alan taraf her surumde once acil kuyruga bakar. A/B'de acil
 * kuyruk bostur ve bakmanin maliyeti uc surumde aynidir; C'den A/B'ye
 * gecerken kuyrukta kalan BTN'ler de boylece sahipsiz kalmaz.
 */
static SemaphoreHandle_t s_slots;
static SemaphoreHandle_t s_items;
static StaticSemaphore_t s_slots_ctrl;
static StaticSemaphore_t s_items_ctrl;

static QueueHandle_t s_fifo;
static StaticQueue_t s_fifo_ctrl;
static uint8_t s_fifo_items[UART_TX_QUEUE_LEN * sizeof(tx_message_t)];

static QueueHandle_t s_urgent; /* yalnizca C'de MSG_BTN */
static StaticQueue_t s_urgent_ctrl;
static uint8_t s_urgent_items[UART_TX_QUEUE_LEN * sizeof(tx_message_t)];

static QueueHandle_t queue_for(const tx_message_t *msg)
{
    if (g_variant == 'C' && msg->type == MSG_BTN)
    {
        return s_urgent;
    }
    return s_fifo;
}

void tx_queue_init(void)
{
    s_slots = xSemaphoreCreateCountingStatic(UART_TX_QUEUE_LEN, UART_TX_QUEUE_LEN, &s_slots_ctrl);
    s_items = xSemaphoreCreateCountingStatic(UART_TX_QUEUE_LEN, 0U, &s_items_ctrl);
    s_fifo = xQueueCreateStatic(UART_TX_QUEUE_LEN, sizeof(tx_message_t), s_fifo_items, &s_fifo_ctrl);
    s_urgent = xQueueCreateStatic(UART_TX_QUEUE_LEN, sizeof(tx_message_t), s_urgent_items,
                                  &s_urgent_ctrl);
    configASSERT(s_slots != NULL);
    configASSERT(s_items != NULL);
    configASSERT(s_fifo != NULL);
    configASSERT(s_urgent != NULL);
}

bool tx_queue_send(const tx_message_t *msg, TickType_t wait)
{
    if (xSemaphoreTake(s_slots, wait) != pdPASS)
    {
        return false;
    }
    BaseType_t ok = xQueueSendToBack(queue_for(msg), msg, 0);
    configASSERT(ok == pdPASS);
    (void)ok;
    (void)xSemaphoreGive(s_items);
    return true;
}

bool tx_queue_send_from_isr(const tx_message_t *msg, BaseType_t *woken)
{
    if (xSemaphoreTakeFromISR(s_slots, woken) != pdPASS)
    {
        return false;
    }
    BaseType_t ok = xQueueSendToBackFromISR(queue_for(msg), msg, woken);
    configASSERT(ok == pdPASS);
    (void)ok;
    (void)xSemaphoreGiveFromISR(s_items, woken);
    return true;
}

void tx_queue_receive(tx_message_t *msg)
{
    (void)xSemaphoreTake(s_items, portMAX_DELAY);

    /* Secim kurali: kritik bolumde yalnizca sifir beklemeli alim var, UART aktarimi bolumun disinda. Bakma ile alma
     * arasinda baska bir gorev mesaj koyamaz. */
    BaseType_t got;
    taskENTER_CRITICAL();
    got = xQueueReceive(s_urgent, msg, 0);
    if (got != pdPASS)
    {
        got = xQueueReceive(s_fifo, msg, 0);
    }
    taskEXIT_CRITICAL();
    configASSERT(got == pdPASS);
    (void)got;

    (void)xSemaphoreGive(s_slots);
}

UBaseType_t tx_queue_waiting(void)
{
    return uxSemaphoreGetCount(s_items);
}
