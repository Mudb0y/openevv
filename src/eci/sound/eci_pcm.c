/* The boundary between the engine and whatever plays the sound.

   Everything above this file works in samples and knows nothing about
   devices. Below it, IBM's original had three objects that between them
   opened a Windows waveform device, converted between sample formats using
   WAVEFORMATEX, and pushed buffers at it. None of that survives a port: a
   Rockbox build has its own PCM path and, like our harness, hands the engine
   a buffer of its own rather than asking the engine to find a speaker.

   So this is not a transcription. It is the same interface, met by code that
   belongs to us, and it is deliberately the one place in the project where
   that is true.

   This first version reports every call and does nothing else, so that the
   question of which of these the engine actually reaches can be settled by
   running it rather than by reading. */

#include <stdint.h>
#include <stdio.h>
#include "eci_synththread.h"
#include "evv_abi.h"

static void pcm_saw(const char *what)
{
    (void)what;
}

/* ---- where finished samples go -------------------------------------- */

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

/* ---- turning one sample format into another ------------------------- */

/* Raising the rate, four ways, none of them the engine's business.
 *
 * This is the one object of IBM's sound layer the port never transcribed,
 * and it is the reason a caller asking for twenty-two thousand used to be
 * handed the eleven thousand stream under a twenty-two thousand label -- the
 * same speech at half the duration. It is ours rather than a transcription,
 * for the reason the head of this file gives about the rest of the layer.
 *
 * The engine runs at eleven thousand and twenty five and this decides what
 * the samples in between are. Which way is a matter for a listener, so there
 * are four and EVV_UPSAMPLE picks:
 *
 *   hold    the sample before, repeated until the next one is due. Keeps the
 *           mirror of the speech that a resampler exists to remove, which is
 *           what old hardware did and what some ears want. Every value that
 *           comes out is a value the engine put in, so what a caller gets at
 *           twenty-two thousand is Eloquence to the byte.
 *   zeros   the sample, then silence until the next is due. The mirror at
 *           full strength with no droop at the top of the band: brighter and
 *           harder still, and quieter, since only one sample in so many
 *           carries anything.
 *   linear  a straight line between one sample and the next. Suppresses the
 *           mirror by roughly twice what holding does.
 *   cubic   a curve through four of them, which is what libsoxr calls its
 *           quick mode. It suppresses the mirror by some nine decibels over
 *           holding, which is a rate change rather than an effect, and it is
 *           well short of removing it.
 *   sinc    a windowed sinc across a hundred and ninety-two of them,
 *           which is what a resampler actually is and is the default. The
 *           images are gone rather than quieter -- some fifty decibels below
 *           where the curve leaves them -- and the passband comes through
 *           flat instead of drooping at the top. Where it stops passing and
 *           how many samples it takes to stop are in eci_pcm.h with the
 *           argument for the particular numbers, and EVV_SINC_CUTOFF and
 *           EVV_SINC_TAPS move them.
 *
 * The sinc is the default because the curve was not enough. Held against
 * Apple's driver, which does its own resampling out of an eci.dylib that
 * only runs at eight and eleven thousand, the curve was the closest of the
 * cheap ways and still short of it. What separates a resampler from an
 * interpolator is the stopband, and only a real filter has one.
 *
 * Written here rather than linked from libsoxr because the engine has no
 * dependency but the C library and gains none here: this ships inside a DLL
 * a screen reader loads and inside builds for platforms nobody has put soxr
 * on. Same arithmetic, not the same code.
 *
 * Both interpolating ways look only backwards, at samples already handed
 * over, so a run joins the one before it with no seam and nothing is held
 * back at the end. What that costs is a constant delay of one input sample
 * for linear and two for cubic -- under two tenths of a millisecond -- which
 * is why the history below is three samples deep.
 *
 * The ratio need not be whole. Position is counted in units of one input
 * sample over the output rate, so twenty-two and forty-four thousand come to
 * an even number of copies and sixteen, twenty-four, thirty-two and
 * forty-eight thousand come to an uneven one, by the same arithmetic.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "eci_pcm.h"

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

/* The modified Bessel function of the first kind, order nought, which is
   what shapes a Kaiser window. The series converges in a dozen terms for the
   arguments a window of this shape asks for. */
static double cvt_i0(double x)
{
    double term = 1.0, sum = 1.0, half = x / 2.0;
    int    k;

    for (k = 1; k < 40; k++) {
        term *= (half / (double)k) * (half / (double)k);
        sum += term;
        if (term < sum * 1e-16)
            break;
    }
    return sum;
}

/* Draw the filter: a sinc cut off below the input's own Nyquist, under a
   Kaiser window, sampled finely enough that a straight line between two of
   its points is not what limits the stopband.

   In units of one input sample throughout, which is what makes it the same
   filter whatever the ratio: raising a rate needs the images of the input
   removed, and where those images begin is a property of the input alone. */
/* How far up the band to pass and over how many samples, read once. Both are
   experiment knobs rather than settings: an engine that changed its mind
   halfway through an utterance would be comparing two things at once. */
static double cvt_cutoff(void)
{
    static int    decided;
    static double cutoff = SINC_CUTOFF;

    if (!decided) {
        const char *say = getenv("EVV_SINC_CUTOFF");

        decided = 1;
        if (say != 0) {
            double v = atof(say);

            /* Below a half there is no point and above one there is no
               meaning: a filter cannot pass what the input never carried. */
            if (v >= 0.5 && v <= 1.0)
                cutoff = v;
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

/* Answers zero when there is no room for the filter. */
static int cvt_draw(PcmResampler *r)
{
    double  cutoff = cvt_cutoff();
    double  edge = cvt_i0(SINC_BETA);
    int32_t half = cvt_taps() / 2;
    int32_t count = 2 * half * SINC_PHASES + 1;
    int32_t i;

    r->sinc = malloc((size_t)count * sizeof(double));
    if (r->sinc == 0)
        return 0;
    r->half = half;

    for (i = 0; i < count; i++) {
        double t = (double)(i - half * SINC_PHASES) / (double)SINC_PHASES;
        double x = t / (double)half;
        double w, v;

        if (x < -1.0 || x > 1.0) {
            r->sinc[i] = 0.0;
            continue;
        }
        w = cvt_i0(SINC_BETA * sqrt(1.0 - x * x)) / edge;

        if (t == 0.0) {
            v = cutoff;
        } else {
            double a = 3.14159265358979323846 * cutoff * t;

            v = cutoff * sin(a) / a;
        }
        r->sinc[i] = v * w;
    }
    return 1;
}

/* One coefficient, by a straight line between the two points either side. */
static double cvt_weight(const PcmResampler *r, double t)
{
    double at = (t + (double)r->half) * (double)SINC_PHASES;
    double top = (double)(2 * r->half * SINC_PHASES);
    int    i;
    double f;

    if (at <= 0.0 || at >= top)
        return 0.0;
    i = (int)at;
    f = at - (double)i;
    return r->sinc[i] + (r->sinc[i + 1] - r->sinc[i]) * f;
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
    if (method == CVT_SINC)
        return cvt_draw(r);
    return 1;
}

void pcm_resample_end(PcmResampler *r)
{
    free(r->sinc);
    r->sinc = 0;
    r->half = 0;
}

int32_t pcm_resample_delay(const PcmResampler *r)
{
    switch (r->method) {
    case CVT_LINEAR: return 1;
    case CVT_CUBIC:  return 2;
    case CVT_SINC:   return r->half;
    default:         return 0;
    }
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
static int32_t cvt_clamp(double v)
{
    if (v >= 32767.0)
        return 32767;
    if (v <= -32768.0)
        return -32768;
    return (int32_t)(v < 0 ? -(double)(long)(-v + 0.5) : (double)(long)(v + 0.5));
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
        double  f = (double)rem / (double)r->to;

        switch (r->method) {
        case CVT_ZEROS:
            out[made] = (i == last_i) ? 0 : cvt_tap(r, src, n, i);
            break;

        case CVT_LINEAR: {
            double a = (double)cvt_tap(r, src, n, i - 1);
            double b = (double)cvt_tap(r, src, n, i);

            out[made] = cvt_clamp(a + (b - a) * f);
            break;
        }

        case CVT_CUBIC: {
            /* Catmull-Rom through four, curving between the middle two. */
            double p0 = (double)cvt_tap(r, src, n, i - 3);
            double p1 = (double)cvt_tap(r, src, n, i - 2);
            double p2 = (double)cvt_tap(r, src, n, i - 1);
            double p3 = (double)cvt_tap(r, src, n, i);

            out[made] = cvt_clamp(
                p1 + 0.5 * f * ((p2 - p0)
                    + f * ((2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3)
                        + f * (3.0 * (p1 - p2) + p3 - p0))));
            break;
        }

        case CVT_SINC: {
            /* The window sits on the position, and the position is behind
               the newest sample by half the window, so every sample it
               reaches has already been handed over. */
            double acc = 0.0, weight = 0.0;
            int32_t j;

            for (j = -2 * r->half + 1; j <= 0; j++) {
                double w = cvt_weight(r, (double)(j + r->half) - f);

                acc += w * (double)cvt_tap(r, src, n, i + j);
                weight += w;
            }
            /* Divided by what the weights actually came to rather than
               trusting them to come to one.

               They do come to one, near enough: over every fraction of a
               sample the worst departure is two parts in a hundred thousand,
               which is three quarters of a count on a full-scale sample, so
               taking this division out changes nothing anything here can
               measure. It stays because it makes a level in a level out a
               property of the arithmetic rather than of the particular
               window and cutoff above it, and those are numbers somebody
               will want to change. */
            out[made] = cvt_clamp(weight != 0.0 ? acc / weight : 0.0);
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

    if (c->walk.from <= 0 || to < c->walk.from)
        return -1;

    {
        /* A second format on the same converter, so whatever the first one
           drew is given back before the next is. */
        int32_t from = c->walk.from;

        pcm_resample_end(&c->walk);
        return pcm_resample_start(&c->walk, from, to, cvt_method()) ? 0 : -1;
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
