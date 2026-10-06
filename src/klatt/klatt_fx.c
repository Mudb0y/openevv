#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "klatt_fx.h"
#include "klatt_tables.h"

/* The original relies on >> sign-extending negative operands, which C leaves
   implementation-defined. Every compiler we target does this; fail the build
   rather than produce silently wrong audio on one that does not. */
typedef char kfx_needs_arithmetic_shift[((int32_t)-8 >> 1) == -4 ? 1 : -1];

void clr_vector(int32_t *v, int32_t n)
{
    memset(v, 0, (size_t)n * 4u);
}

uint32_t klatt_rand(int16_t *out, int32_t n, uint32_t seed)
{
    int32_t i;

    for (i = 0; i < n; i++) {
        seed = seed * 0x19660du + 0x3c6ef35fu;
        *out++ = (int16_t)(seed & 0xffffu);
    }
    return seed;
}

int16_t fxdivl(int32_t num, int32_t den)
{
    int      positive = 1;
    int16_t  result;
    uint32_t n, q;
    int32_t  shift;

    if (den < 0) {
        den = -den;
        num = -num;
    }
    if (num < 0) {
        positive = 0;
        num = -num;
    }

    /* Saturation is not an early exit in the original: it still runs the sign
       fixup below, so a saturated negative quotient comes back as -32767. */
    if (den == 0 || num >= den) {
        result = 0x7fff;
    } else if (num == 0) {
        result = 0;
    } else {
        /* Normalising stalls forever when num's low 16 bits are all zero,
           because num << 16 is then zero and no shift ever sets bit 31. The
           original has the same hole; the engine only feeds it small
           magnitudes. */
        n = (uint32_t)num << 16;
        shift = 16;
        while ((n & 0x80000000u) == 0) {
            n <<= 1;
            shift++;
        }

        q = n / (uint32_t)den;
        q <<= (31 - shift);

        result = (int16_t)(q >> 16);
        if (q & 0x8000u)
            result = (int16_t)(result + 1);
    }

    if (!positive && result != 0)
        result = (int16_t)(-result);

    return result;
}


void fxmul_vector(const int32_t *src, int16_t coef, int32_t *acc, int32_t n)
{
    int32_t i;

    for (i = 0; i < n; i++)
        acc[i] += fxmul_scaled(coef, src[i]);
}

void fxmul1_vector(const int16_t *src, int16_t coef, int32_t *acc, int32_t n)
{
    int32_t i;

    for (i = 0; i < n; i++)
        acc[i] += fxmul_scaled(coef, (int32_t)src[i] << 4);
}

int32_t db2lin(int32_t db)
{
    int32_t t, quot, rem;

    if (db <= 0)
        return 0;

    /* 299/90 is log2(10) to four places, so this is dB expressed in
       twentieths of an octave, clamped at 20 doublings. */
    t = mul32(db, 299) / 90;
    if (t >= 400)
        t = 400;

    quot = t / 20;
    rem = t % 20;

    return fxmul_scaled(klatt_fxl2[rem], 2 << quot);
}

const char KlattVersionString[] =
    "\r\nKlattID version 4.0 \xa9 International Business Machines, Inc. "
    "1996, 1997 \r\n";

int verifyKlattHandle(void *handle)
{
    return strcmp(*(char **)handle, KlattVersionString) == 0;
}

typedef char filter_parms_is_84_bytes[sizeof(filter_parms) == 84 ? 1 : -1];

/* A two-pole resonator. It keeps its history in the sample buffer itself,
   two slots ahead of the pointer it was handed, rather than in locals.
   The three products are weighted 1, 2 and 4 on the way out, so the three
   coefficients are held at three different fixed-point scales. */
void pole_filter(filter_parms *fp, int32_t *buf, int32_t n)
{
    int32_t i, count, k, t1, t2, t3;

    if (fp->enabled == 0)
        return;

    buf[-2] = fp->d2;
    buf[-1] = fp->d1;
    i = 0;

    if (fp->ramp != 0) {
        count = fp->ramp < n ? fp->ramp : n;
        k = 3 - fp->ramp;

        for (; i < count; i++) {
            t1 = fxmul_scaled(fp->c[k], buf[i - 2]);
            t2 = fxmul_scaled(fp->b[k], buf[i - 1]);
            t3 = fxmul_scaled(fp->a[k], buf[i]);
            buf[i] = t1 + t2 * 2 + t3 * 4;
            k++;
        }
        fp->ramp -= count;
    }

    for (; i < n; i++) {
        t1 = fxmul_scaled(fp->sc, buf[i - 2]);
        t2 = fxmul_scaled(fp->sb, buf[i - 1]);
        t3 = fxmul_scaled(fp->sa, buf[i]);
        buf[i] = t1 + t2 * 2 + t3 * 4;
    }

    if (n > 1) {
        fp->d2 = buf[i - 2];
        fp->d1 = buf[i - 1];
    } else {
        fp->d2 = fp->d1;
        fp->d1 = buf[i - 1];
    }
}

/* The same resonator with no input term and no ramp: it runs purely on its
   own history, which is what the parallel branch wants when the excitation is
   summed in somewhere else. */
void parallel0_filter(filter_parms *fp, int32_t *buf, int32_t n)
{
    int32_t i, t1, t2;

    buf[-2] = fp->d2;
    buf[-1] = fp->d1;

    for (i = 0; i < n; i++) {
        t1 = fxmul_scaled(fp->sc, buf[i - 2]);
        t2 = fxmul_scaled(fp->sb, buf[i - 1]);
        buf[i] = t1 + t2 * 2;
    }

    if (n > 1) {
        fp->d2 = buf[i - 2];
        fp->d1 = buf[i - 1];
    } else {
        fp->d2 = fp->d1;
        fp->d1 = buf[i - 1];
    }
}

void zero_filter(filter_parms *fp, const zero_ABCs *z, int32_t *buf, int32_t n)
{
    int32_t p1, p2, x, i, count, k;

    if (fp->enabled == 0)
        return;

    p2 = fp->d2;
    p1 = fp->d1;
    i = 0;

    /* While the ramp is live the coefficients come from the three-entry
       tables, one entry per sample, so a parameter change slides in instead
       of stepping. ramp above 3 would index off the front of them. */
    if (fp->ramp != 0) {
        count = fp->ramp < n ? fp->ramp : n;
        k = 3 - fp->ramp;

        for (; i < count; i++) {
            x = buf[i];
            buf[i] = (mul32(fp->a[k], x) >> 4)
                   + (mul32(fp->b[k], p1) >> 4)
                   + (mul32(fp->c[k], p2) >> 4);
            k++;
            p2 = p1;
            p1 = x;
        }
        fp->ramp -= count;
    }

    for (; i < n; i++) {
        x = buf[i];
        buf[i] = (mul32(z->a, x) >> 4)
               + (mul32(z->b, p1) >> 4)
               + (mul32(z->c, p2) >> 4);
        p2 = p1;
        p1 = x;
    }

    /* With n of zero the original saves the untouched d1 into d2 rather than
       the real d2, so a zero-length call is not a no-op. */
    if (n > 1) {
        fp->d2 = p2;
        fp->d1 = p1;
    } else {
        fp->d2 = fp->d1;
        fp->d1 = p1;
    }
}

/* ---- shaping the noise for a rate above the engine's own ---------------
 *
 * The frication and aspiration source is white: klatt_rand puts one value
 * per output sample in the buffer and nothing bounds it, so the noise
 * occupies whatever band the rate gives it. At 11,025 that is right by
 * accident -- Nyquist is 5,512 and the source is meant to stop there. Ask
 * the synthesiser for 22,050 and the same code spreads the same energy over
 * twice the band; at 44,100 there is as much of it above 11 kHz as between
 * 5.5 and 11, which is why sibilants synthesised outright go thin and hissy
 * and why raising the rate afterwards has been the only way up.
 *
 * So bound it where the engine's own rate bounds it. Two poles at 5,512
 * hertz, and a gain of sqrt(rate / 11025) to put back the power the band
 * limit takes out: white noise at rate R carries its variance over R/2 of
 * spectrum, so confining it to a fixed 5,512 without that gain would leave
 * the band quieter than 11,025 leaves it rather than the same.
 *
 * At 11,025 and below this does nothing at all, by the test below rather
 * than by the arithmetic coming out to unity. That is deliberate: it is what
 * keeps every recorded case byte for byte what it was.
 *
 * EVV_NOISE=flat asks for the old behaviour at any rate, which is what the
 * two are measured against. */

#define NOISE_BAND 5512

static int noise_shaping(void)
{
    static int decided, on = 1;

    if (!decided) {
        const char *say = getenv("EVV_NOISE");

        decided = 1;
        if (say != 0 && strcmp(say, "flat") == 0)
            on = 0;
    }
    return on;
}

/* Fourth-order Butterworth, as two biquads. A gentler filter is no use
   here: the whole transition band is the one octave between 5,512 and the
   new Nyquist at 22,050, so twelve decibels an octave arrives at Nyquist
   having taken almost nothing off. Twenty-four is the least that bites.

   Designed at the rate rather than stored, the way klatt_rates.c builds the
   resonator tables, because the rate is not known until the caller asks. */
void klatt_shape_noise(int16_t *buf, int32_t n, int32_t rate, double *z)
{
    static const double q[2] = { 0.54119610, 1.30656296 };
    double b[2][3], a[2][2], g;
    int32_t i;
    int s;

    if (n <= 0 || rate <= 11025 || !noise_shaping())
        return;

    for (s = 0; s < 2; s++) {
        double w0 = 6.283185307179586 * (double)NOISE_BAND / (double)rate;
        double c = cos(w0), al = sin(w0) / (2.0 * q[s]);
        double a0 = 1.0 + al;

        b[s][0] = (1.0 - c) / 2.0 / a0;
        b[s][1] = (1.0 - c) / a0;
        b[s][2] = b[s][0];
        a[s][0] = -2.0 * c / a0;
        a[s][1] = (1.0 - al) / a0;
    }

    /* White noise carries its variance over the whole band it is given, so
       confining it to a fixed 5,512 leaves less power in that band than
       11,025 leaves there. This puts it back. */
    g = sqrt((double)rate / 11025.0);

    for (i = 0; i < n; i++) {
        double v = buf[i];

        for (s = 0; s < 2; s++) {
            double *w = z + s * 2;
            double y = b[s][0] * v + w[0];

            w[0] = b[s][1] * v - a[s][0] * y + w[1];
            w[1] = b[s][2] * v - a[s][1] * y;
            v = y;
        }
        v *= g;
        if (v > 32767.0)
            v = 32767.0;
        else if (v < -32768.0)
            v = -32768.0;
        buf[i] = (int16_t)v;
    }
}
