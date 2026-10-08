#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx.h"

/* ------------------------------------------------------------------------
 * Zamanlama surumu (A/B/C):
 *   'A': Telemetry 3 > Button 2 > UartTx 1, ortak FIFO
 *   'B': Button 4 > Telemetry 3 > UartTx 1, ortak FIFO
 *   'C': B'nin oncelikleri; aktarim sinirinda bekleyen BTN once (tx_queue.c)
 * Surum calisma aninda yer istasyonundan degistirilir (protocol.h, 'V'
 * komutu); uc surum ayni firmware ile olculur. VARIANT yalnizca ACILISTAKI
 * surumdur. Kart aktif surumu ACK satirinda bildirir.
 * ------------------------------------------------------------------------ */
#ifndef VARIANT
#define VARIANT 'A'
#endif

#if (VARIANT != 'A') && (VARIANT != 'B') && (VARIANT != 'C')
#error "VARIANT 'A', 'B' ya da 'C' olmalidir"
#endif

/* Aktif surum. main() acilista VARIANT ile yazar; sonrasinda YALNIZCA
 * TelemetryTask, komutla, yeni olcum penceresi acarken degistirir. Diger
 * gorevler tek bayt (atomik) okur. */
extern volatile char g_variant;

/* PressTrace FastPath'in buton hizli yolu (uart.c): basis ISR'den dogrudan UART'a gider,
 * ButtonTask'i ve TX kuyrugunu hic kullanmaz. A/B/C'nin olctugu gorev
 * onceligi ve mesaj sirasi bu yolda etkisiz kalacagi icin A/B/C
 * olcumlerinde 0 olmalidir. */
#ifndef BTN_FAST_PATH
#define BTN_FAST_PATH 0
#endif

/* ------------------------------------------------------------------------
 * Senaryo secimi. Senaryo calisma aninda yer istasyonundan degistirilir
 * (protocol.h, komut). ACTIVE_SCENARIO yalnizca ACILISTAKI senaryodur.
 * ------------------------------------------------------------------------ */
#ifndef ACTIVE_SCENARIO
#define ACTIVE_SCENARIO 5 /* 0=S0 ... 6=S6 */
#endif

typedef struct
{
    uint8_t  id;              /* 0..6, TEL/BTN paketlerindeki scenario_id alani */
    uint16_t telemetry_hz;    /* 0 = telemetri kapali (S0) */
    uint16_t extra_load_ms;   /* TelemetryTask icinde hedeflenen ek CPU isi (0, 2 veya 5) */
    uint8_t  tel_per_period;  /* periyot basina art arda uretilen TEL (S6: 4) */
} scenario_config_t;

#define SCENARIO_COUNT 7U

/* S0..S6 tablosu (main.c). */
extern const scenario_config_t g_scenarios[SCENARIO_COUNT];

/* Aktif senaryo. main() acilista ACTIVE_SCENARIO ile yazar; sonrasinda
 * YALNIZCA TelemetryTask, bir periyot sinirinda degistirir. Diger gorevler
 * yalnizca .id alanini (tek bayt, atomik) okur. */
extern scenario_config_t g_scenario;

/* ------------------------------------------------------------------------
 * Deney butcesi
 * ------------------------------------------------------------------------ */
#define RESPONSE_DEADLINE_US   20000U /* R = t4 - t0 <= 20 ms */

/* ------------------------------------------------------------------------
 * Gorev oncelikleri. A: Telemetry (3) > Button (2) > UartTx (1).
 * B ve C'de yalnizca Button 4'e cikar.
 * ------------------------------------------------------------------------ */
#define PRIO_UART_TX_TASK   (tskIDLE_PRIORITY + 1U)
#define PRIO_BUTTON_TASK_A  (tskIDLE_PRIORITY + 2U)
#define PRIO_BUTTON_TASK_BC (tskIDLE_PRIORITY + 4U)
#define PRIO_TELEMETRY_TASK (tskIDLE_PRIORITY + 3U)

#define PRIO_BUTTON_TASK_FOR(v) (((v) == 'A') ? PRIO_BUTTON_TASK_A : PRIO_BUTTON_TASK_BC)

/* ------------------------------------------------------------------------
 * Gorev yigin boyutlari (word = 4 bayt). Yiginlar statik dizilerdir
 * (xTaskCreateStatic); boyut degisince RAM kullanimi linker map
 * dosyasinda gorunur.
 * ------------------------------------------------------------------------ */
/* SystemView acikken (USE_SYSVIEW 1) her kayit cagrisi yigina paket tamponu
 * acar. IAR yigin analizi: Telemetry 584 B, Button 552 B (+ SEGGER'in tek
 * turluk ozyinelemesi ~150 B + kesme/gorev degisimi 208 B ~ 236 ve 228
 * word). 256 word'te pay ~%10 kaliyordu; en az 64 word pay icin 384. */
#define STACK_WORDS_UART_TX_TASK   (configMINIMAL_STACK_SIZE * 4U)
#define STACK_WORDS_BUTTON_TASK    (configMINIMAL_STACK_SIZE * 3U)
#define STACK_WORDS_TELEMETRY_TASK (configMINIMAL_STACK_SIZE * 3U)

/* ------------------------------------------------------------------------
 * Kuyruk derinlikleri
 * ------------------------------------------------------------------------ */
#define BUTTON_EVENT_QUEUE_LEN  8U  /* ISR -> ButtonTask */
#define UART_TX_QUEUE_LEN       16U /* toplam bekleyen TX mesaji siniri (tum surumlerde) */

/* ------------------------------------------------------------------------
 * Donanim atamalari — STM32F407VG Discovery
 * ------------------------------------------------------------------------ */
/* Kullanici butonu B1 (mavi), aktif-HIGH, kart uzerinde harici pull-down. */
#define BUTTON_GPIO_PORT    GPIOA
#define BUTTON_GPIO_PIN     0U
#define BUTTON_EXTI_LINE    0U

/* Gozlem LED'i (yesil, PD12) — ButtonTask her olayi islediginde toggle eder. */
#define LED_GPIO_PORT       GPIOD
#define LED_GPIO_PIN        12U

/* USART2: PA2 = TX, PA3 = RX (AF7). Discovery kartinda USB-UART koprusu
 * YOKTUR; harici bir 3,3 V USB-TTL adaptor gerekir (adaptor RX -> PA2,
 * adaptor TX -> PA3, ortak GND). */
#define UART_PERIPH         USART2
#define UART_TX_GPIO_PORT   GPIOA
#define UART_TX_GPIO_PIN    2U
#define UART_RX_GPIO_PORT   GPIOA
#define UART_RX_GPIO_PIN    3U
#define UART_GPIO_AF        7U

/* UART hizi. Protokol 115200 ile de calisir; olcumler 230400 ile
 * alindi: 64 baytlik satir 5,556 ms -> 2,778 ms,
 * S5'te TEL hattin %55,6'si yerine %27,8'ini kullanir.
 * BRR 42 MHz'den 11,375 (gercek 230 769 baud, +%0,16).
 * 115200'e donmek icin 115200 satirini acip 230400 satirini kapatin;
 * interface/app.js icindeki BAUD_RATE de ayni degere cekilmelidir. */
/* #define UART_BAUDRATE       115200U */
#define UART_BAUDRATE       230400U

/* Tekrar-kenar filtresi: bir kenarin gecerli sayilmasi icin oncesinde
 * gereken kenarsiz (sessiz) sure. Daha yakin gelen kenarlar sicrama
 * (bounce) sayilir, repeat_count'u arttirir. */
#define BUTTON_REPEAT_WINDOW_US  30000U /* 30 ms */

#endif /* MAIN_H */
