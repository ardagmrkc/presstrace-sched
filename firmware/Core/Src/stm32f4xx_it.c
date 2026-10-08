/*
 * Uygulamaya ozel kesme (IRQ) isleyicileri.
 *
 * NMI/HardFault/MemManage/BusFault/UsageFault icin Core/Startup dosyasindaki
 * varsayilan (weak) sonsuz-donguye giren saplamalar kullanilir — bu proje
 * icin yeterlidir; ileride gerekirse burada override edilebilir.
 *
 * SVC_Handler/PendSV_Handler/SysTick_Handler BURADA TANIMLANMAZ: bu proje
 * FreeRTOS'un "Direct Routing" yontemini kullanir, vektor tablosu
 * (startup_stm32f407xx.s) bu uc kesmeyi dogrudan FreeRTOS'un kendi
 * vPortSVCHandler/xPortPendSVHandler/xPortSysTickHandler fonksiyonlarina
 * yonlendirir. Bkz. FreeRTOSConfig.h#configCHECK_HANDLER_INSTALLATION.
 */
#include "main.h"
#include "button.h"
#include "uart.h"

/* SystemView (USE_SYSVIEW 1): kesme girisi (traceISR_ENTER) isleyicilerin
 * icinde, zaman damgasi (t0 / t4) alindiktan SONRA kaydedilir; izleme olcum
 * noktalarini kaydirmaz. Cikisi isleyicinin sonundaki portYIELD_FROM_ISR
 * yazar (traceISR_EXIT / traceISR_EXIT_TO_SCHEDULER, portmacro.h).
 * USART2'nin TC, TXE ve RX kesmelerinin hepsi kaydedilir (uart.c). */

void EXTI0_IRQHandler(void)
{
    button_exti_isr_handler();
}

void USART2_IRQHandler(void)
{
    uart_isr_handler();
}
