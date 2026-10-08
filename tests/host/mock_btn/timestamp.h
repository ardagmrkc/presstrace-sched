#ifndef MOCK_BTN_TIMESTAMP_H
#define MOCK_BTN_TIMESTAMP_H
#include <stdint.h>
extern uint32_t g_now_us;
static inline uint32_t timestamp_now_us(void) { return g_now_us; }
#endif
