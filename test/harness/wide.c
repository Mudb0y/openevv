/* The wideband voice, held to the two things that make it the same voice.
 *
 * Below the join it is the engine's own sound and nothing else. At 22,050,
 * the rate it is made at, the default stream is the 11,025 voice raised by
 * the sinc and the wideband stream is that same raising with the
 * companion's top added, sample for sample; so the two subtracted leave the
 * top alone, and what the difference carries below the join is what the
 * split let through and nothing more. At the rates taken from 22,050 the two
 * go through different converters and arrive at different delays, so the
 * delay is found first and the same comparison made after it.
 *
 * And the top lands on the same moments as what is under it, all the way
 * through. Every comparison above would pass with the top arriving late,
 * since it lies above the join whenever it arrives. The companion is handed
 * each frame just before its partner and its samples are taken a frame at a
 * time, so lateness cannot build up; what this catches is a top made from
 * the wrong frames, or late by a fixed amount. So the top's loudness, moment
 * by moment, is held against the loudness of the band just below the join,
 * which the same fricatives drive, at the start of a long text and at its
 * end. Fed frames ten milliseconds late, it reads seven to nine.
 *
 * Besides those, the setting itself: off on a fresh instance, on when asked,
 * refused for anything but nought and one, off again after a reset, and of
 * no effect at the engine's own two rates.
 *
 * usage: wide
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#include "evv_abi.h"

typedef struct OldInst OldInst;

enum ECIMessage { eciWaveformBuffer, eciPhonemeBuffer, eciIndexReply };
enum ECICallbackReturn { eciDataNotProcessed, eciDataProcessed, eciDataAbort };

OldInst *STDCALL eo_new(void);
int      STDCALL es_delete(OldInst *h);
int      STDCALL es_reset(OldInst *h);
int      STDCALL et_addText(OldInst *h, const char *text);
int      STDCALL et_synthesize(OldInst *h);
int      STDCALL ev_setOutputBuffer(OldInst *h, int32_t n, void *buf);
int      STDCALL ev_setParam(OldInst *h, int32_t which, int32_t value);
int      STDCALL eo_getParam(OldInst *h, int32_t which);
void     STDCALL eo_registerCallback(OldInst *h, void *cb, void *data);
void     STDCALL eo_synchronizeSynth(OldInst *h);
int      STDCALL eo_speaking(OldInst *h);

void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

enum { PARAM_RATE = 5, PARAM_WIDEBAND = 32 };
enum { FRAME = 4096, ROOM = 4000000 };

static short frame[FRAME];
static short *kept;
static long  said;
static int   bad;

static enum ECICallbackReturn STDCALL on_message(OldInst *h,
                                                 enum ECIMessage msg,
                                                 long param, void *data)
{
    long i;

    (void)h;
    (void)data;
    if (msg != eciWaveformBuffer)
        return eciDataProcessed;
    for (i = 0; i < param && i < FRAME; i++) {
        if (said < ROOM)
            kept[said] = frame[i];
        said++;
    }
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

/* One text on a fresh instance, at a rate given in hertz, into a buffer of
   its own. Answers the count, or minus one. */
static long speak(const char *text, int32_t hz, int wide, short **out)
{
    OldInst *h = eo_new();
    int i;

    said = 0;
    if (h == 0)
        return -1;
    eo_registerCallback(h, (void *)on_message, 0);
    if (!ev_setOutputBuffer(h, FRAME, frame)
        || ev_setParam(h, PARAM_RATE, hz) < 0
        || (wide && ev_setParam(h, PARAM_WIDEBAND, 1) < 0)
        || !et_addText(h, text) || !et_synthesize(h)) {
        es_delete(h);
        return -1;
    }
    for (i = 0; i < 12000 && eo_speaking(h); i++)
        nap(10);
    eo_synchronizeSynth(h);
    es_delete(h);
    if (said > ROOM)
        return -1;

    *out = malloc((size_t)said * sizeof(short));
    if (*out == 0)
        return -1;
    memcpy(*out, kept, (size_t)said * sizeof(short));
    return said;
}

/* ---- arithmetic ------------------------------------------------------ */

/* A windowed-sinc lowpass at cut hertz, applied in place of a copy. */
static double *lowpass(const double *x, long n, double cut, double hz)
{
    enum { HALF = 255 };
    double h[2 * HALF + 1], sum = 0.0;
    double *y = calloc((size_t)n, sizeof(double));
    long i, j;

    if (y == 0)
        return 0;
    for (j = -HALF; j <= HALF; j++) {
        double c = 2.0 * cut / hz, t = (double)j;
        double s = t == 0.0 ? c : sin(3.14159265358979 * c * t)
                                  / (3.14159265358979 * t);
        double w = 0.54 + 0.46 * cos(3.14159265358979 * t / (HALF + 1));

        h[j + HALF] = s * w;
        sum += s * w;
    }
    for (j = 0; j <= 2 * HALF; j++)
        h[j] /= sum;
    for (i = 0; i < n; i++) {
        double a = 0.0;

        for (j = -HALF; j <= HALF; j++) {
            long k = i - j;

            if (k >= 0 && k < n)
                a += h[j + HALF] * x[k];
        }
        y[i] = a;
    }
    return y;
}

static double *as_double(const short *s, long n)
{
    double *d = malloc((size_t)n * sizeof(double));
    long i;

    if (d != 0)
        for (i = 0; i < n; i++)
            d[i] = s[i];
    return d;
}

static double energy(const double *x, long from, long to)
{
    double e = 0.0;
    long i;

    for (i = from; i < to; i++)
        e += x[i] * x[i];
    return e;
}

/* The same signal half a sample later than it was sampled: y[i] is x at
   i + 0.5. Only asked of something already cut to well under its Nyquist,
   which is what lets a short filter do it. */
static double *half_on(const double *x, long n)
{
    enum { HALF = 64 };
    double h[2 * HALF];
    double *y = calloc((size_t)n, sizeof(double));
    long i, j;

    if (y == 0)
        return 0;
    for (j = -HALF + 1; j <= HALF; j++) {
        double t = (double)j - 0.5;
        double w = 0.54 + 0.46 * cos(3.14159265358979 * t / HALF);

        h[j + HALF - 1] = sin(3.14159265358979 * t) / (3.14159265358979 * t)
                          * w;
    }
    for (i = 0; i < n; i++) {
        double a = 0.0;

        for (j = -HALF + 1; j <= HALF; j++) {
            long k = i + j;

            if (k >= 0 && k < n)
                a += h[j + HALF - 1] * x[k];
        }
        y[i] = a;
    }
    return y;
}

/* Where b best lines up with a, b running behind by that many half
   samples. The delay between two converters need not be a whole number of
   output samples -- lowering 22,050 to 16,000 puts the wideband stream 96
   and a half behind -- and half a sample is a quarter turn at 4 kHz, which
   would read as most of the voice being different. */
static long best_lag(const double *a, long na, const double *b,
                     const double *bh, long nb, long most)
{
    long lag, best = 0, i;
    double top = -1e300;

    for (lag = 0; lag <= 2 * most; lag++) {
        const double *v = lag & 1 ? bh : b;
        long at = lag / 2;
        double c = 0.0;

        for (i = 0; i + at < nb && i < na; i++)
            c += a[i] * v[i + at];
        if (c > top) {
            top = c;
            best = lag;
        }
    }
    return best;
}

/* ---- the checks ------------------------------------------------------ */

static const char SHORT_TEXT[] =
    "She sells sixty sea shells, and the fish stew is chosen for its sharp, "
    "fresh zest. Peter took the black cat to the top of the big park.";

/* Below the join, the wideband stream against the default one at the same
   rate, after the one's delay behind the other is taken out. */
static void same_below(int32_t hz)
{
    short *d = 0, *w = 0;
    long nd = speak(SHORT_TEXT, hz, 0, &d);
    long nw = speak(SHORT_TEXT, hz, 1, &w);
    double *dd, *wd, *dl, *wl, *wh, *v, *diff;
    long lag, at, n, i;
    double ratio;

    if (nd <= 0 || nw <= 0) {
        printf("wide: %d hertz would not speak\n", (int)hz);
        bad = 1;
        goto out;
    }
    dd = as_double(d, nd);
    wd = as_double(w, nw);
    dl = lowpass(dd, nd, 4500.0, (double)hz);
    wl = lowpass(wd, nw, 4500.0, (double)hz);
    wh = half_on(wl, nw);
    lag = best_lag(dl, nd, wl, wh, nw, hz / 50);
    v = lag & 1 ? wh : wl;
    at = lag / 2;
    n = nd < nw - at ? nd : nw - at;
    diff = malloc((size_t)n * sizeof(double));
    for (i = 0; i < n; i++)
        diff[i] = v[i + at] - dl[i];
    ratio = 10.0 * log10(energy(diff, 0, n) / energy(dl, 0, n) + 1e-30);

    printf("wide: %5d hertz: wideband behind by %ld%s, below 4.5 kHz "
           "%.1f dB from the default\n", (int)hz, at, lag & 1 ? " and a half"
           : "", ratio);
    /* At the wide rate itself the two are the same stream but for the top,
       and the delay is nought. Elsewhere two converters stand between, each
       with its own ripple. */
    if (hz == 22050 && lag != 0) {
        printf("wide: at the wide rate the two should line up exactly\n");
        bad = 1;
    }
    if (ratio > (hz == 22050 ? -70.0 : -45.0)) {
        printf("wide: the lower band is not the default voice\n");
        bad = 1;
    }

    /* And there is a top at all: with nothing added every comparison above
       would pass, since the two streams would be the same one. What lies
       above 6 kHz, against what lies below 4.5. */
    {
        double *dh = lowpass(dd, nd, 6000.0, (double)hz);
        double *whi = lowpass(wd, nw, 6000.0, (double)hz);
        double up_d, up_w;

        for (i = 0; i < nd; i++)
            dh[i] = dd[i] - dh[i];
        for (i = 0; i < nw; i++)
            whi[i] = wd[i] - whi[i];
        up_d = 10.0 * log10(energy(dh, 0, nd) / energy(dl, 0, nd) + 1e-30);
        up_w = 10.0 * log10(energy(whi, 0, nw) / energy(wl, 0, nw) + 1e-30);
        printf("wide: %5d hertz: above 6 kHz the default is at %.1f dB and "
               "the wideband voice at %.1f\n", (int)hz, up_d, up_w);
        if (up_w - up_d < 20.0) {
            printf("wide: there is no top\n");
            bad = 1;
        }
        free(dh);
        free(whi);
    }
    free(dd);
    free(wd);
    free(dl);
    free(wl);
    free(wh);
    free(diff);
out:
    free(d);
    free(w);
}

static const char LONG_TEXT[] =
    "Six thick thistle sticks stood in the sun. She sells sixty sea shells, "
    "and the fish stew is chosen for its sharp, fresh zest. Sister Susie "
    "sits sewing shirts for soldiers. Peter took the black cat to the top of "
    "the big park, and kicked a tin can across the grass. The sixth sheik's "
    "sixth sheep is sick. Seven slick slimy snakes slowly sliding southward. "
    "A sailor went to sea to see what he could see, and all that he could "
    "see was the sea. Swiss wristwatches are such a shame to switch. Fresh "
    "fried fish, fish fresh fried, fried fish fresh, fish fried fresh. She "
    "says she shall sew a sheet, and so she sews it slowly, stitch by stitch, "
    "until the sheet is sewn and the session is over.";

/* How loud a band is, moment by moment: the energy of each run of hop
   samples after a lowpass at hi less one at lo. */
static double *band_track(const double *x, long n, double lo, double hi,
                          double hz, long hop, long *count)
{
    double *a = lowpass(x, n, hi, hz);
    double *b = lo > 0.0 ? lowpass(x, n, lo, hz) : 0;
    double *t;
    long k, i;

    *count = n / hop;
    t = calloc((size_t)*count, sizeof(double));
    for (k = 0; k < *count; k++) {
        double e = 0.0;

        for (i = k * hop; i < (k + 1) * hop; i++) {
            double v = a[i] - (b ? b[i] : 0.0);

            e += v * v;
        }
        t[k] = log10(e + 1.0);
    }
    free(a);
    free(b);
    return t;
}

static long lag_between(const double *top, const double *below, long from,
                        long to)
{
    long n = to - from, i, lag, best = 0;
    double ma = 0.0, mb = 0.0, c, most = -1e300;

    for (i = from; i < to; i++) {
        ma += top[i];
        mb += below[i];
    }
    ma /= (double)n;
    mb /= (double)n;
    for (lag = -40; lag <= 40; lag++) {
        c = 0.0;
        for (i = from; i < to; i++) {
            long j = i + lag;

            if (j >= from && j < to)
                c += (top[j] - ma) * (below[i] - mb);
        }
        if (c > most) {
            most = c;
            best = lag;
        }
    }
    return best;
}

static void in_step(void)
{
    enum { HZ = 22050, HOP = 44 };
    short *d = 0, *w = 0;
    long nd = speak(LONG_TEXT, HZ, 0, &d);
    long nw = speak(LONG_TEXT, HZ, 1, &w);
    double *dd, *top, *tt, *bt;
    long n, i, nt, nb, third, first, last;

    if (nd <= 0 || nw != nd) {
        printf("wide: the long text came out %ld and %ld samples\n", nd, nw);
        bad = 1;
        goto out;
    }
    n = nd;
    dd = as_double(d, n);
    top = malloc((size_t)n * sizeof(double));
    for (i = 0; i < n; i++)
        top[i] = (double)w[i] - (double)d[i];

    tt = band_track(top, n, 6000.0, 10000.0, HZ, HOP, &nt);
    bt = band_track(dd, n, 3000.0, 5000.0, HZ, HOP, &nb);
    third = nt / 3;
    first = lag_between(tt, bt, 0, third);
    last = lag_between(tt, bt, nt - third, nt);

    printf("wide: over %.1f seconds the top runs %ld ms behind the band "
           "below it at the start and %ld ms at the end\n",
           (double)n / HZ, first * HOP * 1000 / HZ, last * HOP * 1000 / HZ);
    /* A step either way is the resolution, not a drift: a top made from
       frames ten milliseconds late reads as four steps or five. */
    if (last - first > 1 || first - last > 1 || first < -2 || first > 2) {
        printf("wide: the two halves are not in step\n");
        bad = 1;
    }
    free(dd);
    free(top);
    free(tt);
    free(bt);
out:
    free(d);
    free(w);
}

/* The median of n values, which are left sorted. */
static int by_size(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;

    return x < y ? -1 : x > y;
}

static double median(double *v, long n)
{
    qsort(v, (size_t)n, sizeof(double), by_size);
    return n == 0 ? -1e300 : n & 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

static const char VOWEL_TEXT[] =
    "Are you aware of how early our ordinary hours are over?";

/* How loud a band of one moment is: n samples from at under a Hann window,
   the power of every bin of their transform from lo to hi hertz, in
   decibels. Not a band cut by two lowpasses as above: those differ by their
   ripple across the whole of both passbands, which lets through about fifty
   decibels under whatever is there, and a vowel has more than that under 1.5
   kHz over what it has past the join. */
enum { MOMENT = 1024 };

static double band_db(const double *x, long at, double lo, double hi,
                      double hz)
{
    static double win[MOMENT], cs[MOMENT], sn[MOMENT];
    static int drawn;
    double sum = 0.0;
    long b, i;

    if (!drawn) {
        for (i = 0; i < MOMENT; i++) {
            double t = 2.0 * 3.14159265358979 * (double)i / MOMENT;

            win[i] = 0.5 - 0.5 * cos(t);
            cs[i] = cos(t);
            sn[i] = sin(t);
        }
        drawn = 1;
    }
    for (b = (long)ceil(lo * MOMENT / hz); b < hi * MOMENT / hz; b++) {
        double re = 0.0, im = 0.0;

        for (i = 0; i < MOMENT; i++) {
            long k = b * i % MOMENT;

            re += x[at + i] * win[i] * cs[k];
            im -= x[at + i] * win[i] * sn[k];
        }
        sum += re * re + im * im;
    }
    return 10.0 * log10(sum + 1e-9);
}

/* Which moments of the default stream are a vowel and which frication, by
   where its energy lies: a vowel has it under 1.5 kHz and next to none from
   3 to 5, frication the other way about, and a moment forty decibels under
   the loudest is neither. Moments start every half of one. */
enum { VOWEL = 1, FRICATION = 2 };

static char *moments(const double *dd, long n, double hz, long *count)
{
    long nk = n < MOMENT ? 0 : (n - MOMENT) / (MOMENT / 2), k;
    double *lo = malloc((size_t)nk * sizeof(double) + 1);
    double *hi = malloc((size_t)nk * sizeof(double) + 1);
    double *all = malloc((size_t)nk * sizeof(double) + 1);
    char *kind = calloc((size_t)nk + 1, 1);
    double loudest = -1e300;

    for (k = 0; k < nk; k++) {
        lo[k] = band_db(dd, k * (MOMENT / 2), 100.0, 1500.0, hz);
        hi[k] = band_db(dd, k * (MOMENT / 2), 3000.0, 5000.0, hz);
        all[k] = band_db(dd, k * (MOMENT / 2), 50.0, 5000.0, hz);
        if (all[k] > loudest)
            loudest = all[k];
    }
    for (k = 0; k < nk; k++)
        if (all[k] > loudest - 40.0)
            kind[k] = lo[k] > hi[k] + 15.0 ? VOWEL
                    : hi[k] > lo[k] ? FRICATION : 0;
    free(lo);
    free(hi);
    free(all);
    *count = nk;
    return kind;
}

/* A vowel carries on across the join, falling as it falls under it, and
   goes on falling above. This reads four decibels down just past the
   join. With the companion at IBM's five formants the vowel stopped there,
   and what was left was a floor eight down all the way up: the companion
   rounding to sixteen bits before its high side was raised, which is what
   the far band, more than twenty down for the vowel itself, is for. With
   the three formants above the fifth running but held under IBM's five
   thousand hertz, or not raised, it reads ten down; at IBM's own narrower
   bandwidths it does not fall at all, which the ear liked less. */
static void vowels_across(void)
{
    enum { HZ = 22050 };
    short *d = 0, *w = 0;
    long nd = speak(VOWEL_TEXT, HZ, 0, &d);
    long nw = speak(VOWEL_TEXT, HZ, 1, &w);
    double *dd, *wd, *near_v, *far_v;
    long nk, k, m = 0;
    char *kind;
    double above, beyond;

    if (nd <= 0 || nw != nd) {
        printf("wide: the vowels came out %ld and %ld samples\n", nd, nw);
        bad = 1;
        goto out;
    }
    dd = as_double(d, nd);
    wd = as_double(w, nw);
    kind = moments(dd, nd, HZ, &nk);
    near_v = malloc((size_t)nk * sizeof(double) + 1);
    far_v = malloc((size_t)nk * sizeof(double) + 1);
    for (k = 0; k < nk; k++)
        if (kind[k] == VOWEL) {
            long at = k * (MOMENT / 2);
            double under = band_db(wd, at, 4500.0, 5250.0, HZ);

            near_v[m] = band_db(wd, at, 5750.0, 6500.0, HZ) - under;
            /* Per hertz: the far band is twice as wide. */
            far_v[m] = band_db(wd, at, 8500.0, 10000.0, HZ) - under
                       - 10.0 * log10(2.0);
            m++;
        }
    above = median(near_v, m);
    beyond = median(far_v, m);

    printf("wide: on %ld vowel moments of %ld, 5.75 to 6.5 kHz is %.1f dB "
           "from 4.5 to 5.25, and 8.5 to 10 is %.1f\n", m, nk, above,
           beyond);
    if (m < nk / 2) {
        printf("wide: the vowels were not found\n");
        bad = 1;
    }
    if (above < -7.0) {
        printf("wide: the vowels stop at the join\n");
        bad = 1;
    }
    if (above > -1.0) {
        printf("wide: the vowels do not fall across the join\n");
        bad = 1;
    }
    if (beyond > -15.0) {
        printf("wide: what is above the vowels is not the vowels\n");
        bad = 1;
    }
    free(dd);
    free(wd);
    free(kind);
    free(near_v);
    free(far_v);
out:
    free(d);
    free(w);
}

/* What the vowels gained, the fricatives did not: the top was settled by
   ear on frication, and what raises the vowels' is applied before the
   frication joins them. So on the moments the default stream says are
   frication, the top alone -- the wideband stream less the default, which
   at the wide rate is exactly the companion's high side at its level --
   above 5.75 kHz, against the default's own 3 to 5. It read the same before
   the vowels had a top, and two decibels either way is a third of the
   smallest step the ear was offered. */
#define FRICATION_TOP  (-12.5)

static void frication_unmoved(void)
{
    enum { HZ = 22050 };
    short *d = 0, *w = 0;
    long nd = speak(SHORT_TEXT, HZ, 0, &d);
    long nw = speak(SHORT_TEXT, HZ, 1, &w);
    double *dd, *top, *v;
    long nk, k, m = 0;
    char *kind;
    double level;

    if (nd <= 0 || nw != nd) {
        printf("wide: the fricatives came out %ld and %ld samples\n", nd,
               nw);
        bad = 1;
        goto out;
    }
    dd = as_double(d, nd);
    top = malloc((size_t)nd * sizeof(double));
    for (k = 0; k < nd; k++)
        top[k] = (double)w[k] - (double)d[k];
    kind = moments(dd, nd, HZ, &nk);
    v = malloc((size_t)nk * sizeof(double) + 1);
    for (k = 0; k < nk; k++)
        if (kind[k] == FRICATION) {
            long at = k * (MOMENT / 2);

            v[m++] = band_db(top, at, 5750.0, HZ / 2.0, HZ)
                     - band_db(dd, at, 3000.0, 5000.0, HZ);
        }
    level = median(v, m);

    printf("wide: on %ld fricative moments the top is %.1f dB from 3 to "
           "5 kHz under it, and was %.1f\n", m, level, FRICATION_TOP);
    if (m < 10 || level < FRICATION_TOP - 2.0
        || level > FRICATION_TOP + 2.0) {
        printf("wide: the fricatives' top has moved\n");
        bad = 1;
    }
    free(dd);
    free(top);
    free(kind);
    free(v);
out:
    free(d);
    free(w);
}

/* Nothing after the voice. IBM's resonators round each product down, so in
   silence they sit off nought, and when the cascade is switched off after
   speech the companion's top stepped back to nought: a click twenty-five to
   sixty-five milliseconds after the voice. So from where the default
   stream's speech ends, the top alone has to stay quiet, after a text that
   ends on a vowel and one that ends on a t. */
static const char *const ENDINGS[] = {
    VOWEL_TEXT,
    "She sells sixty sea shells, and the fish stew is chosen for its sharp, "
    "fresh zest."
};

static void nothing_after(void)
{
    enum { HZ = 22050 };
    size_t t;

    for (t = 0; t < sizeof ENDINGS / sizeof *ENDINGS; t++) {
        short *d = 0, *w = 0;
        long nd = speak(ENDINGS[t], HZ, 0, &d);
        long nw = speak(ENDINGS[t], HZ, 1, &w);
        long end = -1, at = 0, i;
        int most = 0;

        if (nd <= 0 || nw != nd) {
            printf("wide: an ending came out %ld and %ld samples\n", nd, nw);
            bad = 1;
            free(d);
            free(w);
            continue;
        }
        for (i = 0; i < nd; i++)
            if (d[i] > 300 || d[i] < -300)
                end = i;
        for (i = end + 1; i < nd; i++) {
            int v = w[i] - d[i];

            if (v < 0)
                v = -v;
            if (v > most) {
                most = v;
                at = i - end;
            }
        }
        printf("wide: after the voice stops the top reaches %d, %ld ms "
               "later\n", most, at * 1000 / HZ);
        if (most > 20) {
            printf("wide: there is a click after the voice\n");
            bad = 1;
        }
        free(d);
        free(w);
    }
}

static void the_setting(void)
{
    OldInst *h = eo_new();

    if (h == 0) {
        printf("wide: no instance\n");
        bad = 1;
        return;
    }
    if (eo_getParam(h, PARAM_WIDEBAND) != 0) {
        printf("wide: a fresh instance has it on\n");
        bad = 1;
    }
    if (ev_setParam(h, PARAM_WIDEBAND, 1) != 0
        || eo_getParam(h, PARAM_WIDEBAND) != 1) {
        printf("wide: it would not go on\n");
        bad = 1;
    }
    if (ev_setParam(h, PARAM_WIDEBAND, 2) != -1
        || eo_getParam(h, PARAM_WIDEBAND) != 1) {
        printf("wide: a two was taken\n");
        bad = 1;
    }
    {
        int done = es_reset(h);

        if (eo_getParam(h, PARAM_WIDEBAND) != 0) {
            printf("wide: a reset (answering %d) left it at %d\n", done,
                   eo_getParam(h, PARAM_WIDEBAND));
            bad = 1;
        }
    }
    es_delete(h);
}

/* At the engine's own two rates there is no top to add. */
static void nothing_at(int32_t hz)
{
    short *d = 0, *w = 0;
    long nd = speak(SHORT_TEXT, hz, 0, &d);
    long nw = speak(SHORT_TEXT, hz, 1, &w);

    if (nd <= 0 || nd != nw || memcmp(d, w, (size_t)nd * sizeof(short))) {
        printf("wide: at %d hertz it changed the sound\n", (int)hz);
        bad = 1;
    }
    free(d);
    free(w);
}

int main(void)
{
    evv_port_start();
    evvRunStaticInitialisers();
    kept = malloc(ROOM * sizeof(short));
    if (kept == 0)
        return 1;

    the_setting();
    nothing_at(8000);
    nothing_at(11025);
    same_below(22050);
    same_below(16000);
    same_below(44100);
    in_step();
    vowels_across();
    frication_unmoved();
    nothing_after();

    free(kept);
    evv_port_finish();
    printf(bad ? "wide: FAILED\n" : "wide: passed\n");
    return bad;
}
