#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "app_trace.h"

/*
 * USART2, UART_BAUDRATE-8N1 (main.h; su an 230400; 115200 ile de calisir),
 * kesme (TXE/TC) tabanli gonderim. RX de aciktir; alinan baytlar command.c'deki
 * senaryo komutu cozucusune gider.
 *
 * Hatta ayni anda tek satir olabilir ve hatti kimin kullandigina tek bir hakem
 * karar verir (uart.c). Iki yol vardir:
 *  - Gorev yolu (yalnizca UartTxTask): uart_claim_line -> kodla ->
 *    uart_start_claimed -> uart_wait_tx_done.
 *  - Hizli yol (yalnizca EXTI0 ISR'si): uart_btn_submit_from_isr. Buton satiri
 *    gorevleri beklemeden, hat bossa hemen, mesgulse hattaki satir biter bitmez
 *    (TC ISR'sinden) baslatilir. Gorev oncelikleri degismez; zaman kritik yol
 *    NVIC duzleminde calisir.
 */

/* notify_task, gorev yolunun bildirimlerini alacak gorevdir (UartTxTask).
 * Donanim bu cagridan sonra hazirdir; hizli yol ancak bundan sonra acilir. */
void uart_init(TaskHandle_t notify_task);

/* ---- Gorev yolu (yalnizca notify_task cagirir) ------------------------- */

/* Hatti sahiplenir. Hatta hizli yoldan bir buton satiri varsa ya da mandalda
 * bekleyen bir basis varsa, onlar bitene kadar en fazla timeout bekler.
 * Basarida *seq, satira yazilacak sira numarasidir (TEL/BTN/ACK icin). */
bool uart_claim_line(TickType_t timeout, uint16_t *seq);

/* Sahiplenilmis hatta buf/len gonderimini baslatir. tag sifirdan farklidir;
 * TC yalnizca bu tag'e ait aktarimi kapatabilir. consumes_seq, satir seq
 * tasiyorsa true'dur (seq hatta cikis aninda artar). buf, uart_wait_tx_done
 * donene kadar yasamalidir. */
void uart_start_claimed(const uint8_t *buf, size_t len, uint32_t tag, bool consumes_seq);

/* Sahiplenilmis ama baslatilmamis hatti birakir (ornegin kodlama hatasi). */
void uart_release_line(void);

#if USE_SYSVIEW
/* SystemView: siradaki uart_start_claimed aktarimi bu kimlikli BTN satiridir.
 * TC kesmesi BTN TX_TC olayini (t4) bu kimlikle yazar. uart_start_claimed'den
 * hemen once cagrilir. */
void uart_trace_next_btn(uint16_t event_id);
#endif

typedef enum
{
    UART_TX_DONE = 0,     /* TC bu aktarim icin geldi, *t4_us gecerli */
    UART_TX_TIMEOUT,      /* TC gelmedi; aktarim iptal edildi */
    UART_TX_TAG_MISMATCH, /* TC geldi ama baska bir aktarima aitti */
} uart_tx_result_t;

/* TC'yi en fazla timeout kadar bekler. Zaman asiminda aktarimi iptal eder
 * (TXE/TC kesmeleri kapanir), boylece ISR buf'i okumayi birakir ve gec
 * gelen bir TC sonraki aktarimi kapatamaz. */
uart_tx_result_t uart_wait_tx_done(uint32_t tag, TickType_t timeout, uint32_t *t4_us);

/* ---- Hizli yol (yalnizca EXTI0 ISR'si cagirir) ------------------------- */

typedef enum
{
    UART_BTN_STARTED = 0, /* hat bostu: satir hemen baslatildi */
    UART_BTN_LATCHED,     /* hat mesguldu: mandal kuruldu, TC ISR'si baslatacak */
    UART_BTN_REJECTED,    /* mandal dolu ya da UART hazir degil: ButtonTask yolu kullanilmali */
} uart_btn_submit_t;

/* Kabul edilen basisin buton satirini hizli yoldan gonderir. t1 ve t2 bu
 * cagri icinde alinir, t3 satir hatta baslarken, t4 TC'de. Sonuc (t0..t4 ve
 * durum) TC ISR'sinden UartTxTask'a iletilir ve kayit havuzuna yazilir. */
uart_btn_submit_t uart_btn_submit_from_isr(uint16_t event_id, uint8_t scenario_id,
                                           uint32_t t0_us, BaseType_t *woken);

/* Hizli yoldaki bir buton satirinin sonucu (UartTxTask'a iletilir). */
typedef struct
{
    uint16_t event_id;
    uint8_t scenario_id;
    uint8_t known;  /* gecerli zaman sayisi: 3 = t0..t2, 4 = t0..t3, 5 = t0..t4 */
    bool aborted;   /* TC gelmedi, aktarim iptal edildi */
    uint32_t t[5];
} uart_btn_result_t;

/* ---- Sayaclar ve ISR ---------------------------------------------------- */

/* Ucusta aktarim yokken ya da baska aktarima ait gelen TC sayisi. */
uint32_t uart_get_spurious_tc(void);

/* Hizli yol: hemen baslatilan / mandaldan baslatilan buton satiri sayisi
 * (acilistan beri). */
uint32_t uart_get_btn_direct(void);
uint32_t uart_get_btn_latched(void);

/* USART2_IRQHandler (stm32f4xx_it.c) tarafindan cagrilir. TC isleyisinde
 * ilk is t4 zaman damgasini alir: son stop bitinin hattan ciktigi an. */
void uart_isr_handler(void);

#endif /* UART_H */
