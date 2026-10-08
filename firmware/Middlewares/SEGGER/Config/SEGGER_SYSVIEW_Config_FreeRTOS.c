/*********************************************************************
*                    SEGGER Microcontroller GmbH                     *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*            (c) 1995 - 2024 SEGGER Microcontroller GmbH             *
*                                                                    *
*       www.segger.com     Support: support@segger.com               *
*                                                                    *
**********************************************************************
*                                                                    *
*       SEGGER SystemView * Real-time application analysis           *
*                                                                    *
**********************************************************************
*                                                                    *
* All rights reserved.                                               *
*                                                                    *
* SEGGER strongly recommends to not make any changes                 *
* to or modify the source code of this software in order to stay     *
* compatible with the SystemView and RTT protocol, and J-Link.       *
*                                                                    *
* Redistribution and use in source and binary forms, with or         *
* without modification, are permitted provided that the following    *
* condition is met:                                                  *
*                                                                    *
* o Redistributions of source code must retain the above copyright   *
*   notice, this condition and the following disclaimer.             *
*                                                                    *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
* DISCLAIMED. IN NO EVENT SHALL SEGGER Microcontroller BE LIABLE FOR *
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR           *
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  *
* OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;    *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF      *
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT          *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE  *
* USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH   *
* DAMAGE.                                                            *
*                                                                    *
**********************************************************************
-------------------------- END-OF-HEADER -----------------------------

File    : SEGGER_SYSVIEW_Config_FreeRTOS.c
Purpose : Sample setup configuration of SystemView with FreeRTOS.
Revision: $Rev: 7745 $

Projeye uyarlama (PressTrace, STM32F407VG Discovery): Sample/FreeRTOSV11/
Config/Cortex-M dosyasindan alindi. Degisenler: uygulama/cihaz adi, RAM
tabani (SRAM 0x20000000), kesme adlari, DWT cevrim sayacinin acilmasi ve
uygulama olay modulunun (app_trace.c) kaydi.
*/
#include "FreeRTOS.h"

#if USE_SYSVIEW   // PressTrace: USE_SYSVIEW 0 (FreeRTOSConfig.h) iken bu dosya bos derlenir

#include "SEGGER_SYSVIEW.h"
#include "app_trace.h"

extern const SEGGER_SYSVIEW_OS_API SYSVIEW_X_OS_TraceAPI;

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
// The application name to be displayed in SystemViewer
#define SYSVIEW_APP_NAME        "PressTrace sched"

// The target device name
#define SYSVIEW_DEVICE_NAME     "STM32F407VG"

// Frequency of the timestamp. Must match SEGGER_SYSVIEW_GET_TIMESTAMP in SEGGER_SYSVIEW_Conf.h
#define SYSVIEW_TIMESTAMP_FREQ  (configCPU_CLOCK_HZ)

// System Frequency. SystemcoreClock is used in most CMSIS compatible projects.
#define SYSVIEW_CPU_FREQ        configCPU_CLOCK_HZ

// The lowest RAM address used for IDs (pointers)
// Gorevler, kuyruklar ve semaforlar SRAM'de (0x20000000); CCM (0x10000000)
// kullanilmiyor.
#define SYSVIEW_RAM_BASE        (0x20000000)

// DWT cevrim sayaci (SEGGER_SYSVIEW_GET_TIMESTAMP bunu okur). FreeRTOS
// ornegi sayaci acmaz; acik degilse tum zaman damgalari 0 olur.
#define DEMCR                   (*(volatile U32*)(0xE000EDFCuL))
#define DWT_CTRL                (*(volatile U32*)(0xE0001000uL))
#define TRCENA_BIT              (1uL << 24)
#define NOCYCCNT_BIT            (1uL << 25)
#define CYCCNTENA_BIT           (1uL << 0)

/*********************************************************************
*
*       _cbSendSystemDesc()
*
*  Function description
*    Sends SystemView description strings.
*    Kesme numaralari: istisna numarasi = 16 + IRQn.
*/
static void _cbSendSystemDesc(void) {
  SEGGER_SYSVIEW_SendSysDesc("N="SYSVIEW_APP_NAME",D="SYSVIEW_DEVICE_NAME",O=FreeRTOS");
  SEGGER_SYSVIEW_SendSysDesc("I#11=SVCall,I#14=PendSV,I#15=SysTick");
  SEGGER_SYSVIEW_SendSysDesc("I#22=EXTI0 (buton),I#54=USART2 (UART)");
}

/*********************************************************************
*
*       Global functions
*
**********************************************************************
*/
void SEGGER_SYSVIEW_Conf(void) {
  if ((DEMCR & TRCENA_BIT) == 0) {
    DEMCR |= TRCENA_BIT;
  }
  if ((DWT_CTRL & NOCYCCNT_BIT) == 0) {       // Cycle counter supported?
    if ((DWT_CTRL & CYCCNTENA_BIT) == 0) {    // Cycle counter not enabled?
      DWT_CTRL |= CYCCNTENA_BIT;              // Enable Cycle counter
    }
  }
  SEGGER_SYSVIEW_Init(SYSVIEW_TIMESTAMP_FREQ, SYSVIEW_CPU_FREQ,
                      &SYSVIEW_X_OS_TraceAPI, _cbSendSystemDesc);
  SEGGER_SYSVIEW_SetRAMBase(SYSVIEW_RAM_BASE);
  app_trace_register();

#if SEGGER_SYSVIEW_START_ON_INIT
  SEGGER_SYSVIEW_Start();
#endif
}

#endif // USE_SYSVIEW

/*************************** End of file ****************************/
