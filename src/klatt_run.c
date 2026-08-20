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
#include "klatt_state.h"
#include "evv_abi.h"
#include "evv_arena.h"
#include "delta.h"
#include "klatt_lang.h"

/* The engine's own handle. Only the one field this file needs is named. */
typedef delta_state DeltaThis;

#define DT_LANG(d)      EVV_AT(DeltaLang *, (d)->dlang)

#define SD_SAMPLE_CB(v)   ((v)->sample_cb)
#define SD_SAMPLE_DATA(v) ((v)->sample_data)
#define SD_DUR_CB(v)      ((v)->dur_cb)
#define SD_DUR_DATA(v)    ((v)->dur_data)
#define SD_FILENAME(v)    ((v)->filename)
#define SD_QUEUE(v)       ((void *)&(v)->queue)
#define SD_PLAYING(v)     ((v)->playing)
#define SD_INTERRUPTED(v) ((v)->interrupted)
#define SD_LAZY_WRITE(v)  ((v)->lazy_write)
#define SD_UNKNOWN_2C(v)  ((v)->open)
#define SD_SLEEPCYCLE(v)  ((v)->sleepcycle)
#define SD_LAST_CLOCK(v)  ((v)->last_clock)
#define SD_UNKNOWN_38(v)  ((v)->pending)
#define SD_PENDING(v)     ((v)->pending)
#define SD_OPEN(v)        ((v)->open)
#define SD_HOLD(v)        ((v)->hold)
#define SD_INDEX_CB(v)    ((v)->index_cb)
#define SD_INDEX_DATA(v)  ((v)->index_data)
#define SD_PHONEME_CB(v)  ((v)->phoneme_cb)
#define SD_PHONEME_DATA(v) ((v)->phoneme_data)

#define DL_DEVICE(l)      ((l)->device)
#define DL_BUF_100(l)     ((l)->buf_100)
#define DL_BUF_140(l)     ((l)->buf_140)
#define DL_EXTENSION(l)   ((l)->extension)
#define DL_VOICE_FILE(l)  ((l)->voice_file)
#define DL_FLAG_18(l)     ((l)->flag_18)
#define DL_KLATT(l)       ((l)->klatt)
#define DL_BYTE_3C(l)     ((l)->stream)
#define DL_BUF_4(l)       ((l)->buf_4)
#define DL_SPOKEN(l)      ((l)->spoken)
#define DL_MARKED(l)      ((l)->marked)
#define DL_QUEUED(l)      ((l)->queued)
#define DL_RATE(l)        ((l)->rate)
#define DL_BYTES          sizeof(DeltaLang)

/* What the language record keeps of the last utterance: the synthesiser's
   constant parameters, the volume, and the frame. */
#define FRAME_WORDS   62

typedef struct LastGlob {
    KlattConstParms cp;
    int32_t         volume;
    int32_t         frame[FRAME_WORDS];
} LastGlob;

#define BUF_100_BYTES     0x100
#define BUF_140_BYTES     sizeof(LastGlob)
#define BUF_4_BYTES       0x004

/* Durations are counted in milliseconds against a rate in hertz. */
#define MS_PER_SECOND     1000

typedef void (*IndexCallback)(int32_t index, void *data);
typedef int (*PhonemeCallback)(int32_t a, int32_t b, void *data);
typedef int (THIS *IsEmptyFn)(void *self);

extern void setInterrupt(DeltaThis *d, int32_t on) MANGLED("_setInterrupt");
extern int32_t deleteSleepCycle(int32_t h) MANGLED("_deleteSleepCycle");
extern long clock(void);
extern void stmarray_delete(DeltaThis *d) MANGLED("_stmarray_delete");
extern void deltaHeapCleanup(DeltaThis *d) MANGLED("_deltaHeapCleanup");
extern void dlangCleanup(DeltaThis *d) MANGLED("_dlangCleanup");
extern void vnstackCleanup(DeltaThis *d) MANGLED("_vnstackCleanup");
extern void vdelCleanup(DeltaThis *d) MANGLED("_vdelCleanup");
extern void logicalIOCleanup(DeltaThis *d) MANGLED("_logicalIOCleanup");
extern char *dupstr(const char *s) MANGLED("__strdup");

extern THIS void el_listReset(void *self) MANGLED("?reset@EList@@QAEXXZ");
extern THIS int iq_remove(void *self)
    MANGLED("?remove@IndexQueue@@QAEHXZ");
extern THIS int iq_addOffsetFromLast(void *self, int32_t index,
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

    rc = iq_addOffsetFromLast(
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
    el_listReset(q);
}

/* Let every held mark go at once. */
int flushDelayedSynthQueue(DeltaThis *d)
{
    for (;;) {
        void *q = SD_QUEUE(DL_DEVICE(DT_LANG(d)));
        IsEmptyFn isEmpty = (IsEmptyFn)(*(void ***)q)[0];

        if (isEmpty(q))
            return 1;
        if (!insertSynthIndex(d, iq_remove(q)))
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
    d->dlang = EVV_REF(0);
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
/* What a language starts with. The original writes it as the six words it
   comes to, which is the same block only where a function pointer is four
   bytes; these are the fields those words were. */
static const LastGlob last_glob = {
    { 1, 0, 0, 5, 8, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    0, { 0 }
};

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
THIS void *soundDeviceInfoCtor(void *self);
extern THIS void *sti_indexQueueCtor(void *self)
    MANGLED("??0IndexQueue@@QAE@XZ");

extern int stmarray_new(DeltaThis *d) MANGLED("_stmarray_new");
int synthesize(DeltaThis *d, void *buf, int32_t isArray, int32_t *streamA,
               int32_t *streamB, int32_t from, int32_t to, int32_t more,
               int32_t a5, int32_t rate, int32_t a7, int32_t nFormants,
               int32_t addC, int32_t addA, int32_t addB, int32_t addD,
               int32_t volume, const int32_t *frame);

/* An error reporter that does nothing, which is what the engine installs. */
extern int errorIgnore(void) MANGLED("_errorIgnore");

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

    d->dlang = EVV_REF(malloc(DL_BYTES));
    if (!DT_LANG(d))
        return DELTA_NO_ROOM;
    lang = DT_LANG(d);
    memset(lang, 0, DL_BYTES);

    DL_BUF_100(lang) = malloc(BUF_100_BYTES);
    if (!DL_BUF_100(lang)) {
        free(lang);
        d->dlang = EVV_REF(0);
        return DELTA_NO_ROOM;
    }
    memset(DL_BUF_100(lang), 0, BUF_100_BYTES);

    dev = cpp_new(sizeof(SynthDevice));
    DL_DEVICE(lang) = dev ? soundDeviceInfoCtor(dev) : 0;
    if (!DL_DEVICE(lang)) {
        free(DL_BUF_100(lang));
        DL_BUF_100(lang) = 0;
        free(lang);
        d->dlang = EVV_REF(0);
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
        d->dlang = EVV_REF(0);
        return DELTA_NO_ROOM;
    }
    memcpy(DL_BUF_140(lang), &last_glob, BUF_140_BYTES);

    DL_BUF_4(lang) = malloc(BUF_4_BYTES);
    if (!DL_BUF_4(lang)) {
        free(DL_BUF_100(lang));
        DL_BUF_100(lang) = 0;
        cpp_delete(DL_DEVICE(lang));
        DL_DEVICE(lang) = 0;
        free(DL_BUF_140(lang));
        DL_BUF_140(lang) = 0;
        free(lang);
        d->dlang = EVV_REF(0);
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
        d->dlang = EVV_REF(0);
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

    buf = cpp_new(0x0d);
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

    /* Everything after the fixed arguments was pushed by a rule, so each one
       is a value and has to be read back as one: a pointer is wider than that
       where a pointer is wider than a value. */
    va_start(ap, c13);
    n = EVV_AT(Cell *, va_arg(ap, int32_t))->w;
    while (n != 0) {
        n--;
        if (n < 0) {
            va_end(ap);
            cpp_delete(buf);
            return 1;
        }
        frame[n] = EVV_AT(Cell *, va_arg(ap, int32_t))->w;
        n = EVV_AT(Cell *, va_arg(ap, int32_t))->w;
    }
    va_end(ap);

    ok = synthesize(d, buf, 1, 0, 0, v2, v3, v4, v5, v6, v7, v8, v9, v10,
                    v11, v12, v13, frame) ? 0 : 1;
    cpp_delete(buf);
    return ok;
}


/* ---- choosing which stream feeds which parameter -------------------- */

/* The names the engine knows each parameter by, in frame order. The last is
   not a parameter at all but the duration stream, which is why it is looked
   up differently below. */
#define PARM_NAMES_COUNT (FRAME_WORDS + 1)
#define PARM_MS          FRAME_WORDS

static const char *const parmNames[PARM_NAMES_COUNT] = {
    "ui",  "f0",  "av",  "oq",  "tl",  "fl",  "di",  "ah",
    "af",  "f1",  "b1",  "df1", "db1", "f2",  "b2",  "f3",
    "b3",  "f4",  "b4",  "f5",  "b5",  "f6",  "b6",  "f7",
    "b7",  "f8",  "b8",  "fnp", "bnp", "fnz", "bnz", "ftp",
    "btp", "ftz", "btz", "a1f", "a2f", "a3f", "a4f", "a5f",
    "a6f", "a7f", "a8f", "ab",  "b1f", "b2f", "b3f", "b4f",
    "b5f", "b6f", "b7f", "b8f", "anv", "a1v", "a2v", "a3v",
    "a4v", "a5v", "a6v", "a7v", "a8v", "atv", "ms",
};

/* Which stream each parameter is to be read from. Kept in the language
   record's own two hundred and fifty-six bytes. */
typedef struct ParmAssignments {
    int32_t kind;                       /* nought stream, one array */
    int32_t slot[PARM_NAMES_COUNT];
} ParmAssignments;

/* The value a slot holds when nothing supplies it. */
#define SLOT_NONE  (-10)

extern int arrayStreamFind(DeltaThis *d, const char *name)
    MANGLED("_arrayStreamFind");
extern int num_streams(DeltaThis *d) MANGLED("_num_streams");
extern const char *stream_name(int8_t n) MANGLED("_stream_name");
extern int ralStrIcmp(void *ctx, const char *a, const char *b)
    MANGLED("_ralStrIcmp");
extern int enum_field(int8_t n, int32_t which) MANGLED("_enum_field");
extern int time_stream(int8_t n) MANGLED("_time_stream");
extern int checkInterrupt(DeltaThis *d) MANGLED("_checkInterrupt");
extern int timeDuration(DeltaThis *d, void *a, void *b, int8_t which)
    MANGLED("_timeDuration");
extern int sendArrayParameters(DeltaThis *d, int32_t a, int32_t b, int32_t c,
                               int32_t e, int32_t f, int32_t g, int32_t h,
                               void *pa, const int32_t *frame)
    MANGLED("_sendArrayParameters");
extern int sendStreamParameters(DeltaThis *d, void *a, void *b, int32_t c,
                                int32_t e, int32_t f, int32_t g, int32_t h,
                                int32_t i, void *pa, const int32_t *frame)
    MANGLED("_sendStreamParameters");
extern THIS uint32_t iq_reduceLeadTime(void *self, uint32_t n)
    MANGLED("?reduceLeadTime@IndexQueue@@QAEKK@Z");
extern THIS int sti_indexDue(void *self)
    MANGLED("?indexDue@IndexQueue@@QBEHXZ");

/* Work out where every parameter's values are coming from.

   For an array the answer is a stream index found by name, and any one of
   them being found is enough. For a stream the search is by name over the
   streams the engine has, and the duration parameter at the end is matched
   against the stream that carries time rather than a value.

   The original never clears the duration slot before that second search, so
   what it compares against at the end is whatever was there. Kept as it is,
   because a differential port is the point. */
int assignParameters(DeltaThis *d, int32_t kind, ParmAssignments *pa)
{
    int i;
    int8_t n;

    pa->kind = kind;

    if (kind) {
        int any = 0;

        for (i = 0; i < FRAME_WORDS; i++) {
            pa->slot[i] = arrayStreamFind(d, parmNames[i]);
            if (pa->slot[i] != -1)
                any = 1;
        }
        return any;
    }

    for (i = 0; i < FRAME_WORDS; i++) {
        *(int8_t *)&pa->slot[i] = SLOT_NONE;
        for (n = 0; n < num_streams(d); n++)
            if (ralStrIcmp(0, parmNames[i], stream_name(n)) == 0
                && enum_field(n, 0) == 0)
                *(int8_t *)&pa->slot[i] = n;
    }

    for (n = 0; n < num_streams(d); n++)
        if (ralStrIcmp(0, parmNames[PARM_MS], stream_name(n)) == 0
            && time_stream(n))
            *(int8_t *)&pa->slot[PARM_MS] = n;

    return *(int8_t *)&pa->slot[PARM_MS] != SLOT_NONE;
}

/* ---- samples coming back out ---------------------------------------- */

/* What the synthesiser hands back: how many samples, and where they are. */
typedef struct KlattSamples {
    int32_t  count;
    int32_t *samples;
} KlattSamples;

typedef void (*SampleFn)(uint32_t n, int32_t *samples, void *data);

/* Called by the synthesiser with each run of samples it has made.

   The run is handed on to the caller in pieces, each piece no longer than
   the distance to the next index mark, so that a mark can be reported at
   exactly the sample it belongs to rather than at the end of the block.
   Marks that have come due are released between pieces. */
int ourKlattCallback(void *user, KlattSamples *s)
{
    DeltaThis *d = (DeltaThis *)user;
    SynthDevice *dev = DL_DEVICE(DT_LANG(d));
    void *q = SD_QUEUE(dev);
    int32_t done = 0;

    /* Held: wait, but keep looking for a reason to give up. */
    while (SD_HOLD(dev)) {
        if (checkInterrupt(d))
            return 0;
    }

    while (done < s->count) {
        IsEmptyFn isEmpty = (IsEmptyFn)(*(void ***)q)[0];
        uint32_t piece;

        if (isEmpty(q))
            piece = s->count - done;
        else
            piece = iq_reduceLeadTime(q, s->count - done);

        if (SD_SAMPLE_CB(dev))
            ((SampleFn)SD_SAMPLE_CB(dev))(piece, s->samples + done,
                                          SD_SAMPLE_DATA(dev));

        while (sti_indexDue(q)) {
            if (!insertSynthIndex(d, iq_remove(q)))
                return 0;
        }
        done += piece;
    }
    return 1;
}

/* ---- one utterance ---------------------------------------------------- */

/* Turn a stretch of parameters into sound.

   Most of this is deciding whether anything has changed since last time. The
   synthesiser's constant parameters and the frame are both kept in the
   language record, and if neither has moved the synthesiser is left open and
   only the new values are sent. That matters: reopening it resets the filter
   state and would be audible at every boundary.

   The work itself is two calls. The first sends the parameters with nothing
   asked of them, which is how the engine finds out how long the result will
   be; the second sends them again and lets the samples come. */
int synthesize(DeltaThis *d, void *buf, int32_t isArray, int32_t *streamA,
               int32_t *streamB, int32_t from, int32_t to, int32_t more,
               int32_t a5, int32_t rate, int32_t a7, int32_t nFormants,
               int32_t addC, int32_t addA, int32_t addB, int32_t addD,
               int32_t volume, const int32_t *frame)
{
    DeltaLang *lang = DT_LANG(d);
    SynthDevice *dev = DL_DEVICE(lang);
    KlattConstParms cp;
    LastGlob *last;
    int32_t duration;
    int wasIdle;
    int changed = 0;
    int finish = 0;
    int rc;

    SD_INTERRUPTED(dev) = 1;

    /* The first time through, and whenever the caller changes its mind about
       arrays or streams, the parameter-to-stream map is rebuilt. */
    if (DL_FLAG_18(lang)) {
        if (!assignParameters(d, isArray, DL_BUF_100(lang))) {
            SD_INTERRUPTED(dev) = 0;
            return 0;
        }
        DL_FLAG_18(lang) = 0;
    }

    wasIdle = (SD_PLAYING(dev) == 0);
    SD_PLAYING(dev) = 1;

    if (isArray != ((ParmAssignments *)DL_BUF_100(lang))->kind
        && !assignParameters(d, isArray, DL_BUF_100(lang))) {
        SD_PLAYING(dev) = 0;
        SD_INTERRUPTED(dev) = 0;
        return 0;
    }

    if (isArray) {
        duration = to - from;
        if (duration < 0 || (duration == 0 && more == 0)) {
            SD_PLAYING(dev) = 0;
            SD_INTERRUPTED(dev) = 0;
            return 0;
        }
    } else {
        int8_t which =
            *(int8_t *)&((ParmAssignments *)DL_BUF_100(lang))->slot[PARM_MS];
        int32_t at = EVV_AT(delta_vars *, d->vars)->fence_base + which;

        if (!(streamA[at] & 1) || !(streamB[at] & 1)) {
            SD_INTERRUPTED(dev) = 0;
            return 0;
        }
        duration = timeDuration(d, streamA, streamB, which);
        if (duration == 0 && more == 0) {
            SD_PLAYING(dev) = 0;
            SD_INTERRUPTED(dev) = 0;
            return 0;
        }
    }

    if (SD_FILENAME(dev))
        buf = SD_FILENAME(dev);
    else if (!buf)
        buf = "";

    /* Build the synthesiser's constant parameters, then see whether any of
       the six that matter have moved since the last utterance. */
    memset(&cp, 0, sizeof cp);
    cp.unknown_00 = 100;
    cp.sample_rate = 0;
    cp.unknown_08 = 16;
    cp.n_formants = 5;
    cp.unknown_10 = 8;
    cp.unknown_14 = 1;
    cp.error_fn = (klatt_error_fn)errorKlattIgnore;

    if (rate) {
        cp.sample_rate = rate;
        DL_RATE(lang) = rate;
    }
    if (nFormants)
        cp.n_formants = nFormants;
    cp.unknown_2c += addC;
    cp.unknown_24 += addA;
    cp.unknown_28 += addB;
    cp.unknown_20 += addD;
    cp.error_fn = (klatt_error_fn)errorIgnore;
    cp.callback_mode = 2;
    cp.samples_fn = (klatt_samples_fn)ourKlattCallback;

    if (!a7)
        a7 = 5;

    last = (LastGlob *)DL_BUF_140(lang);
    if (cp.sample_rate != last->cp.sample_rate
        || cp.n_formants != last->cp.n_formants
        || cp.unknown_2c != last->cp.unknown_2c
        || cp.unknown_24 != last->cp.unknown_24
        || cp.unknown_28 != last->cp.unknown_28
        || cp.unknown_20 != last->cp.unknown_20) {
        changed = 1;
        last->cp = cp;
    }

    /* Lazy writing: if nothing at all has changed, the whole utterance can
       be skipped. Finding that out costs one pass with nothing asked of it. */
    if (SD_LAZY_WRITE(dev)) {
        int again = 1;

        /* Coming back from idle there is nothing to compare against, and
           when nothing has moved there is nothing to ask. */
        if (wasIdle)
            again = 0;
        else if (!changed && last->volume == volume
                 && memcmp(last->frame, frame, FRAME_WORDS * 4) == 0)
            again = 0;

        if (again) {
            if (isArray)
                rc = sendArrayParameters(d, from, from, 1, 0, 1, 0, a7,
                                         DL_BUF_100(lang), frame);
            else
                rc = sendStreamParameters(d, streamA, streamA, 0, 1, 0, 1, 0,
                                          a7, DL_BUF_100(lang), frame);
            if (!rc) {
                SD_PLAYING(dev) = 0;
                SD_INTERRUPTED(dev) = 0;
                return 0;
            }
        }

        last->volume = volume;
        memcpy(last->frame, frame, FRAME_WORDS * 4);
    }

    if (changed) {
        KlattSetConstParms(DL_KLATT(lang), cp);
        SD_OPEN(dev) = 0;
    }

    if (!SD_OPEN(dev)) {
        if (!KlattOpen(DL_KLATT(lang))) {
            SD_PLAYING(dev) = 0;
            SD_INTERRUPTED(dev) = 0;
            return 0;
        }
        SD_OPEN(dev) = 1;
    }

    /* How much of this utterance is being asked for now, and how much is
       being carried over. */
    if (more) {
        SD_PENDING(dev) = (more > duration) ? more : duration;
        if (SD_SAMPLE_CB(dev) && SD_DUR_CB(dev))
            ((void (*)(int32_t, uint32_t, void *))SD_DUR_CB(dev))(
                SD_PENDING(dev) * rate / MS_PER_SECOND,
                (uint32_t)(a5 * rate) / MS_PER_SECOND,
                SD_DUR_DATA(dev));
    } else {
        SD_PENDING(dev) = 0;
        finish = 1;
    }

    klattSetVolumeMultiplier(DL_KLATT(lang), volume);

    if (isArray)
        rc = sendArrayParameters(d, from, to, SD_LAZY_WRITE(dev), wasIdle,
                                 finish, 0, a7, DL_BUF_100(lang), frame);
    else
        rc = sendStreamParameters(d, streamA, streamB, duration,
                                  SD_LAZY_WRITE(dev), wasIdle, finish, 0, a7,
                                  DL_BUF_100(lang), frame);

    if (checkInterrupt(d))
        SD_PENDING(dev) = 0;

    if (SD_PENDING(dev) > duration)
        SD_PENDING(dev) -= duration;
    else
        SD_PENDING(dev) = 0;

    if (finish)
        finishSynthesis(d);

    SD_INTERRUPTED(dev) = 0;
    return rc;
}

/* The device record starts empty but for its index queue and the handle it
   uses to say it has no sleep cycle yet. */
THIS void *soundDeviceInfoCtor(void *self)
{
    SynthDevice *v = self;

    SD_SAMPLE_CB(v) = 0;
    SD_SAMPLE_DATA(v) = 0;
    SD_DUR_CB(v) = 0;
    SD_DUR_DATA(v) = 0;
    SD_FILENAME(v) = 0;
    sti_indexQueueCtor(SD_QUEUE(v));
    SD_SLEEPCYCLE(v) = -1;
    SD_PLAYING(v) = 0;
    SD_OPEN(v) = 0;
    SD_INTERRUPTED(v) = 0;
    SD_LAZY_WRITE(v) = 0;
    SD_PENDING(v) = 0;
    SD_LAST_CLOCK(v) = 0;
    SD_HOLD(v) = 0;
    SD_INDEX_CB(v) = 0;
    SD_INDEX_DATA(v) = 0;
    SD_PHONEME_CB(v) = 0;
    SD_PHONEME_DATA(v) = 0;
    return self;
}

ALIAS("?finishSynthesis@@YAXPAUDelta_This_Struct@@@Z", "finishSynthesis");
ALIAS("?deleteOutputDevice@@YAXPAUDelta_This_Struct@@@Z",
      "deleteOutputDevice");
ALIAS("?sleepCycleCallback@@YAHPAUSoundDeviceInfo@@H@Z",
      "sleepCycleCallback");
ALIAS("?assignParameters@@YAHPAUDelta_This_Struct@@HPAUParmAssignments@@@Z",
      "assignParameters");
ALIAS("?ourKlattCallback@@YAHPAXPAUKlattSamplesStruct@@@Z",
      "ourKlattCallback");
ALIAS("??0SoundDeviceInfo@@QAE@XZ", "soundDeviceInfoCtor");
