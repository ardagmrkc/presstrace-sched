#ifndef TX_QUEUE_H
#define TX_QUEUE_H

#include <stdbool.h>
#include "FreeRTOS.h"
#include "protocol.h"

/*
 * UartTxTask'a giden mesajlarin kuyrugu. Uc surumde de ayni anda en fazla
 * UART_TX_QUEUE_LEN (16) mesaj bekleyebilir; yer yoksa bloklamayan gonderim
 * false doner ve mesaj duser.
 *  - A, B: tek FIFO. Mesajlar kuyruga girdikleri sirayla gonderilir;
 *    ButtonTask'in onceligi kuyrukta onunde duran TEL'leri geri itmez.
 *  - C: BTN ayri bir acil kuyruga girer. UartTxTask her aktarim bittiginde
 *    once acil kuyruga bakar: bekleyen BTN, kuyrukta onunde duran TEL'lerin
 *    onune gecer. Hatta baslamis bir aktarim kesilmez.
 * Iki surum arasindaki tek fark bu secim kuralidir; kabul siniri ve
 * kuyruk mekanizmasi aynidir. Surum g_variant'tan (main.h) okunur ve
 * calisirken degisebilir.
 */

/* Scheduler baslamadan, gorevler olusturulmadan once cagrilir. */
void tx_queue_init(void);

/* Gorevler cagirir. wait = 0: yer yoksa hemen false (mesaj duser). */
bool tx_queue_send(const tx_message_t *msg, TickType_t wait);

/* Kesmeler cagirir; bloklamaz. */
bool tx_queue_send_from_isr(const tx_message_t *msg, BaseType_t *woken);

/* Yalnizca UartTxTask cagirir: mesaj gelene kadar bekler, sonra surumun
 * secim kuralina gore bir mesaj alir. */
void tx_queue_receive(tx_message_t *msg);

/* Bekleyen (UartTxTask'in henuz almadigi) mesaj sayisi. */
UBaseType_t tx_queue_waiting(void);

#endif /* TX_QUEUE_H */
