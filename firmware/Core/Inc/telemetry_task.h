#ifndef TELEMETRY_TASK_H
#define TELEMETRY_TASK_H

#include <stdint.h>
#include "FreeRTOS.h"

/* Oncelik 3 gorev (A'da en yuksek; B/C'de ButtonTask'in altinda).
 * g_scenario.telemetry_hz periyodunda g_scenario.tel_per_period TEL paketi
 * uretir (S6: 100 ms'de 4); S4/S5'te periyot basina ~Cek ms'lik ek CPU isini
 * (workload_run) de bu gorev icinde calistirir — boylece Telemetry, daha
 * dusuk oncelikli gorevleri kesintiye ugratarak yapay CPU yuku senaryosunu
 * olusturur. Yer istasyonunun senaryo komutlarini da (command.c uzerinden
 * gelen bildirim) periyot sinirinda uygular ve ACK ile yanitlar. */
void telemetry_task_create(void);

/* Gorev yigininda acilistan beri en az bos kalan alan (word). */
UBaseType_t telemetry_task_stack_free(void);

/* Olcum penceresi icin gercek telemetri uretim periyodu (ardisik iki
 * periyodun baslangici arasindaki sure). Pencere, senaryo secilince
 * sifirlanir. Yalnizca telemetri durmusken (dokum sirasinda) okunmalidir. */
typedef struct
{
    uint32_t sent;      /* TX kuyruguna giren TEL */
    uint32_t dropped;   /* TX kuyrugu dolu oldugu icin dusen TEL */
    uint32_t periods;   /* olculen periyot sayisi */
    uint32_t min_us;
    uint32_t max_us;
    uint64_t sum_us;
} telemetry_stats_t;

void telemetry_get_stats(telemetry_stats_t *out);

#endif /* TELEMETRY_TASK_H */
