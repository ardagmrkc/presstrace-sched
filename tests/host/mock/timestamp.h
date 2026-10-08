#ifndef MOCK_TIMESTAMP_H
#define MOCK_TIMESTAMP_H
#include <stdint.h>
extern uint32_t g_now;
static inline uint32_t timestamp_now_us(void) { return g_now; }
#endif
