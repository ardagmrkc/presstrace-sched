#include "timestamp.h"
#include "main.h"

/* TIM2, APB1 zamanlayici saatinden turetilir. APB1 zamanlayici saati,
 * APB1 preskaleri 1'den farkli oldugunda APB1 cekirdek saatinin 2 katidir
 * (RM0090 "Clock tree"). system_clock.c icindeki yapilandirmada
 * APB1 = SYSCLK/4 = 42 MHz secildigi icin TIM2 giris saati 84 MHz'dir. */
#define TIM2_INPUT_CLK_HZ   84000000UL
#define TIMESTAMP_TICK_HZ   1000000UL /* 1 us cozunurluk */

void timestamp_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    __DSB(); /* saat etkinlestirmenin register erisiminden once oturmasini garanti eder */

    TIM2->CR1 = 0;
    TIM2->PSC = (uint16_t)((TIM2_INPUT_CLK_HZ / TIMESTAMP_TICK_HZ) - 1U);
    TIM2->ARR = 0xFFFFFFFFU; /* 32-bit serbest sayac, hicbir zaman durmaz */
    TIM2->EGR = TIM_EGR_UG;  /* PSC/ARR degerlerini hemen yukle */
    TIM2->SR = 0;
    TIM2->CR1 |= TIM_CR1_CEN;
}
