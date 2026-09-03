#ifndef ECI_PCM_H
#define ECI_PCM_H

#include <stdint.h>

/* Raising the sample rate, and what goes between the samples.
 *
 * The engine runs at eleven thousand and twenty five and every rate above it
 * is made from that, so this is what decides what a caller actually hears
 * above the engine's own ceiling. src/eci/sound/eci_pcm.c says what each way sounds
 * like and why the default is what it is.
 *
 * It is declared here rather than kept inside that file so the arithmetic
 * can be driven directly, without an engine behind it. test/harness/rates.c does
 * that: a run split into several blocks has to come out identical to the
 * same run handed over whole, which is the one property no measurement of
 * the audio would catch and the one a mistake in the history would break.
 */

#define CVT_HOLD    0
#define CVT_ZEROS   1
#define CVT_LINEAR  2
#define CVT_CUBIC   3
#define CVT_SINC    4

/* The windowed sinc, and the two numbers worth arguing about.
 *
 * How far up the band it passes, as a fraction of the input's own Nyquist,
 * and how many samples it reaches over. The two trade against each other:
 * taking the filter closer to Nyquist leaves it less room to fall, so it
 * needs more samples to fall as steeply, and a Kaiser window of this shape
 * puts the stopband roughly 2.285 times the transition times the taps, in
 * radians, below the passband.
 *
 * The first choice here was 0.91 over 48 samples, copied from what soxr uses
 * at its medium setting, and it was wrong for this engine. At eleven
 * thousand and twenty five, 0.91 of Nyquist is 5,016 hertz, and Eloquence
 * has real speech above that -- measured, the band from 5,016 to 5,512
 * carries 27 decibels less than the band below it, which is quiet but is
 * not nothing. Cutting there threw away 12.4 decibels of it, and that was
 * heard: the voice came out duller than it should be.
 *
 * So 0.98 over 192 samples. Measured on the same sentence, that band now
 * comes through 1.1 decibels under where the engine put it, against 12.4
 * before, and the images are still 83 decibels down. Pushing further costs
 * more than it returns: 0.99 over 256 gains another half a decibel of speech
 * and gives up fifteen of stopband. Ninety-six samples of delay is under
 * nine milliseconds at the engine's rate.
 *
 * EVV_SINC_CUTOFF and EVV_SINC_TAPS move both, because where exactly to put
 * them is a matter for a listener and not for this file. */
#define SINC_CUTOFF   0.98
#define SINC_TAPS     192
#define SINC_PHASES   128
#define SINC_BETA     8.0

/* As far as either may be pushed. The taps decide how much is allocated for
   the filter and how far behind the input it runs, and two hundred and
   fifty-six of them is nine milliseconds at the engine's rate, which is as
   much delay as is worth having for this. */
#define SINC_TAPS_MAX 256

/* How far back the interpolating ways look, and therefore how many samples
   of the run before have to be kept. The sinc is the deepest. */
#define CVT_HISTORY   SINC_TAPS_MAX

/* How far behind the input a resampler runs, in input samples. Looking only
   backwards is what lets a run join the one before it with no seam and
   nothing held back at the end; the price is a constant delay, and this is
   it. Two tenths of a millisecond for the curve, and half the taps for the
   sinc, which is under six milliseconds at the engine's rate.

   It is asked of the resampler rather than worked out from the method,
   because the sinc's answer depends on how many taps it was given. */

typedef struct PcmResampler {
    int32_t from;
    int32_t to;
    int32_t method;
    /* Where the walk has got to, in units of one input sample over `to'. */
    int32_t at;
    /* The last few samples of the run before. */
    int32_t history[CVT_HISTORY];
    /* The windowed sinc, drawn once when the rate is settled and read with a
       straight line between its points. Owned here rather than shared so
       that two instances on two threads cannot race to draw it, and
       allocated rather than inline because how big it is depends on how many
       taps it was asked for. pcm_resample_end gives it back. */
    int32_t half;            /* taps each side of the position */
    double *sinc;            /* 2 * half * SINC_PHASES + 1 of them */
} PcmResampler;

/* Answers zero where the filter could not be made, which is out of room and
   nothing else. */
int      pcm_resample_start(PcmResampler *r, int32_t from, int32_t to,
                            int32_t method);
void     pcm_resample_end(PcmResampler *r);
uint32_t pcm_resample_count(const PcmResampler *r, uint32_t n);
uint32_t pcm_resample(PcmResampler *r, const int32_t *src, uint32_t n,
                      int32_t *out);
int32_t  pcm_resample_delay(const PcmResampler *r);

#endif
