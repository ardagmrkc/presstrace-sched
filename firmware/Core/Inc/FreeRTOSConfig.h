#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*
 * STM32F407VG (Cortex-M4F) + IAR EWARM + FreeRTOS-Kernel (portable/IAR/ARM_CM4F)
 * icin daraltilmis yapilandirma. Tam aciklamali sablon icin
 * Middlewares/FreeRTOS-Kernel/examples/template_configuration/FreeRTOSConfig.h
 * dosyasina bakin; burada yalnizca bu proje icin gerekli/anlamli olan
 * secenekler tutulmustur (MPU/SMP/TrustZone bolumleri cikarilmistir —
 * plain, tek cekirdekli, MPU'suz ARM_CM4F portu kullaniliyor).
 */

/* ---- Donanim / zamanlama ---- */
#define configCPU_CLOCK_HZ                         ( ( unsigned long ) 168000000 )
#define configTICK_RATE_HZ                         ( ( TickType_t ) 1000 ) /* 1 ms tick */

/* ---- Zamanlama davranisi ---- */
#define configUSE_PREEMPTION                       1
#define configUSE_TIME_SLICING                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION    1
#define configUSE_TICKLESS_IDLE                    0
#define configMAX_PRIORITIES                       5 /* 0=idle, 1=UartTx, 2=Button (A), 3=Telemetry, 4=Button (B/C) */
#define configMINIMAL_STACK_SIZE                   128 /* word; idle gorevi icin */
#define configMAX_TASK_NAME_LEN                    16
#define configTICK_TYPE_WIDTH_IN_BITS              TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                    1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES      1
#define configQUEUE_REGISTRY_SIZE                  0
#define configENABLE_BACKWARD_COMPATIBILITY        0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS    0
#define configSTACK_DEPTH_TYPE                     uint16_t

/* ---- Yazilim zamanlayicilari: kullanilmiyor ---- */
#define configUSE_TIMERS                           0

/* ---- Event group / stream buffer: kullanilmiyor ama zararsiz, kapatildi ---- */
#define configUSE_EVENT_GROUPS                     0
#define configUSE_STREAM_BUFFERS                   0

/* ---- Bellek ayirma: YALNIZCA STATIK. Gorevler xTaskCreateStatic, kuyruklar
 *      xQueueCreateStatic ile derleme aninda ayrilmis global dizileri
 *      kullanir. FreeRTOS heap'i yoktur: projede heap_x.c dosyasi bulunmaz,
 *      xTaskCreate/xQueueCreate gibi dinamik API'ler derlenmez. Idle
 *      gorevinin statik bellegini cekirdek saglar. ---- */
#define configSUPPORT_STATIC_ALLOCATION             1
#define configKERNEL_PROVIDED_STATIC_MEMORY          1
#define configSUPPORT_DYNAMIC_ALLOCATION            0

/* ------------------------------------------------------------------------
 * Cortex-M4 kesme onceligi ayarlari (RTOS-Cortex-M3-M4.html).
 *
 * STM32F4 NVIC 4 oncelik biti uygular (16 seviye: 0..15, 0=en yuksek).
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, FromISR API'lerini
 * cagirabilecek EN YUKSEK (en dusuk numarali) onceliktir; button.c ve
 * uart.c bu HAM (kaydirilmamis) degeri dogrudan CMSIS NVIC_SetPriority()
 * ile kullanir. configKERNEL_INTERRUPT_PRIORITY ve
 * configMAX_SYSCALL_INTERRUPT_PRIORITY ise port.c'nin SHPR
 * register'larina yazdigi, 8-bit register bicimine ONCEDEN KAYDIRILMIS
 * degerlerdir — bu iki degeri app kodunda dogrudan NVIC_SetPriority()
 * ile KARISTIRMAYIN (klasik cifte-kaydirma hatasi).
 * ------------------------------------------------------------------------ */
#define configPRIO_BITS                                  4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY          15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY     5

#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_API_CALL_INTERRUPT_PRIORITY configMAX_SYSCALL_INTERRUPT_PRIORITY

/* Direct Routing (bkz. Core/Startup/startup_stm32f407xx.s): vektor
 * tablosu SVCall/PendSV/SysTick icin dogrudan vPortSVCHandler /
 * xPortPendSVHandler / xPortSysTickHandler'a isaret eder. */
#define configCHECK_HANDLER_INSTALLATION           1

/* ---- Kanca (hook) fonksiyonlari (bkz. Core/Src/main.c) ---- */
#define configUSE_IDLE_HOOK                        0
#define configUSE_TICK_HOOK                        0
#define configUSE_MALLOC_FAILED_HOOK                0 /* heap yok */
#define configCHECK_FOR_STACK_OVERFLOW              2 /* vApplicationStackOverflowHook main.c'de */

/* ---- Calisma zamani istatistikleri: kullanilmiyor ---- */
#define configGENERATE_RUN_TIME_STATS               0
#define configUSE_TRACE_FACILITY                    0
#define configUSE_STATS_FORMATTING_FUNCTIONS        0

/* ---- Co-routine: kullanilmiyor ---- */
#define configUSE_CO_ROUTINES                       0

/* ---- configASSERT: hata aninda kesmeleri kapat ve dur (debugger'da
 *      cagrinin geldigi satiri gormek icin buraya breakpoint konabilir) ---- */
#define configASSERT( x )         \
    if( ( x ) == 0 )              \
    {                             \
        taskDISABLE_INTERRUPTS(); \
        for( ; ; )                \
        ;                         \
    }

/* ---- Dahil edilecek ozellikler ---- */
#define configUSE_TASK_NOTIFICATIONS                1 /* uart_tx_task.c: t4 tasima */
#define configUSE_MUTEXES                           0
#define configUSE_RECURSIVE_MUTEXES                 0
#define configUSE_COUNTING_SEMAPHORES               1 /* tx_queue.c: 16 yer + bekleyen mesaj sayaci */
#define configUSE_QUEUE_SETS                        0
#define configUSE_APPLICATION_TASK_TAG               0
#define configUSE_POSIX_ERRNO                        0

#define INCLUDE_vTaskPrioritySet                    1 /* surum degisimi: ButtonTask 2 <-> 4 */
#define INCLUDE_uxTaskPriorityGet                   0
#define INCLUDE_vTaskDelete                         0
#define INCLUDE_vTaskSuspend                        1 /* portMAX_DELAY = gercekten sonsuz bekleme (S0'da komut beklerken) */
#define INCLUDE_xTaskDelayUntil                     1 /* telemetry_task.c: periyodik gonderim */
#define INCLUDE_vTaskDelay                          1
#define INCLUDE_xTaskGetSchedulerState               0
#define INCLUDE_xTaskGetCurrentTaskHandle            1 /* uart_tx_task.c */
#define INCLUDE_uxTaskGetStackHighWaterMark          1 /* uart_tx_task.c: dokumde stack_hwm_* sayaclari */
#define INCLUDE_xTaskGetIdleTaskHandle                1 /* SystemView: Idle'i tutamaciyla tanir */
#define INCLUDE_eTaskGetState                        0
#define INCLUDE_xTimerPendFunctionCall                0
#define INCLUDE_xTaskAbortDelay                      0
#define INCLUDE_xTaskGetHandle                        0
#define INCLUDE_xTaskResumeFromISR                    0

/* ---- SEGGER SystemView, J-Link + RTT ----
 * 1: izleme acik. 0: izleme kodu hic derlenmez; izleme maliyeti karsilastirmasinin
 * "kapali" tarafi bununla alinir. Kart
 * izleme acikken ACK'teki surumun sonuna T ekler (ornek: "AT").
 * TRACERETURN_ENABLE 0: FreeRTOS API cagrilarinin donus olaylari kaydedilmez
 * (giris olaylari kalir); veri hacmi ve izleme maliyeti azalir. */
#define USE_SYSVIEW                                  1

#if (USE_SYSVIEW == 1) && defined(__ICCARM__) /* portasm.s de bu dosyayi okur */
#define TRACERETURN_ENABLE                           0
#include "SEGGER_SYSVIEW_FreeRTOS.h"
#endif

#endif /* FREERTOS_CONFIG_H */
