/* The C library's exp, log, exp2 and pow, as a rule lifted from 6.1 calls
 * them, in fixed values and whole numbers.
 *
 * 6.1 does its durations and its pitch in doubles and calls the C library
 * on a few of them: insert_f0 and apply_speed_val on exp, f0_step on log,
 * apply_pitch_baseline_val on exp2 and modulate_pwind on pow. The machine
 * here keeps those doubles as ten-thousandths so that every language runs on
 * a processor with no floating point, which these four are therefore written
 * for: sixty-four bit whole numbers, the argument and the answer each a whole
 * number of ten-thousandths, and the working in thirty fractional bits.
 *
 * Measured against the C library over every argument that gives an answer
 * the machine can hold, the answer is within two ten-thousandths of the
 * double's below a thousand, and within three parts in ten million above,
 * where a ten-thousandth is smaller than that anyway.
 */

#include <stdint.h>
#include "delta.h"

#define Q 30
#define QONE ((int64_t)1 << Q)

/* ln 2 in thirty fractional bits. */
#define LN2_Q 744261118LL

/* e to the power r, for r no further from nought than half ln 2, in thirty
   fractional bits each way: the series until its terms are nothing. */
static int64_t exp_q(int64_t r)
{
    int64_t term = QONE;
    int64_t sum = QONE;
    int k;

    for (k = 1; k < 16 && term != 0; k++) {
        term = (term * r) >> Q;
        term /= k;
        sum += term;
    }
    return sum;
}

/* The natural logarithm of m, from one up to two, in thirty fractional bits:
   twice the inverse hyperbolic tangent of (m - 1) / (m + 1), whose series
   needs few terms there. */
static int64_t log_q(int64_t m)
{
    int64_t t = ((m - QONE) << Q) / (m + QONE);
    int64_t t2 = (t * t) >> Q;
    int64_t term = t;
    int64_t sum = 0;
    int k;

    for (k = 1; k < 40 && term != 0; k += 2) {
        sum += term / k;
        term = (term * t2) >> Q;
    }
    return 2 * sum;
}

/* The logarithm of a positive fixed value, in thirty fractional bits:
   brought into one up to two by halving or doubling, each step one ln 2. */
static int64_t ln_fixed(int32_t x)
{
    int64_t m = ((int64_t)x << Q) / DELTA_FIXED_ONE;
    int k = 0;

    while (m >= 2 * QONE) {
        m >>= 1;
        k++;
    }
    while (m < QONE) {
        m <<= 1;
        k--;
    }
    return (int64_t)k * LN2_Q + log_q(m);
}

/* e to the power x, x in thirty fractional bits, as a fixed value: x is
   split into a whole number of ln 2s, which is a shift, and what is left
   over. Two to the nineteenth is past the largest fixed value there is, and
   past where the shift would run out of sixty-four bits. */
static int32_t exp_to_fixed(int64_t xq)
{
    int64_t k = (xq >= 0 ? xq + LN2_Q / 2 : xq - LN2_Q / 2) / LN2_Q;
    int64_t v = exp_q(xq - k * LN2_Q) * DELTA_FIXED_ONE;

    if (k >= 0) {
        if (k > 18)
            return INT32_MAX;
        v <<= k;
    } else {
        if (-k > 62)
            return 0;
        v >>= -k;
    }
    v = (v + (QONE >> 1)) >> Q;
    return v > INT32_MAX ? INT32_MAX : (int32_t)v;
}

int32_t fixed_exp(delta_state *d, int32_t x)
{
    (void)d;
    return exp_to_fixed(((int64_t)x << Q) / DELTA_FIXED_ONE);
}

int32_t fixed_exp2(delta_state *d, int32_t x)
{
    int64_t xq = ((int64_t)x << Q) / DELTA_FIXED_ONE;

    (void)d;
    /* Two to the x is e to the x ln 2, worked in two halves so that the
       product does not run past sixty-four bits. */
    return exp_to_fixed(((xq >> 4) * (LN2_Q >> 4)) >> 22);
}

/* Nothing to give for nought or less, where the C library gives minus
   infinity or not a number: the most negative whole number stands for
   both. */
int32_t fixed_log(delta_state *d, int32_t x)
{
    int64_t v;

    (void)d;
    if (x <= 0)
        return INT32_MIN;
    v = ln_fixed(x) * DELTA_FIXED_ONE;
    v = v >= 0 ? (v + (QONE >> 1)) >> Q : -((-v + (QONE >> 1)) >> Q);
    return (int32_t)v;
}

/* x to the power y as e to the y ln x. A base of nought gives nought, or the
   largest there is for a power below one, and a negative base nought, which
   no rule hands over. */
int32_t fixed_pow(delta_state *d, int32_t x, int32_t y)
{
    int64_t yq;

    (void)d;
    if (x == 0)
        return y > 0 ? 0 : INT32_MAX;
    if (x < 0)
        return 0;
    yq = ((int64_t)y << Q) / DELTA_FIXED_ONE;
    return exp_to_fixed(((ln_fixed(x) >> 4) * (yq >> 4)) >> 22);
}
