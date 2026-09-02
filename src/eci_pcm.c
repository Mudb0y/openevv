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
 *           quick mode and is the default here. It suppresses the mirror far
 *           enough that the result is a rate change rather than an effect,
 *           and it is short of what a windowed sinc would do, which is why
 *           it stays lively rather than sounding filtered.
 *
 * Cubic rather than libsoxr itself because the engine has no dependency but
 * the C library and gains none here: this ships inside a DLL a screen reader
 * loads and inside builds for platforms nobody has put soxr on. It is the
 * same interpolation soxr's quick mode is documented as, not the same code,
 * so it is an equivalent and not a match to the byte.
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

void pcm_resample_start(PcmResampler *r, int32_t from, int32_t to,
                        int32_t method)
{
    memset(r, 0, sizeof *r);
    r->from = from;
    r->to = to;
    r->method = method;
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
    static int method = CVT_CUBIC;

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
            else
                method = CVT_CUBIC;
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

    pcm_resample_start(&c->walk, c->walk.from, to, cvt_method());
    return 0;
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
