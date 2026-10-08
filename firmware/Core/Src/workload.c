#include "workload.h"

/* volatile biriktirici: dongunun derleyici tarafindan sadelestirilip
 * atilmasini (dead-code elimination) engeller, boylece iterasyon basina
 * gecen sure kalibrasyon sirasinda tutarli/olculebilir kalir. */
uint32_t workload_run(uint32_t iterations)
{
    volatile uint32_t acc = 0;
    for (uint32_t i = 0; i < iterations; i++)
    {
        uint32_t a = acc; /* iterasyon basina tek volatile okuma: sira tanimli */
        acc = a + (i ^ (a << 1)) + 1U;
    }
    return acc;
}
