/* The rates IBM never shipped, and the two ways of reaching them.
 *
 * test/rate.c beside this one is about a different question -- whether an
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

enum { FRAME = 4096 };

enum { PARAM_RATE = 5 };

/* Room for the longest run and for the two the held ones are held against.
   The test sentence is 55,696 samples at eight thousand and 76,582 at eleven
   thousand, and four times the larger is the most any rate here produces. */
enum { ROOM = 320000 };
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

/* IBM's own four, as tools/gen-tables.py sliced them out of clsyn.obj. */
extern const int16_t klatt_EX8[3992];
extern const int16_t klatt_CO8[4991];
extern const int16_t klatt_EX11[3992];
extern const int16_t klatt_CO11[4992];

static short frame[FRAME];
static int   report;

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

/* ---- speaking -------------------------------------------------------- */

/* One sentence, on an instance of its own.
 *
 * A fresh instance every time, because the engine's second utterance is not
 * its first and the whole of the check below is one run against another. A
 * new instance says the same thing every run, which is what test/instances.c
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

/* Each setting, the rate it comes to, and which of the engine's own two it
   is held from -- nought where the engine synthesises it outright. */
struct want {
    int32_t set;
    int32_t hz;
    int32_t from;
    int32_t times;
};

static const struct want WANTED[] = {
    { 0, 8000,  0,     1 },
    { 1, 11025, 0,     1 },
    { 3, 16000, 8000,  2 },
    { 2, 22050, 11025, 2 },
    { 4, 32000, 8000,  4 },
    { 5, 44100, 11025, 4 },
    /* Not a number anyone gave a meaning to, and no whole multiple of either
       base, so this is the one that still exercises synthesis at a rate IBM
       never shipped. */
    { 37800, 37800, 0, 1 },
};

#define WANTED_COUNT ((int)(sizeof WANTED / sizeof WANTED[0]))

/* Numbers that are neither a rate we know nor a plausible rate in hertz. */
static const int32_t REFUSED[] = { 6, 7, 100, 7999, KLATT_RATE_MAX + 1, -1 };

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

    report = getenv("EVV_RATES_REPORT") != 0;

    if (!checkTables())
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
        if (WANTED[r].set > 5)
            continue;
        if (es_setDefaultParam(PARAM_RATE, WANTED[r].set) < 0) {
            printf("rates: the default environment refused rate %d\n",
                   (int)WANTED[r].set);
            return 1;
        }
    }
    if (es_setDefaultParam(PARAM_RATE, 6) >= 0) {
        printf("rates: the default environment took 6, which is not a rate\n");
        return 1;
    }
    es_setDefaultParam(PARAM_RATE, 1);

    for (r = 0; r < WANTED_COUNT; r++) {
        int32_t set = WANTED[r].set;
        int32_t hz = WANTED[r].hz;
        long    got, owed, slack;
        int32_t back = -1;

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
                   WANTED[r].times > 1 ? "  held" : "");

        /* Only where the engine synthesised the rate outright. A held rate
           steps exactly as its base does -- repeating a sample leaves the
           jumps between them where they were -- so the slew rule, which is a
           property of a bandlimited signal, says nothing about one that is
           deliberately not bandlimited. What covers a held rate is the
           stronger check further down: if it is the base's samples exactly
           and the base passed, there is nothing left to ask. */
        if (WANTED[r].times == 1 && !verdict(hz))
            return 1;

        if (hz == 8000) {
            if (got != SAID_AT_8000) {
                printf("rates: %ld samples at 8 kHz where it always said "
                       "%d\n", got, SAID_AT_8000);
                return 1;
            }
            memcpy(base8, kept, (size_t)got * sizeof(short));
            base8_n = got;
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

        /* The strong one. A held rate is the engine's own samples and
           nothing else, each repeated a whole number of times, so this is
           byte for byte rather than a matter of how close two spectra are.
           Breaking the copy loop by one sample fails it at once. */
        if (WANTED[r].times > 1) {
            const short *base = WANTED[r].from == 8000 ? base8 : base11;
            long baseN = WANTED[r].from == 8000 ? base8_n : base11_n;
            long times = WANTED[r].times;
            long i, j, wrong = 0;

            if (baseN <= 0) {
                printf("rates: nothing to hold %d hertz against\n", (int)hz);
                return 1;
            }
            if (got != baseN * times) {
                printf("rates: %ld samples at %d hertz where holding %ld of "
                       "%d owes %ld\n", got, (int)hz, baseN,
                       (int)WANTED[r].from, baseN * times);
                return 1;
            }
            for (i = 0; i < baseN && wrong == 0; i++)
                for (j = 0; j < times; j++)
                    if (kept[i * times + j] != base[i]) {
                        wrong = i * times + j + 1;
                        break;
                    }
            if (wrong) {
                printf("rates: %d hertz is not %d held: sample %ld is %d "
                       "where the engine said %d\n", (int)hz,
                       (int)WANTED[r].from, wrong - 1,
                       (int)kept[wrong - 1], (int)base[(wrong - 1) / times]);
                return 1;
            }
            if (report)
                printf("rates: %6d hertz  every one of %ld samples repeated "
                       "%ld times exactly\n", (int)hz, baseN, times);
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
    printf("rates: the tables are IBM's formulae, the held rates are the "
           "engine's own samples repeated, and all %d speak to length\n",
           WANTED_COUNT);
    return 0;
}
