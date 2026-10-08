#ifndef MOCK_BTN_STATS_H
#define MOCK_BTN_STATS_H
#include <stdint.h>
static inline void stats_droplog_add_from_isr(uint16_t id, uint8_t sc, uint32_t t0) { (void)id; (void)sc; (void)t0; }
#endif
