/* Driving the Klatt synthesiser: where the samples are sent, and when the
   marks buried in them come back.

   The Delta engine hands phonemes down and gets samples back. Between the
   two sits a small record saying where those samples are to go -- a callback
   the caller supplied, or a file -- and what to do about the index marks the
   caller put in the text. A mark cannot be reported when it is read, because
   at that moment the sound it belongs to has not been made yet, let alone
   played. So marks are held in a queue with the number of samples that must
   come out before each is due, and released as the sound goes past them.

   That is what the three running totals in the language record are for. One
   is how far the marks have been accounted for, one is how far the phonemes
   have got, and one is the duration of everything queued. A mark arriving
   while all three agree is due immediately; otherwise it is queued at the
   distance between them.

   These carry their own names rather than aliases: the object uses plain C
   names for all of them, so ours are the same names and the swap stands the
   original's aside on that alone. */

#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "eci_synththread.h"

/* The engine's own handle. Only the one field this file needs is named. */
typedef struct DeltaThis DeltaThis;
typedef struct DeltaLang DeltaLang;
typedef struct SynthDevice SynthDevice;

#define DT_LANG(d)      (*(DeltaLang **)((char *)(d) + 0x70))

/* Where the sound is going, and what is to be reported about it. */
#define SD_SAMPLE_CB(v)   (*(void **)((char *)(v) + 0x00))
#define SD_SAMPLE_DATA(v) (*(void **)((char *)(v) + 0x04))
#define SD_DUR_CB(v)      (*(void **)((char *)(v) + 0x08))
#define SD_DUR_DATA(v)    (*(void **)((char *)(v) + 0x0c))
#define SD_FILENAME(v)    (*(char **)((char *)(v) + 0x10))
#define SD_QUEUE(v)       ((void *)((char *)(v) + 0x14))
#define SD_PLAYING(v)     (*(int32_t *)((char *)(v) + 0x28))
#define SD_INTERRUPTED(v) (*(int32_t *)((char *)(v) + 0x30))
#define SD_LAZY_WRITE(v)  (*(int32_t *)((char *)(v) + 0x34))
#define SD_UNKNOWN_2C(v)  (*(int32_t *)((char *)(v) + 0x2c))
#define SD_SLEEPCYCLE(v)  (*(int32_t *)((char *)(v) + 0x24))
#define SD_LAST_CLOCK(v)  (*(int32_t *)((char *)(v) + 0x3c))
#define SD_UNKNOWN_38(v)  (*(int32_t *)((char *)(v) + 0x38))
#define SD_INDEX_CB(v)    (*(void **)((char *)(v) + 0x44))
#define SD_INDEX_DATA(v)  (*(void **)((char *)(v) + 0x48))
#define SD_PHONEME_CB(v)  (*(void **)((char *)(v) + 0x4c))
#define SD_PHONEME_DATA(v) (*(void **)((char *)(v) + 0x50))

/* The language record. Its three running totals are the whole of the mark
   timing, and the rest is memory it owns. */
#define DL_DEVICE(l)      (*(SynthDevice **)((char *)(l) + 0x10))
#define DL_BUF_100(l)     (*(void **)((char *)(l) + 0x14))
#define DL_BUF_140(l)     (*(void **)((char *)(l) + 0x1c))
#define DL_EXTENSION(l)   (*(const char **)((char *)(l) + 0x08))
#define DL_VOICE_FILE(l)  (*(const char **)((char *)(l) + 0x0c))
#define DL_FLAG_18(l)     (*(int32_t *)((char *)(l) + 0x18))
#define DL_KLATT(l)       (*(void **)((char *)(l) + 0x20))
#define DL_BYTE_3C(l)     (*(int8_t *)((char *)(l) + 0x3c))
#define DL_BUF_4(l)       (*(void **)((char *)(l) + 0x30))
#define DL_SPOKEN(l)      (*(int32_t *)((char *)(l) + 0x44))
#define DL_MARKED(l)      (*(int32_t *)((char *)(l) + 0x48))
#define DL_QUEUED(l)      (*(int32_t *)((char *)(l) + 0x4c))
#define DL_RATE(l)        (*(int32_t *)((char *)(l) + 0x50))
#define DL_BYTES          0x54

#define BUF_100_BYTES     0x100
#define BUF_140_BYTES     0x140
#define BUF_4_BYTES       0x004

/* Durations are counted in milliseconds against a rate in hertz. */
#define MS_PER_SECOND     1000

typedef void (*IndexCallback)(int32_t index, void *data);
typedef int (*PhonemeCallback)(int32_t a, int32_t b, void *data);
typedef int (THIS *IsEmptyFn)(void *self);

extern void setInterrupt(DeltaThis *d, int32_t on) MANGLED("_setInterrupt");
extern void klatt_delete(void *k) MANGLED("_klatt_delete");
extern void KlattClose(void *k) MANGLED("_KlattClose");
extern int32_t deleteSleepCycle(int32_t h) MANGLED("_deleteSleepCycle");
extern long clock(void);
extern void stmarray_delete(DeltaThis *d) MANGLED("_stmarray_delete");
extern void deltaHeapCleanup(DeltaThis *d) MANGLED("_deltaHeapCleanup");
extern void dlangCleanup(DeltaThis *d) MANGLED("_dlangCleanup");
extern void vnstackCleanup(DeltaThis *d) MANGLED("_vnstackCleanup");
extern void vdelCleanup(DeltaThis *d) MANGLED("_vdelCleanup");
extern void logicalIOCleanup(DeltaThis *d) MANGLED("_logicalIOCleanup");
extern char *dupstr(const char *s) MANGLED("__strdup");

extern THIS void eListReset(void *self) MANGLED("?reset@EList@@QAEXXZ");
extern THIS int indexQueueRemove(void *self)
    MANGLED("?remove@IndexQueue@@QAEHXZ");
extern THIS int indexQueueAddOffsetFromLast(void *self, int32_t index,
                                            uint32_t offset)
    MANGLED("?addOffsetFromLast@IndexQueue@@QAEHHK@Z");

int synthDevicePlaying(DeltaThis *d);
int setSynthToNamedFile(DeltaThis *d, const char *name);
int insertSynthIndex(DeltaThis *d, int32_t index);
int insertDelayedSynthIndex(DeltaThis *d, int32_t index);

/* How long after the last sound the device is kept open, in the units the
   clock counts in. */
#define IDLE_HOLD  2000

/* ---- finishing, and letting the device go --------------------------- */

/* The synthesiser is closed, the device marked idle, and the moment noted so
   that the idle timer below has something to measure from. */
void finishSynthesis(DeltaThis *d)
{
    DeltaLang *lang = DT_LANG(d);
    SynthDevice *dev = DL_DEVICE(lang);

    KlattClose(DL_KLATT(lang));
    SD_UNKNOWN_2C(dev) = 0;
    SD_LAST_CLOCK(dev) = clock();
    SD_PLAYING(dev) = 0;
}

/* Whether the device should be kept open a moment longer. It is, while
   something is playing, and for a couple of seconds after the last thing
   stopped, so that a run of short utterances does not open and close it
   between every one. Asked to let go, it lets go.

   The original also has a branch that would note the time here, unreachable
   because the flag it depends on is set and never cleared. */
int sleepCycleCallback(SynthDevice *dev, int32_t letGo)
{
    if (letGo)
        return 0;
    if (SD_PLAYING(dev))
        return 1;
    if (SD_LAST_CLOCK(dev) + IDLE_HOLD >= clock())
        return 1;
    return 0;
}

void deleteOutputDevice(DeltaThis *d)
{
    SynthDevice *dev = DL_DEVICE(DT_LANG(d));

    if (SD_SLEEPCYCLE(dev) != -1)
        SD_SLEEPCYCLE(dev) = deleteSleepCycle(SD_SLEEPCYCLE(dev));
    sleepCycleCallback(dev, 1);
}

/* ---- what the device is doing --------------------------------------- */

int synthDevicePlaying(DeltaThis *d)
{
    return SD_PLAYING(DL_DEVICE(DT_LANG(d)));
}

/* Holding the device was published and does nothing; it always succeeds. */
int holdSynthDevice(DeltaThis *d, int32_t on)
{
    (void)d;
    (void)on;
    return 1;
}

/* Stop. If something is already in flight the interrupt flag is all that can
   be done and the sound thread notices it; otherwise, if the device is
   playing, finish here and now. */
int stopSynthesizing(DeltaThis *d)
{
    SynthDevice *dev = DL_DEVICE(DT_LANG(d));

    if (SD_INTERRUPTED(dev)) {
        setInterrupt(d, 1);
        return 1;
    }
    if (SD_PLAYING(dev)) {
        SD_UNKNOWN_38(dev) = 0;
        finishSynthesis(d);
        return 1;
    }
    return 0;
}

int turnLazyWriteOn(DeltaThis *d)
{
    SD_LAZY_WRITE(DL_DEVICE(DT_LANG(d))) = 1;
    return 0;
}

int turnLazyWriteOff(DeltaThis *d)
{
    SD_LAZY_WRITE(DL_DEVICE(DT_LANG(d))) = 0;
    return 0;
}

/* ---- where the sound goes ------------------------------------------- */

/* Send it to a file of this name, or with no name, stop sending it to one.
   A device that is playing will not be redirected. */
int setSynthToNamedFile(DeltaThis *d, const char *name)
{
    SynthDevice *dev;

    if (synthDevicePlaying(d))
        return 0;

    dev = DL_DEVICE(DT_LANG(d));
    if (SD_SAMPLE_CB(dev))
        SD_SAMPLE_CB(dev) = 0;

    if (name && name[0]) {
        SD_FILENAME(dev) = dupstr(name);
        if (!SD_FILENAME(dev))
            return 0;
        return 1;
    }

    if (SD_FILENAME(dev)) {
        free(SD_FILENAME(dev));
        SD_FILENAME(dev) = 0;
    }
    return 1;
}

/* Send it to the caller instead, which cancels any file. */
int setSynthToCallback(DeltaThis *d, void *cb, void *data)
{
    SynthDevice *dev;

    if (synthDevicePlaying(d))
        return 0;

    dev = DL_DEVICE(DT_LANG(d));
    if (SD_FILENAME(dev))
        setSynthToNamedFile(d, 0);

    SD_SAMPLE_CB(dev) = cb;
    SD_SAMPLE_DATA(dev) = data;
    return 1;
}

void setSynthDurationCallback(DeltaThis *d, void *cb, void *data)
{
    SynthDevice *dev = DL_DEVICE(DT_LANG(d));

    SD_DUR_CB(dev) = cb;
    SD_DUR_DATA(dev) = data;
}

void registerSynthIndexCallback(DeltaThis *d, void *cb, void *data)
{
    SynthDevice *dev = DL_DEVICE(DT_LANG(d));

    SD_INDEX_CB(dev) = cb;
    SD_INDEX_DATA(dev) = data;
}

void registerPhonemeCallback(DeltaThis *d, void *cb, void *data)
{
    SynthDevice *dev = DL_DEVICE(DT_LANG(d));

    SD_PHONEME_CB(dev) = cb;
    SD_PHONEME_DATA(dev) = data;
}

/* ---- marks, and when they come due ---------------------------------- */

/* Report a mark now. Only worth doing when the samples are going to a
   callback, because that is the only case where the caller is listening. */
int insertSynthIndex(DeltaThis *d, int32_t index)
{
    SynthDevice *dev = DL_DEVICE(DT_LANG(d));

    if (!SD_SAMPLE_CB(dev))
        return 0;
    if (SD_INDEX_CB(dev))
        ((IndexCallback)SD_INDEX_CB(dev))(index, SD_INDEX_DATA(dev));
    return 1;
}

/* Or hold it until the sound it belongs to has been made.

   The three totals are brought forward to the furthest any of them has
   reached. If they all agree there is nothing outstanding and the mark is
   due at once. Otherwise it goes on the queue at the distance between where
   the marks have got to and where the sound has, turned from milliseconds
   into samples. */
int insertDelayedSynthIndex(DeltaThis *d, int32_t index)
{
    DeltaLang *lang = DT_LANG(d);
    int rc;

    if (DL_MARKED(lang) <= DL_SPOKEN(lang))
        DL_MARKED(lang) = DL_SPOKEN(lang);
    if (DL_QUEUED(lang) <= DL_MARKED(lang))
        DL_QUEUED(lang) = DL_MARKED(lang);

    if (DL_MARKED(lang) == DL_QUEUED(lang)
        && DL_MARKED(lang) == DL_SPOKEN(lang))
        return insertSynthIndex(d, index);

    rc = indexQueueAddOffsetFromLast(
             SD_QUEUE(DL_DEVICE(lang)), index,
             (DL_QUEUED(lang) - DL_MARKED(lang)) * DL_RATE(lang)
                 / MS_PER_SECOND);
    DL_MARKED(lang) = DL_QUEUED(lang);
    return rc;
}

/* A phoneme on its way down. If the caller wanted to be told about phonemes
   it is told; otherwise this is the moment a mark can be timed against. */
int insertPhoneme(DeltaThis *d, int32_t a, int32_t b)
{
    SynthDevice *dev = DL_DEVICE(DT_LANG(d));

    if (SD_PHONEME_CB(dev)) {
        ((PhonemeCallback)SD_PHONEME_CB(dev))(a, b, SD_PHONEME_DATA(dev));
        return 1;
    }
    return insertDelayedSynthIndex(d, a);
}

void resetDelayedSynthQueue(DeltaThis *d)
{
    void *q = SD_QUEUE(DL_DEVICE(DT_LANG(d)));

    *(int32_t *)((char *)q + 0x0c) = 0;
    eListReset(q);
}

/* Let every held mark go at once. */
int flushDelayedSynthQueue(DeltaThis *d)
{
    for (;;) {
        void *q = SD_QUEUE(DL_DEVICE(DT_LANG(d)));
        IsEmptyFn isEmpty = (IsEmptyFn)(*(void ***)q)[0];

        if (isEmpty(q))
            return 1;
        if (!insertSynthIndex(d, indexQueueRemove(q)))
            return 0;
    }
}

/* ---- taking it all down --------------------------------------------- */

/* Give back everything the language record owns. The buffers are wiped
   before they are freed, which the original does throughout and which is
   worth keeping: a stale pointer read after this finds zeroes rather than
   something that still looks live. */
void dlang_delete(DeltaThis *d)
{
    DeltaLang *lang;

    if (!d || !DT_LANG(d))
        return;

    lang = DT_LANG(d);
    deleteOutputDevice(d);
    klatt_delete(DL_KLATT(lang));
    stmarray_delete(d);

    if (DL_DEVICE(lang)) {
        cpp_delete(DL_DEVICE(lang));
        DL_DEVICE(lang) = 0;
    }
    if (DL_BUF_140(lang)) {
        memset(DL_BUF_140(lang), 0, BUF_140_BYTES);
        free(DL_BUF_140(lang));
        DL_BUF_140(lang) = 0;
    }
    if (DL_BUF_100(lang)) {
        memset(DL_BUF_100(lang), 0, BUF_100_BYTES);
        free(DL_BUF_100(lang));
        DL_BUF_100(lang) = 0;
    }
    if (DL_BUF_4(lang)) {
        memset(DL_BUF_4(lang), 0, BUF_4_BYTES);
        free(DL_BUF_4(lang));
        DL_BUF_4(lang) = 0;
    }

    memset(lang, 0, DL_BYTES);
    free(lang);
    DT_LANG(d) = 0;
}

/* And the five other things an engine handle carries. */
void deltaCleanup(DeltaThis *d)
{
    deltaHeapCleanup(d);
    dlangCleanup(d);
    vnstackCleanup(d);
    vdelCleanup(d);
    logicalIOCleanup(d);
}


/* ---- building an engine's language half ----------------------------- */

/* One of the engine's value cells. Only the two fields read here are
   named: a word at two and a long at four. */
typedef struct Cell {
    int16_t pad;
    int16_t w;
    int32_t l;
} Cell;

/* The parameter frame the synthesiser works from, and the defaults every
   frame starts at: a hundred hertz, the eight formants at five hundred,
   fifteen hundred, twenty-five hundred and so on, and everything not named
   at nought. Lifted out of the original, which builds this on the stack one
   store at a time. */
#define FRAME_WORDS   62
#define FRAME_END     63

static const int32_t DEFAULT_FRAME[FRAME_WORDS] = {
        5,  1000,    60,    50,     0,     0,     0,     0,
        0,   500,    60,     0,     0,  1500,    90,  2500,
      150,  3250,   200,  3700,   200,  5000,   500,  6300,
      500,  7500,   600,   280,    90,   280,    90,   250,
       90,   250,    90,     0,     0,     0,     0,     0,
        0,     0,     0,     0,    80,   200,   350,  1000,
      800,  1000,  1500,  1500,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0
};

/* The block of default parameters every language record starts from. Read
   out of the original's data, where it is written once and never again. */
#define LAST_GLOB_WORDS  (BUF_140_BYTES / 4)

static const int32_t last_glob[LAST_GLOB_WORDS] = { 1, 0, 0, 5, 8, 1 };

extern void *cpp_new_bytes(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern THIS void *soundDeviceInfoCtor(void *self)
    MANGLED("??0SoundDeviceInfo@@QAE@XZ");
extern int stmarray_new(DeltaThis *d) MANGLED("_stmarray_new");
extern void *klatt_new(DeltaThis *d) MANGLED("_klatt_new");
extern int synthesize(DeltaThis *d, void *buf, int32_t a, int32_t b,
                      int32_t c, int32_t d1, int32_t d2, int32_t d3,
                      int32_t d4, int32_t d5, int32_t d6, int32_t d7,
                      int32_t d8, int32_t d9, int32_t d10, int32_t d11,
                      int32_t d12, const int32_t *frame)
    MANGLED("_synthesize");

/* What the engine answers when it has run out of memory. */
#define DELTA_NO_ROOM  (-2)

/* Build the language half of an engine handle: a record, two working
   buffers, a device, a word of scratch, the statement array and the
   synthesiser itself. Each step that fails gives back everything the steps
   before it took, which is why this reads as a staircase. */
int dlang_new(DeltaThis *d)
{
    DeltaLang *lang;
    void *dev;

    DT_LANG(d) = malloc(DL_BYTES);
    if (!DT_LANG(d))
        return DELTA_NO_ROOM;
    lang = DT_LANG(d);
    memset(lang, 0, DL_BYTES);

    DL_BUF_100(lang) = malloc(BUF_100_BYTES);
    if (!DL_BUF_100(lang)) {
        free(lang);
        DT_LANG(d) = 0;
        return DELTA_NO_ROOM;
    }
    memset(DL_BUF_100(lang), 0, BUF_100_BYTES);

    dev = cpp_new_bytes(0x54);
    DL_DEVICE(lang) = dev ? soundDeviceInfoCtor(dev) : 0;
    if (!DL_DEVICE(lang)) {
        free(DL_BUF_100(lang));
        DL_BUF_100(lang) = 0;
        free(lang);
        DT_LANG(d) = 0;
        return DELTA_NO_ROOM;
    }

    DL_EXTENSION(lang) = "wav";
    DL_VOICE_FILE(lang) = "audio.cdv";
    DL_FLAG_18(lang) = 1;

    DL_BUF_140(lang) = malloc(BUF_140_BYTES);
    if (!DL_BUF_140(lang)) {
        free(DL_BUF_100(lang));
        DL_BUF_100(lang) = 0;
        cpp_delete(DL_DEVICE(lang));
        DL_DEVICE(lang) = 0;
        free(lang);
        DT_LANG(d) = 0;
        return DELTA_NO_ROOM;
    }
    memcpy(DL_BUF_140(lang), last_glob, BUF_140_BYTES);

    DL_BUF_4(lang) = malloc(BUF_4_BYTES);
    if (!DL_BUF_4(lang)) {
        free(DL_BUF_100(lang));
        DL_BUF_100(lang) = 0;
        cpp_delete(DL_DEVICE(lang));
        DL_DEVICE(lang) = 0;
        free(DL_BUF_140(lang));
        DL_BUF_140(lang) = 0;
        free(lang);
        DT_LANG(d) = 0;
        return DELTA_NO_ROOM;
    }
    memset(DL_BUF_4(lang), 0, BUF_4_BYTES);
    DL_BYTE_3C(lang) = -1;

    if (stmarray_new(d)) {
        free(DL_BUF_100(lang));
        DL_BUF_100(lang) = 0;
        cpp_delete(DL_DEVICE(lang));
        DL_DEVICE(lang) = 0;
        free(DL_BUF_140(lang));
        DL_BUF_140(lang) = 0;
        free(DL_BUF_4(lang));
        DL_BUF_4(lang) = 0;
        free(lang);
        DT_LANG(d) = 0;
        return DELTA_NO_ROOM;
    }

    DL_KLATT(lang) = klatt_new(d);
    return 0;
}

/* ---- one utterance, from a frame of parameters ---------------------- */

/* Speak from a parameter frame.

   Thirteen cells carry the fixed arguments. After them comes a run of pairs,
   an index and a value, ending at an index of nought; each pair overrides one
   word of the frame. The indices the caller gives are counted from one.

   The frame starts at the defaults, is then overwritten by the assignment
   table, and only then by the caller's pairs. */
int callSynthesizeArray(DeltaThis *d, Cell *rate, Cell *c2, Cell *c3,
                        Cell *c4, Cell *c5, Cell *c6, Cell *c7, Cell *c8,
                        Cell *c9, Cell *c10, Cell *c11, Cell *c12, Cell *c13,
                        ...)
{
    int32_t frame[FRAME_WORDS];
    int32_t v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13;
    va_list ap;
    void *buf;
    int32_t n;
    int ok;
    int i;

    (void)rate;

    buf = cpp_new_bytes(0x0d);
    if (!buf)
        return 1;

    v2 = c2->l;
    v3 = c3->l;
    v4 = c4->l;
    v5 = c5->l;
    v6 = c6->w;
    v7 = c7->w;
    v8 = c8->w;
    v9 = c9->w;
    v10 = c10->w;
    v11 = c11->w;
    v12 = c12->w;
    v13 = c13->w;

    for (i = 0; i < FRAME_WORDS; i++)
        frame[i] = 0;
    for (i = 0; i < FRAME_WORDS; i++)
        frame[i] = DEFAULT_FRAME[i];

    va_start(ap, c13);
    n = va_arg(ap, Cell *)->w;
    while (n != 0) {
        n--;
        if (n < 0) {
            va_end(ap);
            cpp_delete(buf);
            return 1;
        }
        frame[n] = va_arg(ap, Cell *)->w;
        n = va_arg(ap, Cell *)->w;
    }
    va_end(ap);

    ok = synthesize(d, buf, 1, 0, 0, v2, v3, v4, v5, v6, v7, v8, v9, v10,
                    v11, v12, v13, frame) ? 0 : 1;
    cpp_delete(buf);
    return ok;
}

ALIAS("?finishSynthesis@@YAXPAUDelta_This_Struct@@@Z", "finishSynthesis");
ALIAS("?deleteOutputDevice@@YAXPAUDelta_This_Struct@@@Z",
      "deleteOutputDevice");
ALIAS("?sleepCycleCallback@@YAHPAUSoundDeviceInfo@@H@Z",
      "sleepCycleCallback");
