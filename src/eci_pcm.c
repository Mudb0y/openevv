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

/* Nothing here looks inside one; only its size matters, and that comes from
   what the original allocates for it. */
typedef struct AudioConverter { uint8_t opaque[0x64]; } AudioConverter;

const uint32_t pcm_cvt_bytes = sizeof(AudioConverter);

THIS void *pcm_cvt_ctor(AudioConverter *c)
{
    pcm_saw("AudioConverter ctor");
    return c;
}

THIS void pcm_cvt_dtor(AudioConverter *c)
{
    (void)c;
    pcm_saw("AudioConverter dtor");
}

THIS int32_t pcm_cvt_setSource(AudioConverter *c, void *fmt)
{
    (void)c;
    (void)fmt;
    pcm_saw("setSourceFormat");
    return 0;
}

THIS int32_t pcm_cvt_setDest(AudioConverter *c, void *fmt)
{
    (void)c;
    (void)fmt;
    pcm_saw("setDestFormat");
    return 0;
}

THIS int32_t pcm_cvt_convert(AudioConverter *c, void *in, void **out)
{
    (void)c;
    (void)in;
    (void)out;
    pcm_saw("convertSamples");
    return 0;
}

THIS void pcm_cvt_storeHistory(AudioConverter *c)
{
    (void)c;
    pcm_saw("storeHistory");
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
