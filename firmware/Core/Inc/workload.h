#ifndef WORKLOAD_H
#define WORKLOAD_H

#include <stdint.h>

/*
 * S4/S5 icin "ek hesaplama/aktivasyon" yuku: TelemetryTask her periyodunda
 * (100 Hz) sabit sayida yineleme calistirarak CPU'yu belirli bir sure mesgul
 * eder (Uek = Cek * f formulundeki Cek). Gercek gecikme, saat hizina ve
 * derleyici optimizasyon seviyesine bagli oldugundan, WORKLOAD_ITERS_2MS /
 * WORKLOAD_ITERS_5MS sabitleri kart uzerinde kalibre EDILMELIDIR (bkz.
 * docs/setup.md#kalibrasyon).
 *
 * Kalibrasyon kaydi (Debug yapilandirmasi, Low optimizasyon, 168 MHz):
 * S5'te 140000 iterasyon -> extra_load_us = 8373 us olculdu, yani
 * 0,0598 us/iterasyon. Asagidaki degerler bu orandan hesaplandi; yuklemeden
 * sonra arayuzde extra_load_us ~2000 / ~5000 us oldugu dogrulanmalidir.
 * Release (High optimizasyon) icin gecerli DEGILDIR.
 */
#define WORKLOAD_ITERS_2MS  33440UL /* 2000 us / 0,0598 us */
#define WORKLOAD_ITERS_5MS  83600UL /* 5000 us / 0,0598 us */

/* iterations kadar yapay (derleyicinin atamayacagi) aritmetik is yapar.
 * Donus degeri yalnizca optimize edilip silinmesini engellemek icindir. */
uint32_t workload_run(uint32_t iterations);

#endif /* WORKLOAD_H */
