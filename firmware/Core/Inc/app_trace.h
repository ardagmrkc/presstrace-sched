#ifndef APP_TRACE_H
#define APP_TRACE_H

#include <stdint.h>
#include "FreeRTOS.h"

/*
 * SEGGER SystemView uygulama olaylari (uygulama
 * isaretleri). Ayni basisi uctan uca izlemek icin her olay event_id tasir.
 * Olaylar olcum noktasinin hemen SONRASINA konur: t0..t4 zamanlari
 * kaymaz, izlemenin maliyeti bir sonraki araliga duser.
 *
 *   SystemView'de    olay etiketi   nokta  yer
 *   BTN ACCEPT #id   BTN_ACCEPT     t0     EXTI0 ISR, kabul edilen basis (Mark baslar)
 *   BTN TASK #id     BTN_TASK       t1     ButtonTask olayi aldi
 *   BTN READY #id    BTN_READY      t2     BTN mesaji hazir, TX kuyruguna verilecek
 *   BTN ENQUEUE #id  BTN_ENQUEUE           TX kuyruguna girdi; q = bekleyen mesaj
 *   BTN DROP #id     BTN_DROP              dustu; at 1 = buton kuyrugu, 2 = TX kuyrugu
 *   BTN TX_START #id BTN_TX_START   t3     UART aktarimi basliyor
 *   BTN TX_TC #id    BTN_TX_TC      t4     TC kesmesi, son bit hattan cikti (Mark biter)
 *   "Pencere A/S5"                         yeni olcum penceresi (surum/senaryo)
 *
 * USE_SYSVIEW (FreeRTOSConfig.h) 0 iken hepsi bos makrodur: izleme
 * kapali/acik olcumu ayni kaynaktan derlenir.
 */
#ifndef USE_SYSVIEW
#define USE_SYSVIEW 0
#endif

#if USE_SYSVIEW

#include "SEGGER_SYSVIEW.h"

enum
{
    APP_EV_ACCEPT = 0,
    APP_EV_TASK,
    APP_EV_READY,
    APP_EV_ENQUEUE,
    APP_EV_DROP,
    APP_EV_TX_START,
    APP_EV_TX_TC,
    APP_EV_COUNT
};

#define APP_DROP_AT_BUTTON_QUEUE 1U
#define APP_DROP_AT_TX_QUEUE     2U

extern SEGGER_SYSVIEW_MODULE g_app_trace_module;

/* SEGGER_SYSVIEW_Conf() icinde, SEGGER_SYSVIEW_Init() sonrasi cagrilir. */
void app_trace_register(void);

/* Yeni olcum penceresi: SystemView'e "Pencere <surum>/S<n>" yazar. */
void app_trace_window(char variant, uint8_t scenario);

#define APP_EV_ID(e) (g_app_trace_module.EventOffset + (unsigned)(e))

#define APP_TRACE_BTN_ACCEPT(id, sc)                                                      \
    do                                                                                    \
    {                                                                                     \
        SEGGER_SYSVIEW_RecordU32x2(APP_EV_ID(APP_EV_ACCEPT), (U32)(id), (U32)(sc));       \
        SEGGER_SYSVIEW_MarkStart((unsigned)(id));                                         \
    } while (0)
#define APP_TRACE_BTN_TASK(id)     SEGGER_SYSVIEW_RecordU32(APP_EV_ID(APP_EV_TASK), (U32)(id))
#define APP_TRACE_BTN_READY(id)    SEGGER_SYSVIEW_RecordU32(APP_EV_ID(APP_EV_READY), (U32)(id))
#define APP_TRACE_BTN_ENQUEUE(id, q) \
    SEGGER_SYSVIEW_RecordU32x2(APP_EV_ID(APP_EV_ENQUEUE), (U32)(id), (U32)(q))
#define APP_TRACE_BTN_DROP(id, at)                                                        \
    do                                                                                    \
    {                                                                                     \
        SEGGER_SYSVIEW_RecordU32x2(APP_EV_ID(APP_EV_DROP), (U32)(id), (U32)(at));         \
        SEGGER_SYSVIEW_MarkStop((unsigned)(id));                                          \
    } while (0)
#define APP_TRACE_BTN_TX_START(id) SEGGER_SYSVIEW_RecordU32(APP_EV_ID(APP_EV_TX_START), (U32)(id))
#define APP_TRACE_BTN_TX_TC(id)                                                           \
    do                                                                                    \
    {                                                                                     \
        SEGGER_SYSVIEW_RecordU32(APP_EV_ID(APP_EV_TX_TC), (U32)(id));                     \
        SEGGER_SYSVIEW_MarkStop((unsigned)(id));                                          \
    } while (0)
#define APP_TRACE_WINDOW(v, sc)    app_trace_window((v), (sc))

#else /* USE_SYSVIEW */

#define APP_TRACE_BTN_ACCEPT(id, sc) ((void)0)
#define APP_TRACE_BTN_TASK(id)       ((void)0)
#define APP_TRACE_BTN_READY(id)      ((void)0)
#define APP_TRACE_BTN_ENQUEUE(id, q) ((void)0)
#define APP_TRACE_BTN_DROP(id, at)   ((void)0)
#define APP_TRACE_BTN_TX_START(id)   ((void)0)
#define APP_TRACE_BTN_TX_TC(id)      ((void)0)
#define APP_TRACE_WINDOW(v, sc)      ((void)0)

#endif /* USE_SYSVIEW */

#endif /* APP_TRACE_H */
