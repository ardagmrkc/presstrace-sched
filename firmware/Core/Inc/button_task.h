#ifndef BUTTON_TASK_H
#define BUTTON_TASK_H

#include "FreeRTOS.h"

/* Buton olaylarini isleyen gorev (A'da oncelik 2, B/C'de 4). EXTI0 ISR'si
 * kabul ettigi basisi buton kuyruguna koyar; bu gorev olayi alir (t1), BTN
 * mesaji olusturup (t2) TX kuyruguna (tx_queue.c) verir. BTN_FAST_PATH 1
 * iken bu yol yalnizca hizli yolun reddettigi basislar icin kullanilir.
 * Kendi buton kuyrugunu olusturur ve button_init() ile kaydeder. */
void button_task_create(void);

/* Surum degisiminde TelemetryTask cagirir (A: 2, B/C: 4). */
void button_task_set_priority(UBaseType_t prio);

/* Gorev yigininda acilistan beri en az bos kalan alan (word). */
UBaseType_t button_task_stack_free(void);

#endif /* BUTTON_TASK_H */
