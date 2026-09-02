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

/* Raising the rate by repeating samples, and deliberately not filtering.
 *
 * This is the one object of IBM's sound layer the port never transcribed,
 * and it is the reason a caller asking for twenty-two thousand used to be
 * handed the eleven thousand stream under a twenty-two thousand label -- the
 * same speech at half the duration. It is ours rather than a transcription,
 * for the reason the head of this file gives about the rest of the layer.
 *
 * What it does is the point rather than an approximation of something
 * better. The engine can be run at twenty-two thousand outright, and that
 * was tried first: it sounds wrong, because the frication and aspiration
 * come out of a noise generator that produces one value per output sample,
 * so their bandwidth is whatever the rate is and the sibilants go thin and
 * hissy. Repeating samples touches none of that. The speech is bit for bit
 * the speech Eloquence has always made, and what fills the new band is a
 * mirror of it rather than new noise.
 *
 * That mirror is what a bandlimited resampler exists to remove, and here it
 * is kept. Doubling by holding leaves the top of the original band three
 * decibels down and puts the mirrored copy between about four decibels below
 * the speech just above the old Nyquist and a null at the old sample rate.
 * It is the sound old hardware made, and it is what this was asked for.
 *
 * Three ways of filling the gap, chosen by EVV_UPSAMPLE, because which one
 * sounds best is a question for a listener and not for this file:
 *
 *   hold    repeat each sample, which is the default and the brief
 *   zeros   one sample then silence, which keeps the mirror at full strength
 *           with no droop at the top of the band, and is brighter and harder
 *           still -- and quieter, since only one sample in N carries anything
 *   linear  slide between one sample and the next, which suppresses the
 *           mirror by roughly twice what holding does and is where to go if
 *           holding turns out too bright
 *
 * Those three are about a whole-number ratio. Where the ratio is not whole
 * the nearest input sample is taken and the setting does not apply, because
 * the repeats are already uneven and there is no one gap to fill.
 *
 * A whole-number ratio repeats each sample the same number of times. Where
 * the ratio is not whole, each output sample takes the input sample nearest
 * it in time, which is the same idea carried on: no interpolation, no
 * filtering, and every value that comes out is a value the engine put in.
 * What it costs is that the repeats are uneven -- at forty-eight thousand
 * from eleven thousand and twenty five each sample lands four or five times
 * -- so the timing wobbles by up to half an input sample. The position is
 * carried between runs, so the unevenness never restarts at a boundary.
 */

#include <stdlib.h>
#include <string.h>

#define CVT_HOLD    0
#define CVT_ZEROS   1
#define CVT_LINEAR  2

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

typedef struct AudioConverter {
    int32_t   from;         /* the engine's rate */
    int32_t   to;           /* the caller's */
    int32_t   times;        /* to over from where that is whole, else nought */
    int32_t   at;           /* where the fractional walk has got to, in units
                               of one input sample over `to' */
    int32_t   method;
    int32_t   last;         /* the sample before this run, for linear */
    int       have_last;
    int32_t  *room;         /* what convertSamples answers with */
    uint32_t  samples;      /* how many it has room for */
    SDATA     out;
} AudioConverter;

const uint32_t pcm_cvt_bytes = sizeof(AudioConverter);

/* Read once, because it is an experiment knob and not a setting: an engine
   that changed its mind halfway through an utterance would be comparing two
   things at once. */
static int cvt_method(void)
{
    static int decided;
    static int method = CVT_HOLD;

    if (!decided) {
        const char *say = getenv("EVV_UPSAMPLE");

        decided = 1;
        if (say != 0) {
            if (strcmp(say, "zeros") == 0)
                method = CVT_ZEROS;
            else if (strcmp(say, "linear") == 0)
                method = CVT_LINEAR;
            else
                method = CVT_HOLD;
        }
    }
    return method;
}

THIS void *pcm_cvt_ctor(AudioConverter *c)
{
    memset(c, 0, sizeof *c);
    c->times = 1;
    c->method = cvt_method();
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
    c->from = (int32_t)((WaveFormat *)fmt)->rate;
    c->have_last = 0;
    return 0;
}

/* The ratio is settled here, and a fractional one is refused rather than
   rounded. */
THIS int32_t pcm_cvt_setDest(AudioConverter *c, void *fmt)
{
    int32_t to = (int32_t)((WaveFormat *)fmt)->rate;

    if (c->from <= 0 || to < c->from)
        return -1;

    c->to = to;
    c->times = (to % c->from) == 0 ? to / c->from : 0;
    c->at = 0;
    c->have_last = 0;
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
    uint32_t want = n * (uint32_t)c->times;
    uint32_t i;
    int32_t  j, times = c->times;
    int32_t *put;

    if (times == 0 && src != 0 && c->to > c->from) {
        /* The nearest input sample to each output one, walked with the
           position carried over from the run before. */
        uint32_t made = 0;
        int32_t  at = c->at;

        want = 0;
        while ((uint32_t)(at / c->to) < n) {
            want++;
            at += c->from;
        }
        if (!cvt_room(c, want))
            return -1;

        at = c->at;
        put = c->room;
        while ((uint32_t)(at / c->to) < n) {
            put[made++] = src[at / c->to];
            at += c->from;
        }
        c->at = at - (int32_t)n * c->to;

        if (n > 0) {
            c->last = src[n - 1];
            c->have_last = 1;
        }
        c->out.at = c->room;
        c->out.bytes = made << 2;
        *out = &c->out;
        return 0;
    }

    if (times <= 1 || src == 0) {
        /* Nothing to do, and saying so by handing back what came in keeps
           the caller's one code path. */
        c->out.at = (void *)src;
        c->out.bytes = in.bytes;
        *out = &c->out;
        return 0;
    }

    if (!cvt_room(c, want))
        return -1;

    put = c->room;

    switch (c->method) {
    case CVT_ZEROS:
        for (i = 0; i < n; i++) {
            *put++ = src[i];
            for (j = 1; j < times; j++)
                *put++ = 0;
        }
        break;

    case CVT_LINEAR:
        for (i = 0; i < n; i++) {
            int32_t was = (i == 0) ? (c->have_last ? c->last : src[0])
                                   : src[i - 1];

            /* Between the sample before and this one, so the run joins on to
               the one before it rather than stepping at every boundary. */
            for (j = 0; j < times; j++)
                *put++ = was + (int32_t)(((int64_t)(src[i] - was) * j)
                                         / times);
        }
        break;

    default:
        for (i = 0; i < n; i++)
            for (j = 0; j < times; j++)
                *put++ = src[i];
        break;
    }

    if (n > 0) {
        c->last = src[n - 1];
        c->have_last = 1;
    }

    c->out.at = c->room;
    c->out.bytes = want << 2;
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
