#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <stdint.h>

/* STM32F407 dahili sicaklik sensoru (ADC1, kanal 16). Olcum cip (die)
 * sicakligidir; ortam sicakligindan birkac derece yuksek olmasi normaldir. */
typedef struct
{
    int16_t  temp_centi_c; /* derece C x 100 (ornek: 3245 = 32,45 C) */
    uint16_t raw;          /* kanal 16 ham 12-bit ADC degeri */
    uint16_t vdda_mv;      /* acilista VREFINT ile olculen VDDA (mV) */
} temp_sample_t;

/* ADC1'i baslatir, VDDA'yi VREFINT ile bir kez olcer, ilk sicaklik
 * ornegini bloklayarak alir ve bir sonraki donusumu arka planda baslatir.
 * timestamp_init()'ten sonra cagrilmalidir. */
void temp_sensor_init(void);

/* Bloklamaz: arka planda biten donusum varsa sonucunu okuyup yenisini
 * baslatir; yoksa son gecerli ornegi dondurur. */
void temp_sensor_poll(temp_sample_t *out);

#endif /* TEMP_SENSOR_H */
