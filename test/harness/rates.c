/* The rates IBM never shipped, and the two ways of reaching them.
 *
 * test/harness/rate.c beside this one is about a different question -- whether an
 * instance survives having its rate changed at all -- and it asks it only of
 * the numbers IBM's engine took. This one is about the rates above eleven
 * thousand, and it has three halves.
 *
 * The first is the claim the resonator tables rest on: that IBM's four are
 * the two formulae they look like, so tables for another rate can be built
 * rather than found. Checked by building the tables for IBM's own two rates
 * and holding them against IBM's own arrays entry by entry. The engine never
 * uses what this builds at those two rates -- KlattSetConstParms reaches for
 * the static arrays by name -- which is exactly why the check has to be made
 * explicitly here.
 *
 * The second is the one that matters most, and it is stronger than anything
 * about spectra: every rate reached by holding must be the engine's own
 * samples, each repeated a whole number of times, and nothing else. That is
 * a byte-for-byte property, so it says outright that twenty-two thousand is
 * Eloquence and not an approximation of it. It is checked across instances
 * rather than within one, because the engine's second utterance is not its
 * first -- the machine's state has moved on -- while a fresh instance says
 * the same thing every time.
 *
 * The third is that every rate speaks, that it speaks for the same length of
 * time, that nothing comes out at a level that is not speech, and that the
 * two rates IBM did ship still answer what they always answered.
 *
 * EVV_RATES_REPORT=1 prints what each rate said.
 *
 * usage: rates
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "evv_abi.h"
#include "klatt_rates.h"
#include "eci_pcm.h"

enum { FRAME = 4096 };

enum { PARAM_RATE = 5 };

/* The highest rate a caller may ask for. src/eci/api/eci_env.c owns this; it is
   named again here so the refusals below stay in step with it. */
enum { EV_RATE_MAX_HZ = 48000 };

/* Room for the longest run and for the one the held ones are held against.
   The test sentence is 76,582 samples at eleven thousand and twenty five,
   and forty-eight thousand is the most any rate here asks for. */
enum { ROOM = 400000 };
enum { BASE_ROOM = 100000 };

typedef struct OldInst OldInst;

enum ECIMessage { eciWaveformBuffer, eciPhonemeBuffer, eciIndexReply };
enum ECICallbackReturn { eciDataNotProcessed, eciDataProcessed, eciDataAbort };

OldInst *STDCALL eo_new(void);
OldInst *STDCALL eo_newEx(int32_t language);
int      STDCALL es_delete(OldInst *h);
int      STDCALL et_addText(OldInst *h, const char *text);
int      STDCALL et_synthesize(OldInst *h);
int      STDCALL ev_setOutputBuffer(OldInst *h, int32_t n, void *buf);
int      STDCALL ev_setParam(OldInst *h, int32_t which, int32_t value);
int      STDCALL eo_getParam(OldInst *h, int32_t which);
void     STDCALL eo_registerCallback(OldInst *h, void *cb, void *data);
void     STDCALL eo_synchronizeSynth(OldInst *h);
int      STDCALL eo_speaking(OldInst *h);
int      STDCALL eo_getAvailableLanguages(uint32_t *out, int *count);
int32_t  STDCALL es_setDefaultParam(int32_t which, int32_t value);

void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

/* IBM's own four, as tools/engine/klatt-tables.py sliced them out of clsyn.obj. */
extern const int16_t klatt_EX8[3992];
extern const int16_t klatt_CO8[4991];
extern const int16_t klatt_EX11[3992];
extern const int16_t klatt_CO11[4992];

static short frame[FRAME];
static int   report;

/* Which way this run is going about it, because the claims that can be made
   differ and each path would otherwise rot. The same binary is run three
   times -- see the rates target in the Makefile.
 *
 * The default curves through four samples, so what comes out is not what
 * went in and the strongest thing that can be said is that a whole ratio
 * lands exactly on the engine's own samples where the two grids meet.
 * EVV_UPSAMPLE=hold interpolates nothing, so every sample is the engine's
 * and that is checked to the byte. EVV_UPSAMPLE=none synthesises at the
 * rate instead, which is a different half of the engine again. No one run
 * enters another's code. */
static int   method = CVT_SINC;
static int   synthesising;
static const char *method_name = "sinc";

/* What one run said, and how it behaved while saying it. */
static short kept[ROOM];
static long  said;
static long  peak_seen;
static long  step_seen;
static int   have_last;
static int   last_sample;
static int   overflowed;

static void watch(long count)
{
    long i;

    for (i = 0; i < count && i < FRAME; i++) {
        int  x = frame[i];
        long v = x < 0 ? -(long)x : x;

        if (v > peak_seen)
            peak_seen = v;
        if (have_last) {
            long d = (long)x - (long)last_sample;

            if (d < 0)
                d = -d;
            if (d > step_seen)
                step_seen = d;
        }
        last_sample = x;
        have_last = 1;

        if (said < ROOM)
            kept[said] = (short)x;
        else
            overflowed = 1;
        said++;
    }
}

static enum ECICallbackReturn STDCALL on_message(OldInst *h,
                                                 enum ECIMessage msg,
                                                 long param, void *data)
{
    (void)h;
    (void)data;
    if (msg == eciWaveformBuffer)
        watch(param);
    return eciDataProcessed;
}

static void nap(long ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec t;

    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
#endif
}

/* ---- the tables ------------------------------------------------------ */

/* Building IBM's two rates and holding the result against IBM's own arrays.
 *
 * Everything matches but two entries, and those two are pinned here rather
 * than tolerated: at eight thousand the cosine table's entries for 3,992 and
 * 4,008 hertz are the saturated value where the formula comes to 32,767.35
 * and rounds one short of it. They sit either side of four thousand, which
 * is where the cosine is exactly minus one, so whatever IBM generated these
 * with rounded the last fraction outwards there. Nothing can hear it: at
 * eight thousand the engine uses IBM's array, and this rate is the only one
 * whose cosine table runs past its own Nyquist at all.
 *
 * A third mismatch, or one anywhere else, means the formula has stopped
 * being IBM's and every built table is suspect.
 */
static int checkOne(const char *name, int32_t rate, int kind,
                    const int16_t *ibm, int allowed)
{
    static int16_t ex[KLATT_EX_COUNT];
    static int16_t co[KLATT_CO_COUNT];
    const int16_t *ours;
    int count, i, off = 0;

    if (!klatt_buildRateTables(rate, ex, co)) {
        printf("rates: would not build tables for %d\n", (int)rate);
        return 0;
    }

    ours = kind ? co : ex;
    count = kind ? KLATT_CO_COUNT : KLATT_EX_COUNT;

    for (i = 0; i < count; i++) {
        if (ours[i] == ibm[i])
            continue;
        off++;
        if (report || off > allowed)
            printf("rates: %s at %d hertz: IBM %d, built %d\n", name,
                   i + (kind ? KLATT_CO_FIRST : KLATT_EX_FIRST),
                   (int)ibm[i], (int)ours[i]);
    }

    if (off != allowed) {
        printf("rates: %s differs from IBM's in %d places, not %d\n",
               name, off, allowed);
        return 0;
    }
    return 1;
}

static int checkTables(void)
{
    if (!checkOne("EX8", 8000, 0, klatt_EX8, 0))
        return 0;
    if (!checkOne("EX11", 11025, 0, klatt_EX11, 0))
        return 0;
    if (!checkOne("CO8", 8000, 1, klatt_CO8, 2))
        return 0;
    if (!checkOne("CO11", 11025, 1, klatt_CO11, 0))
        return 0;

    /* And that it refuses what it cannot do, rather than filling a buffer
       with something meaningless. */
    {
        static int16_t ex[KLATT_EX_COUNT];
        static int16_t co[KLATT_CO_COUNT];

        if (klatt_buildRateTables(KLATT_RATE_MIN - 1, ex, co)
            || klatt_buildRateTables(KLATT_RATE_MAX + 1, ex, co)) {
            printf("rates: it built tables for a rate out of range\n");
            return 0;
        }
    }
    return 1;
}

/* ---- the resampler on its own ---------------------------------------- */

/* Driving the arithmetic directly, with no engine behind it.
 *
 * Three properties, and the third is the one that matters most because
 * nothing measured on the audio would catch it. A run split into blocks has
 * to come out identical to the same run handed over whole -- the engine
 * hands its samples over a few dozen at a time, so every utterance is
 * hundreds of separate calls, and an interpolating way that did not carry
 * its tail across would put a seam at every one of them. Seams at forty
 * hertz would be a buzz under the speech, and the spectra would barely move.
 *
 * The other two say the arithmetic is a rate change and not an effect: a
 * held level has to come out at that level, and a straight line has to come
 * out straight wherever the way is one that can follow a line at all. The
 * second of those is what pins the shape of the curve rather than merely
 * where it starts -- moving one of the four points a curve is drawn through
 * leaves it passing through the same places on the grid, so nothing about
 * the grid would notice, and following a slope is what does.
 */
static int32_t unit_in[4096];
static int32_t unit_whole[16384];
static int32_t unit_piece[16384];

static const struct { int32_t method; const char *name; } METHODS[] = {
    { CVT_HOLD,   "hold" },
    { CVT_ZEROS,  "zeros" },
    { CVT_LINEAR, "linear" },
    { CVT_CUBIC,  "cubic" },
    { CVT_SINC,   "sinc" },
};

#define METHOD_COUNT ((int)(sizeof METHODS / sizeof METHODS[0]))

static const struct { int32_t from; int32_t to; } RATIOS[] = {
    { 11025, 22050 },   /* a whole one */
    { 11025, 44100 },   /* and a wider whole one */
    { 11025, 16000 },   /* and the uneven ones */
    { 11025, 48000 },
};

#define RATIO_COUNT ((int)(sizeof RATIOS / sizeof RATIOS[0]))

static int unitChecks(void)
{
    int m, q;

    for (m = 0; m < METHOD_COUNT; m++) {
        for (q = 0; q < RATIO_COUNT; q++) {
            int32_t from = RATIOS[q].from, to = RATIOS[q].to;
            PcmResampler r;
            uint32_t n = 1000, whole, made, at;
            uint32_t i;
            int32_t delay;

            /* A held level, then a straight line, so both properties are in
               the one run and the join between them is exercised too.

               The line is steep on purpose. A curve drawn through the wrong
               four points still passes through the right places where the
               grids meet, and still holds a level, so the only thing that
               catches it is how far it strays between them -- and that is in
               proportion to the slope. At three a sample the worst stray is
               a fifth of a count and invisible; at sixty it is nearly four,
               which is well past what rounding explains. Sixty a sample over
               half the run stays inside sixteen bits, which the clamp would
               otherwise hide. */
            for (i = 0; i < n; i++)
                unit_in[i] = i < n / 2 ? 1000
                                       : 1000 + (int32_t)(i - n / 2) * 60;

            if (!pcm_resample_start(&r, from, to, METHODS[m].method)) {
                printf("rates: no room for the %s filter\n",
                       METHODS[m].name);
                return 0;
            }
            delay = pcm_resample_delay(&r);
            whole = pcm_resample_count(&r, n);
            if (whole > sizeof unit_whole / sizeof unit_whole[0]) {
                printf("rates: no room to drive the resampler\n");
                return 0;
            }
            made = pcm_resample(&r, unit_in, n, unit_whole);
            if (made != whole) {
                printf("rates: %s at %d to %d made %u samples where it said "
                       "it would make %u\n", METHODS[m].name, (int)from,
                       (int)to, made, whole);
                return 0;
            }

            /* A level in has to be that level out, once the delay has been
               walked past. Zeros is not asked: putting silence between the
               samples is the whole of what it does. */
            if (METHODS[m].method != CVT_ZEROS) {
                /* Twice the delay is how far back the window reaches, not
                   once: a way that runs `delay' behind reaches that far
                   either side of where it sits. Plus a couple, so nothing
                   sits exactly on the boundary. */
                uint32_t settled =
                    (uint32_t)((int64_t)(2 * delay + 2) * to / from);

                for (i = settled; i < (uint32_t)((int64_t)(n / 2) * to / from);
                     i++)
                    if (unit_whole[i] != 1000) {
                        printf("rates: %s at %d to %d turned a held level of "
                               "1000 into %d at sample %u\n",
                               METHODS[m].name, (int)from, (int)to,
                               (int)unit_whole[i], i);
                        return 0;
                    }
            }

            /* And a slope in has to be that slope out, for the two ways
               that can follow one. Where the walk is at p input samples, the
               line is at 1000 plus three for every sample past the halfway
               point, less however far behind this way runs. */
            if (METHODS[m].method == CVT_LINEAR
                || METHODS[m].method == CVT_CUBIC
                || METHODS[m].method == CVT_SINC) {
                uint32_t first = (uint32_t)(((int64_t)(n / 2) + 2 * delay + 2)
                                            * to / from);

                for (i = first; i < whole; i++) {
                    double p = (double)((int64_t)i * from) / (double)to;
                    double owed = 1000.0
                                + (p - (double)delay - (double)(n / 2)) * 60.0;
                    double off = (double)unit_whole[i] - owed;

                    if (off < 0)
                        off = -off;
                    /* A straight line comes out straight, and the two ways
                       that draw a curve through a handful of points do it to
                       the count. The sinc is held a shade looser: its window
                       is cut off at the ends, so the weights either side of
                       the position are not quite balanced and the line it
                       draws is out by a fraction of a count rather than by
                       nothing at all. */
                    if (off > (METHODS[m].method == CVT_SINC ? 4.0 : 1.0)) {
                        printf("rates: %s at %d to %d does not follow a "
                               "slope: sample %u is %d where the line is at "
                               "%.2f\n", METHODS[m].name, (int)from, (int)to,
                               i, (int)unit_whole[i], owed);
                        return 0;
                    }
                }
            }

            pcm_resample_end(&r);

            /* And the same run in pieces, which has to come out the same.
               Uneven pieces on purpose: a way that only joined up on a round
               number of samples would pass on even ones. */
            if (!pcm_resample_start(&r, from, to, METHODS[m].method)) {
                printf("rates: no room for the %s filter\n",
                       METHODS[m].name);
                return 0;
            }
            at = 0;
            made = 0;
            {
                uint32_t sizes[6] = { 37, 200, 1, 111, 400, 251 };
                int      k = 0;

                while (at < n) {
                    uint32_t take = sizes[k % 6];

                    if (take > n - at)
                        take = n - at;
                    made += pcm_resample(&r, unit_in + at, take,
                                         unit_piece + made);
                    at += take;
                    k++;
                }
            }
            if (made != whole) {
                printf("rates: %s at %d to %d made %u samples in pieces "
                       "where it made %u whole\n", METHODS[m].name,
                       (int)from, (int)to, made, whole);
                return 0;
            }
            for (i = 0; i < made; i++)
                if (unit_piece[i] != unit_whole[i]) {
                    printf("rates: %s at %d to %d has a seam: sample %u is "
                           "%d in pieces and %d whole\n", METHODS[m].name,
                           (int)from, (int)to, i, (int)unit_piece[i],
                           (int)unit_whole[i]);
                    pcm_resample_end(&r);
                    return 0;
                }
            pcm_resample_end(&r);
        }
    }

    if (report)
        printf("rates: the resampler holds a level, follows a slope, and "
               "over %d ways and %d ratios comes out the same in pieces as "
               "whole\n", METHOD_COUNT, RATIO_COUNT);
    return 1;
}

/* ---- speaking -------------------------------------------------------- */

/* One sentence, on an instance of its own.
 *
 * A fresh instance every time, because the engine's second utterance is not
 * its first and the whole of the check below is one run against another. A
 * new instance says the same thing every run, which is what test/harness/instances.c
 * is about; the same instance asked twice does not.
 */
static const char TEXT[] =
    "The quick brown fox jumps over the lazy dog. Testing 1 2 3, ABC XYZ.";

static long run_at(int32_t setting, int32_t *rateBack)
{
    OldInst *h;
    uint32_t langs[32];
    int n = 32;
    int i;

    said = 0;
    peak_seen = 0;
    step_seen = 0;
    have_last = 0;
    overflowed = 0;

    if (eo_getAvailableLanguages(langs, &n) || n < 1)
        return -1;

    h = eo_new();
    if (h == 0)
        h = eo_newEx(langs[0]);
    if (h == 0)
        return -1;

    eo_registerCallback(h, (void *)on_message, 0);
    if (!ev_setOutputBuffer(h, FRAME, frame)) {
        es_delete(h);
        return -1;
    }
    if (ev_setParam(h, PARAM_RATE, setting) < 0) {
        es_delete(h);
        return -2;
    }
    if (rateBack)
        *rateBack = eo_getParam(h, PARAM_RATE);

    if (!et_addText(h, TEXT) || !et_synthesize(h)) {
        es_delete(h);
        return -1;
    }
    for (i = 0; i < 6000 && eo_speaking(h); i++)
        nap(10);
    eo_synchronizeSynth(h);
    es_delete(h);

    if (overflowed) {
        printf("rates: more samples than there is room for\n");
        return -1;
    }
    return said;
}

/* Whether what came out is speech or a runaway.
 *
 * The peak alone cannot tell the two apart: real speech is normalised and
 * goes near full scale, and so does a runaway once it has been wrapped into
 * sixteen bits. What tells them apart is how fast the signal moves. A
 * waveform sampled well above its own bandwidth cannot swing its whole
 * amplitude between one sample and the next; wrapping does exactly that.
 *
 * Eight thousand is exempt, and that is IBM's own doing rather than a
 * loophole: there the speech really does move that fast relative to the
 * rate. Nothing this work touches can change eight thousand -- it uses IBM's
 * own tables and its sample count is pinned below.
 *
 * A held rate is not asked either, and for a better reason than exemption:
 * repeating samples leaves every jump between them exactly where it was, so
 * a held stream slews like its base and not like its own rate. The check
 * that covers a held rate is that it is the base's samples to the byte.
 */
static int verdict(int32_t hz)
{
    if (hz <= 8000)
        return 1;
    if (step_seen > peak_seen) {
        printf("rates: at %d hertz the signal steps %ld between samples with "
               "a peak of only %ld, which is a runaway wrapped into sixteen "
               "bits and not speech\n", (int)hz, step_seen, peak_seen);
        return 0;
    }
    return 1;
}

/* What IBM's two rates have always said, so that widening the interface
   cannot quietly move them. */
#define SAID_AT_8000   55696
#define SAID_AT_11025  76582

/* How far from proportional a rate may come out. Hardly at all: KlattSynth
   works a frame's length out as its duration in milliseconds times the rate
   over a thousand, truncated, so a rate that is not a whole number of
   samples to the millisecond rounds each frame down by a fraction. One per
   cent is far tighter than the distance between any two rates here. */
#define TOLERANCE_PCT  1

/* Each setting, the rate it comes to, and the rate it is taken from -- nought
   where the engine synthesises it outright. */
struct want {
    int32_t set;
    int32_t hz;
    int32_t from;
};

static const struct want WANTED[] = {
    { 0, 8000,  0 },
    { 1, 11025, 0 },
    { 3, 16000, 11025 },
    { 2, 22050, 11025 },
    { 4, 32000, 11025 },
    { 5, 44100, 11025 },
    { 6, 48000, 11025 },
    /* Not a number anyone gave a meaning to, asked for as a rate in hertz.
       Taken over like the rest with the holding on, and synthesised with
       built tables with it off. */
    { 24000, 24000, 11025 },
};

#define WANTED_COUNT ((int)(sizeof WANTED / sizeof WANTED[0]))

/* Numbers that name no rate the engine will make.
 *
 * The three in the middle are the gap between the engine's own two, and they
 * are refused rather than synthesised: the language compensates for a cosine
 * table running past Nyquist only at eight thousand, so a rate just above it
 * is made without the compensation it needs and the output wraps. Measured
 * at 8,500 and 9,000, on the edge at 9,500 and 10,000, clean from about
 * 10,500. All of the gap is refused rather than the part that misbehaves,
 * because the boundary is a matter of degree and a rate nobody can name a
 * reason for is not worth the doubt. */
static const int32_t REFUSED[] = {
    7, 100, 7999, 8500, 9000, 10500, EV_RATE_MAX_HZ + 1, -1,
};

#define REFUSED_COUNT ((int)(sizeof REFUSED / sizeof REFUSED[0]))

/* The engine's own two runs, kept so the held ones can be held against
   them. */
static short base8[BASE_ROOM];
static long  base8_n;
static short base11[BASE_ROOM];
static long  base11_n;

int main(void)
{
    int r;

    {
        const char *say = getenv("EVV_UPSAMPLE");

        if (say != 0) {
            method_name = say;
            if (strcmp(say, "none") == 0)
                synthesising = 1;
            else if (strcmp(say, "hold") == 0)
                method = CVT_HOLD;
            else if (strcmp(say, "zeros") == 0)
                method = CVT_ZEROS;
            else if (strcmp(say, "linear") == 0)
                method = CVT_LINEAR;
            else if (strcmp(say, "cubic") == 0)
                method = CVT_CUBIC;
        }
    }
    report = getenv("EVV_RATES_REPORT") != 0;

    if (!checkTables())
        return 1;
    if (!unitChecks())
        return 1;

    evv_port_start();
    evvRunStaticInitialisers();

    /* Whatever is not a rate has to be refused, and refused before anything
       else so that a later failure cannot be blamed on it. */
    {
        OldInst *h;
        uint32_t langs[32];
        int n = 32;

        if (eo_getAvailableLanguages(langs, &n) || n < 1) {
            printf("rates: no language\n");
            return 1;
        }
        h = eo_new();
        if (h == 0)
            h = eo_newEx(langs[0]);
        if (h == 0) {
            printf("rates: no instance\n");
            return 1;
        }
        eo_registerCallback(h, (void *)on_message, 0);
        if (!ev_setOutputBuffer(h, FRAME, frame)) {
            printf("rates: it would not take the buffer\n");
            return 1;
        }
        for (r = 0; r < REFUSED_COUNT; r++) {
            if (ev_setParam(h, PARAM_RATE, REFUSED[r]) >= 0) {
                printf("rates: it took %d, which is not a rate\n",
                       (int)REFUSED[r]);
                return 1;
            }
        }
        es_delete(h);
    }

    /* The default environment goes through a different gate: it asks the
       device whether it will give us the rate, where an instance does not.
       So a numbered rate the device has no bit for would be refused there
       and nowhere else, which nothing else here would notice. */
    for (r = 0; r < WANTED_COUNT; r++) {
        if (WANTED[r].set >= KLATT_RATE_MIN)
            continue;   /* a rate in hertz, not one of the numbered ones */
        if (es_setDefaultParam(PARAM_RATE, WANTED[r].set) < 0) {
            printf("rates: the default environment refused rate %d\n",
                   (int)WANTED[r].set);
            return 1;
        }
    }
    if (es_setDefaultParam(PARAM_RATE, 7) >= 0) {
        printf("rates: the default environment took 7, which is not a rate\n");
        return 1;
    }
    es_setDefaultParam(PARAM_RATE, 1);

    for (r = 0; r < WANTED_COUNT; r++) {
        int32_t set = WANTED[r].set;
        int32_t hz = WANTED[r].hz;
        int32_t from = synthesising ? 0 : WANTED[r].from;
        long    got, owed, slack;
        int32_t back = -1;

        /* Synthesised outright, the engine has a ceiling the held rates do
           not, so with the holding off the highest rate is not merely worse
           but impossible, and has to be refused rather than attempted. */
        if (synthesising && hz > KLATT_RATE_MAX) {
            if (run_at(set, &back) != -2) {
                printf("rates: with no holding it took %d hertz, which it "
                       "cannot synthesise\n", (int)hz);
                return 1;
            }
            continue;
        }

        got = run_at(set, &back);
        if (got == -2) {
            printf("rates: it refused rate setting %d\n", (int)set);
            return 1;
        }
        if (got < 0) {
            printf("rates: it would not speak at %d hertz\n", (int)hz);
            return 1;
        }
        if (got == 0) {
            printf("rates: silent at %d hertz\n", (int)hz);
            return 1;
        }
        if (back != set) {
            printf("rates: asked for %d and it says %d\n", (int)set,
                   (int)back);
            return 1;
        }
        if (report)
            printf("rates: %6d hertz  %8ld samples  %8.1f ms  peak %6ld  "
                   "largest step %6ld%s\n", (int)hz, got,
                   1000.0 * (double)got / (double)hz, peak_seen, step_seen,
                   from != 0 ? "  taken over" : "");

        /* Only where the engine synthesised the rate outright. A held rate
           steps exactly as its base does -- repeating a sample leaves the
           jumps between them where they were -- so the slew rule, which is a
           property of a bandlimited signal, says nothing about one that is
           deliberately not bandlimited. What covers a held rate is the
           stronger check further down: if it is the base's samples exactly
           and the base passed, there is nothing left to ask. */
        if (from == 0 && !verdict(hz))
            return 1;

        if (hz == 8000 && got != SAID_AT_8000) {
            printf("rates: %ld samples at 8 kHz where it always said %d\n",
                   got, SAID_AT_8000);
            return 1;
        }
        if (hz == 11025) {
            if (got != SAID_AT_11025) {
                printf("rates: %ld samples at 11 kHz where it always said "
                       "%d\n", got, SAID_AT_11025);
                return 1;
            }
            memcpy(base11, kept, (size_t)got * sizeof(short));
            base11_n = got;
        }

        /* The strong one. Every sample of a rate taken from another is a
           sample the engine produced, and which one is settled outright:
           the sample nearest that moment in time. So this is byte for byte
           rather than a matter of how close two spectra are, and it covers
           both the whole ratios, where it comes to each sample repeated the
           same number of times, and the uneven ones. Breaking the copy for a
           single sample fails it and names the sample. */
        if (from != 0) {
            long baseN = base11_n;
            long owedN;
            long k, wrong = 0;

            if (baseN <= 0) {
                printf("rates: nothing to hold %d hertz against\n", (int)hz);
                return 1;
            }
            /* However many output samples have an input sample under them. */
            owedN = (long)(((long long)baseN * hz + from - 1) / from);
            if (got != owedN) {
                printf("rates: %ld samples at %d hertz where taking %ld of "
                       "%d owes %ld\n", got, (int)hz, baseN, (int)from,
                       owedN);
                return 1;
            }
            if (method == CVT_HOLD) {
                /* Nothing is interpolated, so every sample is the engine's
                   and which one is settled: the one whose turn it still is.
                   Byte for byte, which is what says this rate is Eloquence
                   rather than an approximation of it. */
                for (k = 0; k < got; k++) {
                    long at = (long)(((long long)k * from) / hz);

                    if (at >= baseN)
                        at = baseN - 1;
                    if (kept[k] != base11[at]) {
                        wrong = k + 1;
                        break;
                    }
                }
                if (wrong) {
                    long at = (long)(((long long)(wrong - 1) * from) / hz);

                    printf("rates: %d hertz is not %d held: sample %ld is %d "
                           "where the engine said %d\n", (int)hz, (int)from,
                           wrong - 1, (int)kept[wrong - 1], (int)base11[at]);
                    return 1;
                }
            } else if (method != CVT_SINC && (hz % from) == 0) {
                /* A curve does not hand back what it was given, but where
                   the two grids meet it passes through the point, so every
                   output sample that lands on an input one has to be that
                   input sample exactly -- offset by however far behind the
                   way in force runs. That is still byte for byte, on one
                   sample in every few.

                   The sinc is not asked, and that is what it is for rather
                   than a gap in the checking. It cuts the band off below the
                   input's own Nyquist, so it changes the signal on the grid
                   as well as between: a filter that passed every input
                   sample through untouched would not be filtering. What
                   stands for it is the arithmetic driven directly above. */
                long times = hz / from;
                long delay = method == CVT_LINEAR ? 1
                           : method == CVT_CUBIC ? 2 : 0;

                for (k = delay; k < baseN && wrong == 0; k++) {
                    long j = k * times;

                    if (j >= got)
                        break;
                    if (kept[j] != base11[k - delay])
                        wrong = j + 1;
                }
                if (wrong) {
                    printf("rates: %d hertz does not meet %d on the grid: "
                           "sample %ld is %d where the engine said %d\n",
                           (int)hz, (int)from, wrong - 1,
                           (int)kept[wrong - 1],
                           (int)base11[(wrong - 1) / (hz / from) - delay]);
                    return 1;
                }
            }

            if (report)
                printf("rates: %6d hertz  %s\n", (int)hz,
                       method == CVT_HOLD
                       ? "every sample is the engine's own"
                       : method == CVT_SINC
                       ? "filtered, so the resampler's own checks stand"
                       : (hz % from) == 0
                       ? "every sample where the grids meet is the engine's own"
                       : "an uneven ratio, so the resampler's own checks stand");
            continue;
        }

        /* And for a rate the engine synthesises outright there is nothing to
           hold it against, so the claim is the weaker one: the same sentence
           for the same length of time. */
        if (base11_n == 0)
            continue;
        owed = (long)((double)base11_n * (double)hz / 11025.0);
        slack = owed / (100 / TOLERANCE_PCT);
        if (got < owed - slack || got > owed + slack) {
            printf("rates: %ld samples at %d hertz, owed about %ld\n",
                   got, (int)hz, owed);
            return 1;
        }
    }

    evv_port_finish();
    if (synthesising)
        printf("rates: synthesised outright, every rate the engine can make "
               "does so from tables built out of IBM's formulae, and the one "
               "it cannot is refused\n");
    else
        printf("rates: the tables are IBM's formulae, %s, and all %d rates "
               "speak to length\n",
               method == CVT_HOLD
               ? "every rate above eleven thousand is the engine's own "
                 "samples to the byte"
               : method == CVT_SINC
               ? "the resampler holds a level, follows a slope and has no "
                 "seams"
               : "every whole ratio meets the engine's own samples on the "
                 "grid", WANTED_COUNT);
    return 0;
}
