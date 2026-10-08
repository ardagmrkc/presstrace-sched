#ifndef MOCK_COMMAND_H
#define MOCK_COMMAND_H
#include "FreeRTOS.h"
static inline void command_rx_byte_from_isr(uint8_t b, BaseType_t *w) { (void)b; (void)w; }
#endif
