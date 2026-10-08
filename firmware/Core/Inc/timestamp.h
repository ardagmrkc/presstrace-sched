#ifndef TIMESTAMP_H
#define TIMESTAMP_H

#include <stdint.h>
#include "stm32f4xx.h"

/*
 * Mikrosaniye cozunurlukte, serbest calisan zaman damgasi kaynagi.
 *
 * TIM2 (32-bit genel amacli zamanlayici) 1 MHz'e bolunerek calistirilir ve
 * hicbir zaman durdurulmaz/sifirlanmaz; t0..t4 olcumlerinin hepsi bu ortak
 * sayacdan okunur, boylece aralarindaki fark dogrudan mikrosaniye cinsinden
 * gecen sureyi verir. 32-bit sayac ~71,58 dakikada bir sarar (rollover);
 * R = t4 - t0 gibi farklar unsigned aritmetigi sayesinde sarma durumunda da
 * dogru sonuc verir (iki olcum arasindaki gercek sure 71 dakikayi asmadigi
 * surece).
 */

/* TIM2'yi 1 MHz sayma hizinda baslatir. system_clock_config() sonrasinda,
 * yani APB1 saat hizi kesinlestikten sonra cagrilmalidir. */
void timestamp_init(void);

/* Su anki zaman damgasini mikrosaniye cinsinden dondurur. ISR icinden de
 * guvenle cagrilabilir (salt okunur register erisimi, kesme maskeleme
 * gerektirmez). */
static inline uint32_t timestamp_now_us(void)
{
    return TIM2->CNT;
}

#endif /* TIMESTAMP_H */
