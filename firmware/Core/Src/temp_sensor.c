#include "temp_sensor.h"
#include "main.h"
#include "timestamp.h"

/* Fabrika kalibrasyon degerleri, VDDA = 3,3 V'ta olculmus
 * (DS8626 STM32F405/407 datasheet; CMSIS device basliginda tanimli degil). */
#define VREFINT_CAL     (*(const uint16_t *)0x1FFF7A2AUL)
#define TS_CAL1         (*(const uint16_t *)0x1FFF7A2CUL) /* 30 C */
#define TS_CAL2         (*(const uint16_t *)0x1FFF7A2EUL) /* 110 C */
#define TS_CAL1_TEMP_C  30
#define TS_CAL2_TEMP_C  110
#define CAL_VDDA_MV     3300UL

#define ADC_CH_TEMP     16U
#define ADC_CH_VREFINT  17U

static uint16_t s_vrefint_raw;
static temp_sample_t s_last;

static void delay_us(uint32_t us)
{
    uint32_t start = timestamp_now_us();
    while ((uint32_t)(timestamp_now_us() - start) < us)
    {
    }
}

static uint16_t convert_blocking(uint32_t channel)
{
    ADC1->SQR3 = channel << ADC_SQR3_SQ1_Pos;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while ((ADC1->SR & ADC_SR_EOC) == 0U)
    {
    }
    return (uint16_t)ADC1->DR; /* DR okumak EOC'yi temizler */
}

static int16_t raw_to_centi_c(uint16_t raw)
{
    /* Discovery kartinda VDDA ~3,0 V, kalibrasyon ise 3,3 V'ta yapilmis:
     * ham degeri once kalibrasyon kosuluna olcekliyoruz. */
    int32_t raw_cal = (int32_t)(((uint32_t)raw * VREFINT_CAL) / s_vrefint_raw);
    int32_t span = (int32_t)TS_CAL2 - (int32_t)TS_CAL1;
    int32_t centi = ((raw_cal - (int32_t)TS_CAL1) * (TS_CAL2_TEMP_C - TS_CAL1_TEMP_C) * 100) / span;
    return (int16_t)(centi + TS_CAL1_TEMP_C * 100);
}

void temp_sensor_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    __DSB();

    /* ADCCLK = APB2 (84 MHz) / 4 = 21 MHz (F407 siniri 36 MHz);
     * sicaklik sensoru + VREFINT acik. */
    ADC123_COMMON->CCR = (ADC123_COMMON->CCR & ~ADC_CCR_ADCPRE) |
                         ADC_CCR_ADCPRE_0 | ADC_CCR_TSVREFE;

    ADC1->CR1 = 0;  /* 12-bit, scan kapali */
    ADC1->CR2 = 0;  /* tekli donusum, sag hizali, yazilim tetigi */
    ADC1->SQR1 = 0; /* L = 0: dizide tek donusum */

    /* Sensor icin asgari ornekleme suresi 10 us:
     * 480 cevrim / 21 MHz = 22,9 us (en uzun secenek). */
    ADC1->SMPR1 |= ADC_SMPR1_SMP16 | ADC_SMPR1_SMP17;

    ADC1->CR2 |= ADC_CR2_ADON;
    delay_us(20); /* ADC t_STAB (3 us) + sensor t_START (10 us) */

    uint32_t acc = 0;
    for (uint32_t i = 0; i < 8U; i++)
    {
        acc += convert_blocking(ADC_CH_VREFINT);
    }
    s_vrefint_raw = (uint16_t)(acc / 8U);

    uint16_t raw = convert_blocking(ADC_CH_TEMP);
    s_last.raw = raw;
    s_last.vdda_mv = (uint16_t)((CAL_VDDA_MV * VREFINT_CAL) / s_vrefint_raw);
    s_last.temp_centi_c = raw_to_centi_c(raw);

    ADC1->CR2 |= ADC_CR2_SWSTART; /* SQR3 hala kanal 16 */
}

void temp_sensor_poll(temp_sample_t *out)
{
    /* Donusum ~23 us surer; beklemek yerine onceki periyotta baslatilani
     * okuyup hemen yenisini baslatiyoruz. Boylece TelemetryTask hic
     * mesgul-beklemez ve S1-S3'teki CPU suresi pratikte degismez. Ornek
     * bir periyot (10-100 ms) eskidir; sicaklik icin onemsiz. */
    if ((ADC1->SR & ADC_SR_EOC) != 0U)
    {
        uint16_t raw = (uint16_t)ADC1->DR;
        s_last.raw = raw;
        s_last.temp_centi_c = raw_to_centi_c(raw);
        ADC1->CR2 |= ADC_CR2_SWSTART;
    }
    *out = s_last;
}
