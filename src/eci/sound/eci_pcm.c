/* The boundary between the engine and whatever plays the sound.
 *
 * Everything above this file works in samples and knows nothing about
 * devices. Below it, IBM's original had three objects that between them
 * opened a Windows waveform device, converted between sample formats using
 * WAVEFORMATEX, and pushed buffers at it. None of that survives a port: a
 * Rockbox build has its own PCM path and, like our harness, hands the engine
 * a buffer of its own rather than asking the engine to find a speaker.
 *
 * So this is not a transcription. It is the same interface, met by code that
 * belongs to us, and it is deliberately the one place in the project where
 * that is true.
 *
 * This first version reports every call and does nothing else, so that the
 * question of which of these the engine actually reaches can be settled by
 * running it rather than by reading. */

#include <stdint.h>
#include <stdio.h>
#include "eci_synththread.h"
#include "evv_abi.h"

static void pcm_saw(const char *what)
{
    (void)what;
}

/* ---- where finished samples go --------------------------------------- */

/* Sixty-four bytes embedded in the sound thread. Nothing outside this file
   looks inside it. */
typedef struct SoundOutput { uint8_t opaque[0x40]; } SoundOutput;

THIS void *pcm_ctor(SoundOutput *o)
{
    pcm_saw("SoundOutput ctor");
    return o;
}

THIS void pcm_dtor(SoundOutput *o)
{
    (void)o;
    pcm_saw("SoundOutput dtor");
}

THIS int16_t pcm_open(SoundOutput *o)
{
    (void)o;
    pcm_saw("open");
    return 0;
}

THIS int32_t pcm_close(SoundOutput *o)
{
    (void)o;
    pcm_saw("close");
    return 0;
}

THIS int32_t pcm_reset(SoundOutput *o)
{
    (void)o;
    pcm_saw("reset");
    return 0;
}

THIS int32_t pcm_flush(SoundOutput *o)
{
    (void)o;
    pcm_saw("flush");
    return 0;
}

THIS int32_t pcm_hold(SoundOutput *o, int32_t on)
{
    (void)o;
    (void)on;
    pcm_saw("hold");
    return 0;
}

THIS int32_t pcm_write(SoundOutput *o, const int32_t *data, uint32_t n)
{
    (void)o;
    (void)data;
    (void)n;
    pcm_saw("write");
    return 0;
}

THIS int32_t pcm_insertIndex(SoundOutput *o, int32_t i)
{
    (void)o;
    (void)i;
    pcm_saw("insertIndex");
    return 0;
}

THIS int16_t pcm_getStatus(SoundOutput *o)
{
    (void)o;
    pcm_saw("getStatus");
    return 0;
}

THIS int32_t pcm_setup(SoundOutput *o, char *a, int32_t *b, int32_t *c,
                       int32_t *d, int32_t *e, int32_t *f, int32_t *g,
                       int32_t *h)
{
    (void)o; (void)a; (void)b; (void)c;
    (void)d; (void)e; (void)f; (void)g; (void)h;
    pcm_saw("setup");
    return 1;
}

/* ---- turning one sample format into another -------------------------- */

/* Raising the rate, four ways, none of them the engine's business.

   This is the one object of IBM's sound layer the port never transcribed,
   and it is the reason a caller asking for twenty-two thousand used to be
   handed the eleven thousand stream under a twenty-two thousand label -- the
   same speech at half the duration. It is ours rather than a transcription,
   for the reason the head of this file gives about the rest of the layer.

   The engine runs at eleven thousand and twenty five and this decides what
   the samples in between are. Which way is a matter for a listener, so there
   are four and EVV_UPSAMPLE picks:

   hold    the sample before, repeated until the next one is due. Keeps the
   mirror of the speech that a resampler exists to remove, which is
   what old hardware did and what some ears want. Every value that
   comes out is a value the engine put in, so what a caller gets at
   twenty-two thousand is Eloquence to the byte.
   zeros   the sample, then silence until the next is due. The mirror at
   full strength with no droop at the top of the band: brighter and
   harder still, and quieter, since only one sample in so many
   carries anything.
   linear  a straight line between one sample and the next. Suppresses the
   mirror by roughly twice what holding does.
   cubic   a curve through four of them, which is what libsoxr calls its
   quick mode. It suppresses the mirror by some nine decibels over
   holding, which is a rate change rather than an effect, and it is
   well short of removing it.
   sinc    a windowed sinc across a hundred and ninety-two of them,
   which is what a resampler actually is and is the default. The
   images are gone rather than quieter -- some fifty decibels below
   where the curve leaves them -- and the passband comes through
   flat instead of drooping at the top. Where it stops passing and
   how many samples it takes to stop are in eci_pcm.h with the
   argument for the particular numbers, and EVV_SINC_CUTOFF and
   EVV_SINC_TAPS move them.

   The sinc is the default because the curve was not enough. Held against
   Apple's driver, which does its own resampling out of an eci.dylib that
   only runs at eight and eleven thousand, the curve was the closest of the
   cheap ways and still short of it. What separates a resampler from an
   interpolator is the stopband, and only a real filter has one.

   Written here rather than linked from libsoxr because the engine has no
   dependency but the C library and gains none here: this ships inside a DLL
   a screen reader loads and inside builds for platforms nobody has put soxr
   on. The same kind of filter, not the same code, and in whole numbers where
   soxr works in floating point, for the reason src/klatt/klatt_fx.h gives.

   Both interpolating ways look only backwards, at samples already handed
   over, so a run joins the one before it with no seam and nothing is held
   back at the end. What that costs is a constant delay of one input sample
   for linear and two for cubic -- under two tenths of a millisecond -- which
   is why the history below is three samples deep.

   The ratio need not be whole. Position is counted in units of one input
   sample over the output rate, so twenty-two and forty-four thousand come to
   an even number of copies and sixteen, twenty-four, thirty-two and
   forty-eight thousand come to an uneven one, by the same arithmetic.
   */

#include <stdlib.h>
#include <string.h>

#include "eci_pcm.h"
#include "klatt_fx.h"

/* What the layer above hands down, and what comes back. */
typedef struct { void *at; uint32_t bytes; } SDATA;

/* The platform's own audio header, as eci_synthwork.c fills it in. Only the
   rate is read here. */
typedef struct {
    uint16_t tag;
    uint16_t channels;
    uint32_t rate;
    uint32_t bytesPerSecond;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    uint16_t extra;
} WaveFormat;

/* How far up the band to pass and over how many samples, read once. Both are
   experiment knobs rather than settings: an engine that changed its mind
   halfway through an utterance would be comparing two things at once. The
   cutoff is in ten-thousandths of the input's own Nyquist. */
static int32_t cvt_cutoff(void)
{
    static int     decided;
    static int32_t cutoff = SINC_CUTOFF;

    if (!decided) {
        const char *say = getenv("EVV_SINC_CUTOFF");

        decided = 1;
        if (say != 0) {
            double v = atof(say);

            /* Below a half there is no point and above one there is no
               meaning: a filter cannot pass what the input never carried. */
            if (v >= 0.5 && v <= 1.0)
                cutoff = (int32_t)(v * 10000.0 + 0.5);
        }
    }
    return cutoff;
}

static int32_t cvt_taps(void)
{
    static int     decided;
    static int32_t taps = SINC_TAPS;

    if (!decided) {
        const char *say = getenv("EVV_SINC_TAPS");

        decided = 1;
        if (say != 0) {
            int32_t v = (int32_t)atoi(say);

            /* Even, because the window sits between the two middle samples,
               and enough of them to be a filter at all. */
            if (v >= 8 && v <= SINC_TAPS_MAX)
                taps = v & ~1;
        }
    }
    return taps;
}

/* One over pi, in units of 2^-30. */
#define CVT_INV_PI  341782638

/* The modified Bessel function of the first kind, order nought, which is
   what shapes a Kaiser window: of x in units of 2^-26, answering in them.
   The series converges in a couple of dozen terms for the arguments a
   window of this shape asks for, and with the window's beta at eight no
   term or product comes near sixty-three bits. */
static int64_t cvt_i0(int64_t x)
{
    int64_t half = x / 2, term = (int64_t)1 << 26, sum = term;
    int     k;

    for (k = 1; k < 64 && term != 0; k++) {
        int64_t q = fx_div(half, k);

        term = fx_shift(fx_shift(term * q, 26) * q, 26);
        sum += term;
    }
    return sum;
}

/* Draw the filter: a sinc cut off below the input's Nyquist, under a
   Kaiser window, sampled finely enough that a straight line between two of
   its points is not what limits the stopband. The middle and one side,
   since it is symmetric: base[i] is i/SINC_PHASES input samples out, in
   units of 2^-30.

   In units of one input sample throughout, which is what makes it the same
   filter whatever the ratio: raising a rate needs the images of the input
   removed, and where those images begin is a property of the input alone.

   And in whole numbers, sine, root and Bessel function alike, so that what
   it comes to is a property of this code rather than of a maths library or
   the processor under it. */
static int32_t *cvt_draw(int32_t half, int32_t cutoff)
{
    int32_t  top = half * SINC_PHASES;
    int64_t  sq = (int64_t)top * top;
    int64_t  edge = cvt_i0((int64_t)SINC_BETA << 26);
    int32_t *base = malloc((size_t)(top + 1) * sizeof(int32_t));
    int32_t  i;

    if (base == 0)
        return 0;
    base[0] = (int32_t)fx_div((int64_t)cutoff << 30, 10000);
    for (i = 1; i <= top; i++) {
        /* sqrt(1 - x^2) for x the way through the window, then the window
           there, as a fraction of its middle. */
        int64_t root = (int64_t)fx_isqrt(
            (uint64_t)fx_div((sq - (int64_t)i * i) << 30, sq) << 30);
        int64_t window =
            fx_div(cvt_i0(fx_shift(SINC_BETA * root, 4)) << 28, edge) << 2;
        int32_t c, s;
        int64_t v;

        /* sin(pi cutoff t) / (pi t), the angle being cutoff t / 2 of a
           turn. */
        fx_cos_sin((int64_t)cutoff * i, (int64_t)20000 * SINC_PHASES, &c, &s);
        v = fx_shift(fx_div((int64_t)s * SINC_PHASES, i) * CVT_INV_PI, 30);
        base[i] = (int32_t)fx_shift(v * window, 30);
    }
    return base;
}

/* The filter num/den input samples from its middle, by a straight line
   between the two points of the drawing either side.

   Where on the drawing that falls is settled in whole numbers, and that is
   not a nicety. The window is not nought at its ends, so the filter steps
   there, and lowering a rate puts taps exactly on the step: from 22,050 to
   16,000, two output samples in every 320. Worked out as a rounded product
   in doubles, whether such a tap counted came down to the last bit, which
   x87 kept where every other processor dropped it. */
static int64_t cvt_weight_at(const int32_t *base, int32_t half, int64_t num,
                             int64_t den)
{
    int64_t top = (int64_t)half * SINC_PHASES;
    int64_t at = (num + (int64_t)half * den) * SINC_PHASES;
    int64_t i, a, b;

    if (at <= 0 || at >= 2 * top * den)
        return 0;
    i = at / den;
    a = base[i < top ? top - i : i - top];
    b = base[i + 1 < top ? top - i - 1 : i + 1 - top];
    return a + fx_div((b - a) * (at - i * den), den);
}

static int32_t cvt_gcd(int32_t a, int32_t b)
{
    while (b != 0) {
        int32_t t = a % b;

        a = b;
        b = t;
    }
    return a;
}

/* Which of the places worked out an output sample uses, rem/to of an input
   sample past the one before it. */
static int32_t cvt_place(const PcmResampler *r, int32_t rem)
{
    int32_t p;

    if (r->spacing != 0)
        return rem / r->spacing;
    p = (int32_t)(((int64_t)rem * r->phases + r->to / 2) / r->to);
    return p < r->phases ? p : r->phases - 1;
}

/* The weights for every place an output sample can fall, once. Two rates
   share a grid every gcd of them, so there are to/gcd such places: two
   raising 11,025 to 22,050, and 1,280 raising it to 32,000, which is the
   most any numbered rate asks. A rate given in hertz can ask for tens of
   thousands, and past SINC_PLACES_MAX the places are spread evenly instead
   and a sample takes the nearest.

   Each place's weights are scaled to come to exactly one, and whatever
   rounding leaves over goes on the largest, so a level in is that level
   out as a matter of arithmetic rather than of the window and cutoff,
   which are numbers somebody will want to change. */
static int cvt_places(PcmResampler *r, const int32_t *base)
{
    int32_t g = cvt_gcd(r->from, r->to);
    int32_t taps = 2 * r->reach;
    int64_t den = r->to < r->from ? r->from : r->to;
    int32_t p, k;

    r->spacing = g;
    r->phases = r->to / g;
    if (r->phases > SINC_PLACES_MAX) {
        r->spacing = 0;
        r->phases = SINC_PLACES_MAX;
    }
    r->weights = malloc((size_t)r->phases * (size_t)taps * sizeof(int32_t));
    if (r->weights == 0)
        return 0;

    for (p = 0; p < r->phases; p++) {
        int32_t *w = r->weights + (size_t)p * (size_t)taps;
        int64_t  rem = r->spacing != 0
                     ? (int64_t)p * g
                     : fx_div((int64_t)p * r->to, r->phases);
        int64_t  sum = 0, got = 0;
        int32_t  big = 0;

        /* Tap k is input sample i - 2 reach + 1 + k for an output sample
           whose walk is at i and rem: (k - reach + 1 - rem/to) input samples
           from the window's middle, which lowering stretches by to/from, so
           a whole number over the higher of the two rates either way. */
        for (k = 0; k < taps; k++) {
            int64_t v = cvt_weight_at(base, r->half,
                                      (int64_t)(k - r->reach + 1) * r->to
                                      - rem, den);

            w[k] = (int32_t)v;
            sum += v;
        }
        for (k = 0; k < taps; k++) {
            w[k] = (int32_t)fx_div((int64_t)w[k] << 30, sum);
            got += w[k];
            if ((w[k] < 0 ? -w[k] : w[k]) > (w[big] < 0 ? -w[big] : w[big]))
                big = k;
        }
        w[big] += (int32_t)(FX_ONE - got);
    }
    return 1;
}

int pcm_resample_start(PcmResampler *r, int32_t from, int32_t to,
                       int32_t method)
{
    /* Nothing is given back here: what arrives is taken as uninitialised,
       because that is what it usually is, and reading a pointer out of a
       struct nobody has cleared to decide whether to free it is how a stack
       variable becomes a crash. Whoever starts one ends it. */
    memset(r, 0, sizeof *r);
    r->from = from;
    r->to = to;
    r->method = method;
    if (method == CVT_SINC) {
        int32_t *base;
        int      ok;

        r->half = cvt_taps() / 2;
        r->reach = r->half;
        if (to < from)
            r->reach = (int32_t)(((int64_t)r->half * from + to - 1) / to);
        base = cvt_draw(r->half, cvt_cutoff());
        if (base == 0)
            return 0;
        ok = cvt_places(r, base);
        free(base);
        return ok;
    }
    return 1;
}

void pcm_resample_end(PcmResampler *r)
{
    free(r->weights);
    r->weights = 0;
    r->half = 0;
}

int32_t pcm_resample_delay(const PcmResampler *r)
{
    switch (r->method) {
    case CVT_LINEAR: return 1;
    case CVT_CUBIC:  return 2;
    case CVT_SINC:   return r->reach;
    default:         return 0;
    }
}

int32_t pcm_resample_weight(const PcmResampler *r, int32_t num, int32_t den)
{
    int64_t D = r->to < r->from ? r->from : r->to;
    int64_t t, rem, k;

    if (r->weights == 0 || r->spacing == 0 || den <= 0
        || ((int64_t)num * D) % den != 0)
        return 0;
    /* In units of 1/D, as the places were worked out, and the rem that puts
       a tap there: whatever brings it to a whole number of `to'. */
    t = (int64_t)num * D / den;
    rem = t >= 0 ? (r->to - t % r->to) % r->to : (-t) % r->to;
    if (rem % r->spacing != 0)
        return 0;
    k = (t + rem) / r->to + r->reach - 1;
    if (k < 0 || k >= 2 * r->reach)
        return 0;
    return r->weights[(size_t)(rem / r->spacing) * (size_t)(2 * r->reach)
                      + (size_t)k];
}

/* How many output samples a run of n input ones comes to. Every output
   position whose input index falls inside the run, and no more, so nothing
   is held back and the count over a whole utterance is exact. */
uint32_t pcm_resample_count(const PcmResampler *r, uint32_t n)
{
    int64_t span = (int64_t)n * r->to - r->at;

    if (span <= 0)
        return 0;
    return (uint32_t)((span + r->from - 1) / r->from);
}

/* The samples handed over lie at 0 and up; the three before them are the
   tail of the run before. */
static int32_t cvt_tap(const PcmResampler *r, const int32_t *src, uint32_t n,
                       int32_t i)
{
    if (i < 0)
        return r->history[CVT_HISTORY + i];
    if ((uint32_t)i >= n)
        return src[n - 1];
    return src[i];
}

/* Sixteen bits is where these are going, and a curve through four points can
   overshoot the points. Left to itself that wraps rather than clips, which
   is a click and not a loud sample. */
static int32_t cvt_clamp(int64_t v)
{
    if (v > 32767)
        return 32767;
    if (v < -32768)
        return -32768;
    return (int32_t)v;
}

uint32_t pcm_resample(PcmResampler *r, const int32_t *src, uint32_t n,
                      int32_t *out)
{
    uint32_t made = 0;
    int32_t  at = r->at;
    int32_t  last_i = -1;

    while ((int64_t)at < (int64_t)n * r->to) {
        int32_t i = at / r->to;
        int32_t rem = at - i * r->to;

        switch (r->method) {
        case CVT_ZEROS:
            out[made] = (i == last_i) ? 0 : cvt_tap(r, src, n, i);
            break;

        case CVT_LINEAR: {
            int64_t a = cvt_tap(r, src, n, i - 1);
            int64_t b = cvt_tap(r, src, n, i);

            out[made] = cvt_clamp(fx_div(a * r->to + (b - a) * rem, r->to));
            break;
        }

        case CVT_CUBIC: {
            /* Catmull-Rom through four, curving between the middle two, with
               the fraction in units of 2^-16: as an exact fraction its cube
               would not fit in sixty-four bits at every rate a caller may
               name. */
            int64_t p0 = cvt_tap(r, src, n, i - 3);
            int64_t p1 = cvt_tap(r, src, n, i - 2);
            int64_t p2 = cvt_tap(r, src, n, i - 1);
            int64_t p3 = cvt_tap(r, src, n, i);
            int64_t f = fx_div((int64_t)rem << 16, r->to);
            int64_t v = ((2 * p0 - 5 * p1 + 4 * p2 - p3) << 16)
                      + (3 * (p1 - p2) + p3 - p0) * f;

            v = ((p2 - p0) << 16) + fx_shift(v * f, 16);
            v = fx_shift(v * f, 16);
            out[made] = cvt_clamp(fx_shift((p1 << 17) + v, 17));
            break;
        }

        case CVT_SINC: {
            /* The window sits on the position, and the position is behind
               the newest sample by half the window, so every sample it
               reaches has already been handed over. */
            const int32_t *w = r->weights
                             + (size_t)cvt_place(r, rem)
                               * (size_t)(2 * r->reach);
            int64_t acc = 0;
            int32_t k;

            for (k = 0; k < 2 * r->reach; k++)
                acc += (int64_t)w[k]
                       * cvt_tap(r, src, n, i - 2 * r->reach + 1 + k);
            out[made] = cvt_clamp(fx_shift(acc, 30));
            break;
        }

        default:
            out[made] = cvt_tap(r, src, n, i);
            break;
        }

        last_i = i;
        made++;
        at += r->from;
    }

    /* Rebase the walk on the run that follows, and keep its tail. */
    r->at = at - (int32_t)((int64_t)n * r->to);
    {
        /* The last three samples of what has been handed over so far, which
           is some of this run and, where this run was shorter than three,
           the tail of the one before. Counted signed: a run longer than the
           history is the ordinary case and the arithmetic below must not
           depend on the branch above it to stay in range. */
        int32_t k;

        for (k = 0; k < CVT_HISTORY; k++) {
            int32_t want = (int32_t)n - CVT_HISTORY + k;

            r->history[k] = want >= 0 ? src[want]
                                      : r->history[CVT_HISTORY + want];
        }
    }
    return made;
}

typedef struct AudioConverter {
    PcmResampler  walk;
    int32_t      *room;      /* what convertSamples answers with */
    uint32_t      samples;   /* how many it has room for */
    SDATA         out;
} AudioConverter;

const uint32_t pcm_cvt_bytes = sizeof(AudioConverter);

/* Read once, because it is an experiment knob and not a setting: an engine
   that changed its mind halfway through an utterance would be comparing two
   things at once. */
static int cvt_method(void)
{
    static int decided;
    static int method = CVT_SINC;

    if (!decided) {
        const char *say = getenv("EVV_UPSAMPLE");

        decided = 1;
        if (say != 0) {
            if (strcmp(say, "zeros") == 0)
                method = CVT_ZEROS;
            else if (strcmp(say, "linear") == 0)
                method = CVT_LINEAR;
            else if (strcmp(say, "hold") == 0)
                method = CVT_HOLD;
            else if (strcmp(say, "cubic") == 0)
                method = CVT_CUBIC;
            else
                method = CVT_SINC;
        }
    }
    return method;
}

THIS void *pcm_cvt_ctor(AudioConverter *c)
{
    memset(c, 0, sizeof *c);
    pcm_resample_start(&c->walk, 1, 1, cvt_method());
    return c;
}

THIS void pcm_cvt_dtor(AudioConverter *c)
{
    pcm_resample_end(&c->walk);
    free(c->room);
    c->room = 0;
    c->samples = 0;
}

THIS int32_t pcm_cvt_setSource(AudioConverter *c, void *fmt)
{
    c->walk.from = (int32_t)((WaveFormat *)fmt)->rate;
    return 0;
}

THIS int32_t pcm_cvt_setDest(AudioConverter *c, void *fmt)
{
    int32_t to = (int32_t)((WaveFormat *)fmt)->rate;

    /* Lowered only from the wideband voice's rate to sixteen thousand, and
       only by the sinc: the other ways are ways of filling in between
       samples and have nothing to say about taking some away. */
    if (c->walk.from <= 0 || 2 * to < c->walk.from)
        return -1;

    {
        /* A second format on the same converter, so whatever the first one
           drew is given back before the next is. */
        int32_t from = c->walk.from;

        pcm_resample_end(&c->walk);
        return pcm_resample_start(&c->walk, from, to,
                                  to < from ? CVT_SINC : cvt_method())
               ? 0 : -1;
    }
}

/* Enough room for n samples, kept between runs so a steady stream of them
   allocates once. */
static int cvt_room(AudioConverter *c, uint32_t n)
{
    if (n <= c->samples)
        return 1;

    {
        int32_t *bigger = realloc(c->room, (size_t)n * sizeof(int32_t));

        if (bigger == 0)
            return 0;
        c->room = bigger;
        c->samples = n;
    }
    return 1;
}

THIS int32_t pcm_cvt_convert(AudioConverter *c, SDATA in, SDATA **out)
{
    const int32_t *src = in.at;
    uint32_t n = in.bytes >> 2;
    uint32_t want, made;

    if (src == 0 || c->walk.to == c->walk.from) {
        /* Nothing to do, and saying so by handing back what came in keeps
           the caller's one code path. */
        c->out.at = (void *)src;
        c->out.bytes = in.bytes;
        *out = &c->out;
        return 0;
    }

    want = pcm_resample_count(&c->walk, n);
    if (!cvt_room(c, want))
        return -1;

    made = pcm_resample(&c->walk, src, n, c->room);
    c->out.at = c->room;
    c->out.bytes = made << 2;
    *out = &c->out;
    return 0;
}

/* The tail of a run is already kept by convertSamples, which is where it is
   known. This is the call the engine makes to say a run has ended. */
THIS void pcm_cvt_storeHistory(AudioConverter *c)
{
    (void)c;
}

/* The format descriptor the format table asks for. */
int32_t ealAudioSoundFormat[16];

ALIAS("??0SoundOutput@@QAE@XZ", "pcm_ctor");
ALIAS("??1SoundOutput@@QAE@XZ", "pcm_dtor");
ALIAS("?open@SoundOutput@@QAE?AW4SoundFileErrorEnum@@XZ", "pcm_open");
ALIAS("?close@SoundOutput@@QAEHXZ", "pcm_close");
ALIAS("?reset@SoundOutput@@QAEHXZ", "pcm_reset");
ALIAS("?flush@SoundOutput@@QAE?AW4SoundFileErrorEnum@@XZ", "pcm_flush");
ALIAS("?hold@SoundOutput@@QAEHH@Z", "pcm_hold");
ALIAS("?write@SoundOutput@@QAE?AW4SoundFileErrorEnum@@PBJI@Z", "pcm_write");
ALIAS("?insertIndex@SoundOutput@@QAEHJ@Z", "pcm_insertIndex");
ALIAS("?getStatus@SoundOutput@@QAE?AW4SoundFileStatusEnum@@XZ",
      "pcm_getStatus");
ALIAS("?setup@SoundOutput@@QAEHPADPAJ111111@Z", "pcm_setup");

ALIAS("??0AudioConverter@@QAE@XZ", "pcm_cvt_ctor");
ALIAS("??1AudioConverter@@QAE@XZ", "pcm_cvt_dtor");
ALIAS("?setSourceFormat@AudioConverter@@QAEJPAUtWAVEFORMATEX@@@Z",
      "pcm_cvt_setSource");
ALIAS("?setDestFormat@AudioConverter@@QAEJPAUtWAVEFORMATEX@@@Z",
      "pcm_cvt_setDest");
ALIAS("?convertSamples@AudioConverter@@QAEJUSDATA@@PAPAU2@@Z",
      "pcm_cvt_convert");
ALIAS("?storeHistory@AudioConverter@@QAEXXZ", "pcm_cvt_storeHistory");
