#ifndef STATS_H
#define STATS_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

/*
 * Olcum penceresi sayaclari ve dusen olay kaydi. TelemetryTask, ButtonTask
 * ve buton ISR'si yazdigi icin kritik bolumle korunur. Pencere basinda
 * (senaryo secimi) UartTxTask stats_window_reset() ile sifirlar.
 */
void stats_window_reset(void);

void stats_record_tx_drop(void);           /* TX kuyrugu dolu (TEL veya BTN) */
void stats_note_tx_depth(uint32_t depth);  /* basarili gonderimden sonraki doluluk */
uint32_t stats_get_tx_drop(void);
uint32_t stats_get_tx_queue_max(void);

/* Kuyruga hic giremeyen olaylar: kimlik ve bilinen zamanlar korunur,
 * olcum sonunda REC satiri olarak gonderilir (olcum kurali: kuyruga
 * girmeden dusen olay da kimligi ve t0 degeriyle raporlanir). */
#define DROPLOG_SIZE 32U

typedef struct
{
    uint16_t event_id;
    uint8_t scenario_id;
    rec_status_t status; /* REC_STATUS_BTN_DROP (yalniz t0) ya da REC_STATUS_TX_DROP (t0..t2) */
    uint8_t known;
    uint32_t t[3];
} drop_record_t;

void stats_droplog_add_from_isr(uint16_t event_id, uint8_t scenario_id, uint32_t t0);
void stats_droplog_add(uint16_t event_id, uint8_t scenario_id, uint32_t t0, uint32_t t1, uint32_t t2);
uint32_t stats_droplog_count(void);
bool stats_droplog_get(uint32_t index, drop_record_t *out);
uint32_t stats_droplog_overflow(void);

#endif /* STATS_H */
