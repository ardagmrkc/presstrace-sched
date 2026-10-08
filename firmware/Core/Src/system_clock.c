#include "system_clock.h"
#include "main.h"

/* STM32F407VG Discovery: HSE = 8 MHz.
 * PLL: VCO_in = HSE/PLLM = 1 MHz, VCO_out = VCO_in*PLLN = 336 MHz,
 *      SYSCLK = VCO_out/PLLP = 168 MHz, USB/SDIO/RNG saati = VCO_out/PLLQ = 48 MHz. */
#define PLL_M   8U
#define PLL_N   336U
#define PLL_P   2U  /* PLLP register kodlamasi: 00b -> /2 */
#define PLL_Q   7U

void system_clock_config(void)
{
    /* 1) HSE'yi devreye al ve kararli olmasini bekle. */
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U)
    {
        /* bekle */
    }

    /* 2) Guc ayarlari: 168 MHz icin PWR olcek 1 (VOS=1) gerekir. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS;

    /* 3) Flash bekleme durumlari (168 MHz, VDD>=2.7V icin 5 WS, RM0090 Tablo 10)
     *    ve onbellekleri/on-getirmeyi etkinlestir. */
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN |
                 (5U << FLASH_ACR_LATENCY_Pos);

    /* 4) PLL yapilandirmasi (HSE kaynakli). */
    RCC->PLLCFGR = (PLL_M << RCC_PLLCFGR_PLLM_Pos) |
                   (PLL_N << RCC_PLLCFGR_PLLN_Pos) |
                   (((PLL_P / 2U) - 1U) << RCC_PLLCFGR_PLLP_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSE |
                   (PLL_Q << RCC_PLLCFGR_PLLQ_Pos);

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U)
    {
        /* bekle */
    }

    /* 5) Veri yolu bolucileri: AHB=/1 (168 MHz), APB1=/4 (42 MHz), APB2=/2 (84 MHz). */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
                RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    /* 6) Sistem saat kaynagini PLL'ye gecir. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
    {
        /* bekle */
    }

    /* CMSIS SystemCoreClock degiskenini (168000000) guncelle; FreeRTOS ve
     * uart/timestamp modulleri bu degeri degil sabit makrolari kullanir,
     * ancak baska kod bu degiskene guvenebilir. */
    SystemCoreClockUpdate();
}
