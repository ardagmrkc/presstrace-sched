#ifndef SYSTEM_CLOCK_H
#define SYSTEM_CLOCK_H

/*
 * HSE (8 MHz, kart uzerindeki osilator) -> PLL -> 168 MHz SYSCLK.
 * AHB = 168 MHz, APB1 = 42 MHz (/4), APB2 = 84 MHz (/2).
 * Basari ile tamamlandiginda SystemCoreClock (CMSIS) 168000000 olarak
 * guncellenir.
 */
void system_clock_config(void);

#endif /* SYSTEM_CLOCK_H */
