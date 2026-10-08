#ifndef UART_TX_TASK_H
#define UART_TX_TASK_H

#include "FreeRTOS.h"
#include "uart.h"

/* En dusuk oncelikli (PRIO_UART_TX_TASK = 1) gorev. TX kuyrugundan
 * (tx_queue.c) surumun secim kuralina gore mesaj alir: A/B'de FIFO, C'de
 * once bekleyen BTN. Hatti uart.c'deki hakemden sahiplenip gonderir (t3) ve
 * TC tamamlanmasini sureli bekler (t4). Olcum kaydi havuzunun (64) tek
 * sahibidir: ButtonTask'tan gelen BTN olaylarini (ve BTN_FAST_PATH 1 iken
 * hizli yolun sonuclarini, MSG_BTN_DONE) buraya yazar. Havuz yalnizca
 * "olcumu bitir" komutuyla REC + CNT + END olarak dokulur. */
void uart_tx_task_create(void);

/* UART TC ISR'si (ya da hakemin kritik bolumu) tarafindan cagrilir: hizli
 * yoldaki bir buton satirinin sonucunu MSG_BTN_DONE olarak TX kuyruguna
 * koyar. Kuyruk doluysa sonuc kaybolur ve btn_result_lost sayilir. */
void uart_tx_task_btn_done_from_isr(const uart_btn_result_t *res, BaseType_t *woken);

#endif /* UART_TX_TASK_H */
