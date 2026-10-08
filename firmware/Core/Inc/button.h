#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"

/* EXTI0 ISR'den ButtonTask'a tasinan olay: "Buton ISR -> Olay kimligi + t0".
 * Butonun kendisi degil, kabul anindaki kucuk kopyasi kuyruga girer. */
typedef struct
{
    uint16_t event_id; /* 1'den baslar; yalnizca kabul edilen basis kimlik alir */
    uint32_t t0_us;    /* filtre tarafindan kabul edilen basis kenarinin zamani */
} button_event_t;

/* PA0/EXTI0'i (kullanici butonu B1) iki kenarda kesme uretecek sekilde
 * kurar ve gozlem LED'ini (PD12) baslatir. evt_queue bu cagridan ONCE
 * olusturulmus olmalidir; NVIC kesmesi en son acilir. */
void button_init(QueueHandle_t evt_queue);

/* EXTI0_IRQHandler (stm32f4xx_it.c) tarafindan dogrudan cagrilir. */
void button_exti_isr_handler(void);

/* Tekrar-kenar filtresinin reddettigi kenarlar (sicramalar). */
uint32_t button_get_repeat_count(void);

/* Kabul edilmis ama buton kuyrugu dolu oldugu icin kaybolan olaylar. */
uint32_t button_get_drop_count(void);

/* Filtrenin kabul ettigi basis sayisi (acilistan beri). */
uint32_t button_get_accepted_count(void);

/* Hizli yolun reddettigi (mandal dolu ya da UART hazir degil) ve ButtonTask
 * yoluna giden basis sayisi (acilistan beri). */
uint32_t button_get_fallback_count(void);

#endif /* BUTTON_H */
