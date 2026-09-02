#ifndef ECI_PCM_H
#define ECI_PCM_H

#include <stdint.h>

/* Raising the sample rate, and what goes between the samples.
 *
 * The engine runs at eleven thousand and twenty five and every rate above it
 * is made from that, so this is what decides what a caller actually hears
 * above the engine's own ceiling. src/eci_pcm.c says what each way sounds
 * like and why the default is what it is.
 *
 * It is declared here rather than kept inside that file so the arithmetic
 * can be driven directly, without an engine behind it. test/rates.c does
 * that: a run split into several blocks has to come out identical to the
 * same run handed over whole, which is the one property no measurement of
 * the audio would catch and the one a mistake in the history would break.
 */

#define CVT_HOLD    0
#define CVT_ZEROS   1
#define CVT_LINEAR  2
#define CVT_CUBIC   3
#define CVT_SINC    4

/* The windowed sinc: how many samples each side of the position it reaches,
   how finely the curve it reads from is drawn, where it stops passing the
   band through, and how hard the window is.
 *
 * Twenty-four each side is enough for the images to be gone rather than
 * merely quieter, which is the difference between this and the curve. The
 * cutoff is a fraction of the input's own Nyquist and matches what soxr uses
 * at its medium setting: taking the filter right up to Nyquist would leave
 * no room for it to fall, so the last ninth of the band is where it does.
 * The window is a Kaiser and eight is the shape that puts the stopband about
 * eighty decibels down for this many taps. */
#define SINC_HALF    24
#define SINC_PHASES  128
#define SINC_CUTOFF  0.91
#define SINC_BETA    8.0
#define SINC_TABLE   (2 * SINC_HALF * SINC_PHASES + 1)

/* How far back the interpolating ways look, and therefore how many samples
   of the run before have to be kept. The sinc is the deepest. */
#define CVT_HISTORY (2 * SINC_HALF)

/* How far behind the input each way runs, in input samples. Looking only
   backwards is what lets a run join the one before it with no seam and
   nothing held back at the end; the price is a constant delay, and this is
   it. Two tenths of a millisecond for the curve and two milliseconds for
   the sinc, at the engine's own rate. */
#define CVT_DELAY(m) ((m) == CVT_LINEAR ? 1 \
                    : (m) == CVT_CUBIC  ? 2 \
                    : (m) == CVT_SINC   ? SINC_HALF : 0)

typedef struct PcmResampler {
    int32_t from;
    int32_t to;
    int32_t method;
    /* Where the walk has got to, in units of one input sample over `to'. */
    int32_t at;
    /* The last few samples of the run before. */
    int32_t history[CVT_HISTORY];
    /* The windowed sinc, drawn once when the rate is settled and read with a
       straight line between its points. Kept here rather than shared so that
       two instances on two threads cannot race to draw it. */
    int     drawn;
    double  sinc[SINC_TABLE];
} PcmResampler;

void     pcm_resample_start(PcmResampler *r, int32_t from, int32_t to,
                            int32_t method);
uint32_t pcm_resample_count(const PcmResampler *r, uint32_t n);
uint32_t pcm_resample(PcmResampler *r, const int32_t *src, uint32_t n,
                      int32_t *out);

#endif
