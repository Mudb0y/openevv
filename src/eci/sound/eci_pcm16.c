/* Sound written out as plain sixteen-bit samples.
 *
 * One of the two formats the engine registers, and the simple one: no
 * header, no framing, just the samples one after another in the order the
 * synthesiser produced them. The other format writes a device's own kind of
 * file; this one writes what a caller asked for raw.
 *
 * A format is a block of function pointers and a file is a block the format
 * keeps its own working state in. The format only accepts three sample
 * rates and only sixteen bits, and says so rather than converting.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "evv_abi.h"
#include "evv_arena.h"

/* A sound file, as far as a format is concerned. */
#define SF_MODE(f)    (*(int32_t *)((char *)(f) + 0x04))
#define SF_FORMAT(f)  (*(void **)((char *)(f) + 0x0c))
#define SF_RATE(f)    (*(int32_t *)((char *)(f) + 0x10))
#define SF_BITS(f)    (*(int32_t *)((char *)(f) + 0x14))
#define SF_STREAM(f)  (*(FILE **)((char *)(f) + 0x18))
#define SF_HOW(f)     (*(int32_t *)((char *)(f) + 0x1c))
#define SF_PRIVATE(f) (*(Pcm **)((char *)(f) + 0x28))

/* What a format declares about itself, which is where a file with nothing
   said about it takes its rate and width from. */
#define FMT_RATE(m)   (*(const int32_t *)((const char *)(m) + 0x08))
#define FMT_BITS(m)   (*(const int32_t *)((const char *)(m) + 0x0c))

/* What this format keeps for one file. */
typedef struct {
    int32_t  total;   /* +0x00, the furthest the file has ever reached */
    int32_t  rate;    /* +0x04 */
    uint16_t bits;    /* +0x08 */
    uint16_t pad_0a;
    int32_t  at;      /* +0x0c, where it is now, in samples */
    int32_t  least;   /* +0x10, the two ends of one sample's range */
    int32_t  most;    /* +0x14 */
} Pcm;

/* What a file is being used for. Nothing said yet counts as writing. */
#define MODE_UNSAID 0
#define MODE_WRITE  2

/* How a file was opened: begun afresh, or added to. */
#define HOW_FRESH 2
#define HOW_ADD   3

/* The only three rates and the only width this format will take. */
#define RATE_22050 0x5622
#define RATE_11025 0x2b11
#define RATE_8000  0x1f40
#define BITS_16    16

/* One sample as the engine hands it over, and as it goes out. */
#define SAMPLE_STRIDE 4
#define SAMPLE_MOST   0x7fff

extern void    defaultSetDuration(void);
extern int32_t defaultHold(void);
extern int32_t defaultReset(void);
extern int32_t defaultSamplesUncommitted(void);
extern void    defaultSetIndexCallback(void *file, void *fn, void *param);
extern int32_t defaultInsertIndex(void *file, int32_t index);
extern int32_t defaultPoll(void);

static void sampleFormat(void *file, int32_t *out);
static int32_t initFile(void *file);

/* Two bytes, least significant first, whatever this machine would have
   done with a short. The original writes the short itself and so only ever
   worked on a machine that agreed with the name. */
int32_t writeLittleEnd16(int16_t v, FILE *f)
{
    unsigned char b[2];

    b[0] = (unsigned char)((uint16_t)v & 0xff);
    b[1] = (unsigned char)(((uint16_t)v >> 8) & 0xff);
    return fwrite(b, 2, 1, f) == 1;
}

int32_t getFileLength(FILE *f)
{
    fseek(f, 0, SEEK_END);
    return (int32_t)ftell(f);
}

/* Whether this format can take the file as it stands, filling in whatever
   was left unsaid from what the format declares. */
static int32_t checkFormat(void *file)
{
    if (file == 0)
        return 0;

    if (SF_RATE(file) == 0)
        SF_RATE(file) = FMT_RATE(SF_FORMAT(file));
    if (SF_BITS(file) == 0)
        SF_BITS(file) = FMT_BITS(SF_FORMAT(file));

    switch (SF_MODE(file)) {
    case MODE_UNSAID:
        SF_MODE(file) = MODE_WRITE;
        /* and on into the writing case, as the original does */
    case MODE_WRITE:
        if (SF_RATE(file) != RATE_22050 && SF_RATE(file) != RATE_11025
         && SF_RATE(file) != RATE_8000)
            return 0;
        if (SF_BITS(file) != BITS_16)
            return 0;
        return 1;
    default:
        return 0;
    }
}

/* Start a file: throw away anything left from a previous one and take a
   fresh block of working state. */
static int32_t initFile(void *file)
{
    Pcm *p;

    if (file == 0)
        return 0;
    if (SF_RATE(file) != RATE_22050 && SF_RATE(file) != RATE_11025
     && SF_RATE(file) != RATE_8000)
        return 0;
    if (SF_BITS(file) != BITS_16)
        return 0;

    if (SF_PRIVATE(file) != 0)
        free(SF_PRIVATE(file));

    SF_PRIVATE(file) = malloc(sizeof(Pcm));
    p = SF_PRIVATE(file);
    if (p == 0)
        return 0;

    p->rate  = SF_RATE(file);
    p->bits  = (uint16_t)SF_BITS(file);
    p->total = 0;
    p->at    = 0;
    sampleFormat(file, &p->least);
    return 1;
}

/* Take a file on, either fresh or as it already stands. A file being
   written from the start is simply begun; one that is being added to has
   its length read back and turned into a sample count, and whatever it did
   not say about itself is taken from the format.

   The original builds the working state on its stack and copies the block
   over afterwards, of which only the first three fields have been set; the
   other three are written immediately below, so they are set here instead
   of copying what happened to be there. */
static int32_t checkFile(void *format, void *file)
{
    Pcm    *p;
    int32_t total;
    int32_t rate;
    int16_t bits;

    if (file == 0 || format == 0 || SF_MODE(file) != MODE_WRITE)
        return 0;
    if (SF_BITS(file) != BITS_16 && SF_BITS(file) != 0)
        return 0;
    if (SF_RATE(file) != RATE_22050 && SF_RATE(file) != RATE_11025
     && SF_RATE(file) != RATE_8000 && SF_RATE(file) != 0)
        return 0;
    if (SF_STREAM(file) == 0 || SF_HOW(file) == 0)
        return 0;

    if (SF_HOW(file) == HOW_FRESH
     || (SF_HOW(file) == HOW_ADD && getFileLength(SF_STREAM(file)) == 0)) {
        initFile(file);
        return 1;
    }

    total = getFileLength(SF_STREAM(file)) / 2;
    rate  = SF_RATE(file) ? SF_RATE(file) : FMT_RATE(format);
    bits  = SF_BITS(file) ? (int16_t)SF_BITS(file) : (int16_t)FMT_BITS(format);

    SF_FORMAT(file) = format;
    initFile(file);

    p = SF_PRIVATE(file);
    p->total = total;
    p->rate  = rate;
    p->bits  = (uint16_t)bits;
    p->at    = 0;
    sampleFormat(file, &p->least);
    return 1;
}

/* Give the working state back. The original reads through a file that is
   not there rather than answering; this answers. */
static int32_t endFile(void *file)
{
    if (file == 0)
        return 0;

    free(SF_PRIVATE(file));
    SF_PRIVATE(file) = 0;
    return 1;
}

/* What one sample may be, which for this format is the whole of a signed
   sixteen-bit range read as a magnitude. Says nothing when the file is not
   one it can write. */
static void sampleFormat(void *file, int32_t *out)
{
    if (file == 0 || out == 0)
        return;
    if (SF_FORMAT(file) == 0 || SF_PRIVATE(file) == 0)
        return;
    if (SF_BITS(file) != BITS_16)
        return;

    out[0] = 0;
    out[1] = SAMPLE_MOST;
}

/* Samples come in four bytes apiece and go out in two: the engine works
   wider than it writes. */
static int32_t addSamples(void *file, const int32_t *format,
                          const int16_t *samples, uint32_t count)
{
    Pcm *p;

    if (file == 0 || format == 0 || samples == 0)
        return 0;
    if (SF_STREAM(file) == 0 || SF_PRIVATE(file) == 0)
        return 0;

    p = SF_PRIVATE(file);
    p->at += (int32_t)count;
    if ((uint32_t)p->at > (uint32_t)p->total)
        p->total = p->at;

    switch (SF_MODE(file)) {
    case MODE_UNSAID:
    case MODE_WRITE:
        if (p->bits != BITS_16)
            return 1;
        if (format[0] != p->least || format[1] != p->most)
            return 0;

        while (count != 0) {
            int16_t v = *samples;

            if (!writeLittleEnd16(v, SF_STREAM(file)))
                return 0;
            samples = (const int16_t *)((const char *)samples
                                        + SAMPLE_STRIDE);
            count--;
        }
        return 1;

    default:
        return 0;
    }
}

/* Nothing is ever read back out of one of these. */
static int32_t getSamples(void *file, const int32_t *format,
                          int16_t *samples, uint32_t count)
{
    (void)file; (void)format; (void)samples; (void)count;
    return 0;
}

/* Everything written is already written; this only puts the file back to
   its beginning. */
static int32_t flush(void *file)
{
    if (file == 0)
        return 0;
    if (SF_STREAM(file) == 0)
        return 0;

    SF_PRIVATE(file)->at = 0;
    return 1;
}

static int32_t setPosition(void *file, int32_t at)
{
    Pcm *p;

    if (file == 0 || SF_STREAM(file) == 0 || SF_PRIVATE(file) == 0)
        return -1;

    p = SF_PRIVATE(file);
    if (at > p->total)
        at = p->total;

    fseek(SF_STREAM(file), (p->bits >> 3) * at, SEEK_SET);
    p->at = at;
    return at;
}

static int32_t getDuration(void *file)
{
    if (file == 0 || SF_PRIVATE(file) == 0)
        return -1;
    return SF_PRIVATE(file)->total;
}

/* The format itself. Seventeen slots, of which this fills nine and leaves
   the rest to the fallbacks every format shares. */
void *PCM16MonoSoundFormat[] = {
    (void *)"pcm",
    (void *)1,
    (void *)RATE_11025,
    (void *)BITS_16,
    (void *)checkFormat,
    (void *)checkFile,
    (void *)endFile,
    (void *)sampleFormat,
    (void *)addSamples,
    (void *)getSamples,
    (void *)flush,
    (void *)setPosition,
    (void *)defaultSetDuration,
    (void *)getDuration,
    (void *)defaultHold,
    (void *)defaultReset,
    (void *)defaultSamplesUncommitted,
    (void *)defaultSetIndexCallback,
    (void *)defaultInsertIndex,
    (void *)defaultPoll
};
