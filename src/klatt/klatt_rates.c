#include <math.h>

#include "klatt_rates.h"

/* Fifteen-bit fixed point, saturating. One counts as 32,768, which does not
   fit, so a pole of zero bandwidth would land on the largest value there is
   rather than wrap to the most negative one. */
static int16_t q15(double v)
{
    double scaled = v * 32768.0;

    if (scaled >= 32767.0)
        return 32767;
    if (scaled <= -32768.0)
        return -32768;
    return (int16_t)(scaled < 0 ? -floor(-scaled + 0.5) : floor(scaled + 0.5));
}

int klatt_buildRateTables(int32_t rate, int16_t *ex, int16_t *co)
{
    const double pi = 3.14159265358979323846;
    int32_t hz;

    if (rate < KLATT_RATE_MIN || rate > KLATT_RATE_MAX || !ex || !co)
        return 0;

    for (hz = KLATT_EX_FIRST; hz <= KLATT_EX_LAST; hz++)
        ex[hz - KLATT_EX_FIRST] = q15(exp(-pi * (double)hz / (double)rate));

    for (hz = KLATT_CO_FIRST; hz <= KLATT_CO_LAST; hz++)
        co[hz - KLATT_CO_FIRST] = q15(cos(2.0 * pi * (double)hz / (double)rate));

    return 1;
}
