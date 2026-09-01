/* The rates IBM never shipped.
 *
 * test/rate.c beside this one is about a different question -- whether an
 * instance survives having its rate changed at all -- and it asks it only of
 * the numbers IBM's engine took. This one is about the rates the synthesiser
 * can now be run at, and it has two halves.
 *
 * The first is the claim the whole thing rests on: that IBM's four resonator
 * tables are the two formulae they look like, so tables for another rate can
 * be built rather than found. It is checked rather than remembered, by
 * building the tables for IBM's own two rates and holding them against IBM's
 * own arrays entry by entry. The engine never uses what this builds at those
 * two rates -- KlattSetConstParms reaches for the static arrays by name --
 * which is exactly why the check has to be made explicitly here.
 *
 * The second is that every rate speaks, that it speaks for the same length
 * of time, that nothing comes out at a level that is not speech, and that
 * the two rates IBM did ship still answer what they always answered. The engine works in milliseconds and the rate turns those into
 * samples, so the same sentence owes a sample count in proportion to the
 * rate. It is not exact: KlattSynth counts its decay in whole milliseconds
 * worked out from a block of at most two hundred samples, so a rate that
 * does not divide evenly loses a fraction of a millisecond a block and the
 * tails come out slightly long. EVV_RATES_REPORT=1 prints what each rate
 * actually said, which is how the bound below was chosen.
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
static long  said;
static int   report;

static void watch(long count);

static enum ECICallbackReturn STDCALL on_message(OldInst *h,
                                                 enum ECIMessage msg,
                                                 long param, void *data)
{
    (void)h;
    (void)data;
    if (msg == eciWaveformBuffer) {
        said += param;
        watch(param);
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

/* The sentence the length check is made on, and the one the loudness check
   is made on. They are different, and that is the point: the fox sentence
   does not provoke the runaway at all -- at 44,509 hertz, one hertz past the
   boundary, it comes out at a peak of 4,880 like any other rate -- while the
   second comes out at full scale. A harness that spoke only the first would
   have said the engine was fine at every rate up to ninety-six thousand. */
static const char PLAIN_TEXT[] =
    "The quick brown fox jumps over the lazy dog.";
static const char LOUD_TEXT[] =
    "Testing 1 2 3, ABC XYZ, hyphenated-words and UPPERCASE.";

static long say_once(OldInst *h, const char *text)
{
    long was = said;
    int  i;

    if (!et_addText(h, text) || !et_synthesize(h))
        return -1;

    for (i = 0; i < 6000 && eo_speaking(h); i++)
        nap(10);
    eo_synchronizeSynth(h);
    return said - was;
}

/* Whether what came out is speech or a runaway.
 *
 * This is the check that catches the one real limit found in this work.
 * Above about forty-four and a half thousand hertz the synthesiser's
 * accumulator runs away on ordinary speech, and what a caller gets is the
 * same waveform some thousands of times too large, wrapped into sixteen
 * bits. The sample count is unchanged, the duration is unchanged and the
 * rate reads back correctly, so every other check here passes.
 *
 * The peak alone cannot tell the two apart: real speech is normalised and
 * goes near full scale, and so does a wrapped runaway. What tells them apart
 * is how fast the signal moves. A waveform sampled well above its own
 * bandwidth cannot swing its whole amplitude between one sample and the
 * next, so the largest step from sample to sample stays under the largest
 * sample; wrapping does exactly that swing, and more. Measured on one
 * sentence: at 22,050 hertz the largest step is 2,856 against a peak of
 * 10,821, at 44,100 it is 745 against 5,668, and one hertz past the boundary
 * it is 63,068 against 32,671.
 *
 * Eight thousand is exempt, and that is IBM's own doing rather than a
 * loophole: at eight thousand the same sentence steps 39,413 against a peak
 * of 28,112, because there the speech really does move that fast relative to
 * the rate. Nothing this work touches can change eight thousand -- it uses
 * IBM's own tables and its sample count is pinned below -- so the rule is
 * applied from eleven thousand and twenty five upwards, where it holds with
 * an order of magnitude to spare.
 */
static long peak_seen;
static long step_seen;
static int  have_last;
static int  last_sample;

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
    }
}

static void watch_reset(void)
{
    peak_seen = 0;
    step_seen = 0;
    have_last = 0;
}

/* Answers zero when what came out cannot be speech. */
static int watch_verdict(int32_t hz)
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

/* What IBM's two rates have always said, so that widening the interface
   cannot quietly move them. These are test/rate.c's own numbers. */
#define SAID_AT_8000   27944
#define SAID_AT_11025  38423

/* How far from proportional a rate may come out.

   Hardly at all, as it turns out. KlattSynth works a frame's length out as
   its duration in milliseconds times the rate over a thousand, truncated, so
   a rate that is not a whole number of samples to the millisecond rounds
   each frame down by a fraction. Over the whole sentence that comes to a
   quarter of one per cent between the widest pair -- 3,493 milliseconds
   against 3,485. One per cent is the bound, which is still far tighter than
   the distance between any two of the rates below. */
#define TOLERANCE_PCT  1

struct want { int32_t set; int32_t hz; };

static const struct want WANTED[] = {
    { 0, 8000 }, { 1, 11025 }, { 2, 22050 }, { 3, 16000 },
    { 4, 32000 }, { 5, 44100 },
    /* Not a number IBM gave a meaning to, and not one we did either: a rate
       in hertz, which is what makes trying an odd one cost no rebuild. */
    { 37800, 37800 },
};

#define WANTED_COUNT ((int)(sizeof WANTED / sizeof WANTED[0]))

/* Numbers that are neither a rate we know nor a plausible rate in hertz. */
static const int32_t REFUSED[] = { 7, 100, 7999, KLATT_RATE_MAX + 1, -1 };

#define REFUSED_COUNT ((int)(sizeof REFUSED / sizeof REFUSED[0]))

int main(void)
{
    OldInst *h;
    uint32_t langs[32];
    int n = 32;
    int r;
    long base = 0;

    report = getenv("EVV_RATES_REPORT") != 0;

    if (!checkTables())
        return 1;

    evv_port_start();
    evvRunStaticInitialisers();

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

    /* The default environment goes through a different gate: it asks the
       device whether it will give us the rate, where an instance does not.
       So a numbered rate the device has no bit for is refused there and
       nowhere else, which would leave the new numbers settable on an
       instance and not as a default -- a difference nothing else here would
       notice. */
    for (r = 0; r < WANTED_COUNT; r++) {
        if (WANTED[r].set > 6)
            continue;
        if (es_setDefaultParam(PARAM_RATE, WANTED[r].set) < 0) {
            printf("rates: the default environment refused rate %d\n",
                   (int)WANTED[r].set);
            return 1;
        }
    }
    if (es_setDefaultParam(PARAM_RATE, 7) >= 0) {
        printf("rates: the default environment took 7, which is not a rate\n");
        return 1;
    }
    es_setDefaultParam(PARAM_RATE, 1);

    for (r = 0; r < WANTED_COUNT; r++) {
        int32_t set = WANTED[r].set;
        int32_t hz = WANTED[r].hz;
        long    got, owed, slack;

        if (ev_setParam(h, PARAM_RATE, set) < 0) {
            printf("rates: it refused rate setting %d\n", (int)set);
            return 1;
        }
        if (eo_getParam(h, PARAM_RATE) != set) {
            printf("rates: asked for %d and it says %d\n", (int)set,
                   eo_getParam(h, PARAM_RATE));
            return 1;
        }

        watch_reset();
        got = say_once(h, PLAIN_TEXT);
        if (got < 0) {
            printf("rates: it would not speak at %d hertz\n", (int)hz);
            return 1;
        }
        if (got == 0) {
            printf("rates: silent at %d hertz\n", (int)hz);
            return 1;
        }
        if (report)
            printf("rates: %6d hertz  %8ld samples  %8.1f ms  peak %8ld\n",
                   (int)hz, got, 1000.0 * (double)got / (double)hz,
                   peak_seen);
        if (!watch_verdict(hz))
            return 1;

        /* And the text that provokes it, whose only claim is the loudness.
           A runaway passes every other test here -- the sample count, the
           duration and the rate read back are all exactly right -- so this
           is the one thing that says it happened. */
        watch_reset();
        if (say_once(h, LOUD_TEXT) <= 0) {
            printf("rates: it would not speak the second text at %d hertz\n",
                   (int)hz);
            return 1;
        }
        if (report)
            printf("rates: %6d hertz  peak %8ld  largest step %8ld  on the "
                   "second text\n", (int)hz, peak_seen, step_seen);
        if (!watch_verdict(hz))
            return 1;

        if (hz == 8000 && got != SAID_AT_8000) {
            printf("rates: %ld samples at 8 kHz where it always said %d\n",
                   got, SAID_AT_8000);
            return 1;
        }
        if (hz == 11025) {
            if (got != SAID_AT_11025) {
                printf("rates: %ld samples at 11 kHz where it always said "
                       "%d\n", got, SAID_AT_11025);
                return 1;
            }
            base = got;
        }

        /* Everything else against eleven thousand and twenty five, which is
           the rate the samples in test/samples.sha256 are at and therefore
           the one known to be right. */
        if (base == 0)
            continue;
        owed = (long)((double)base * (double)hz / 11025.0);
        slack = owed / (100 / TOLERANCE_PCT);
        if (got < owed - slack || got > owed + slack) {
            printf("rates: %ld samples at %d hertz, owed about %ld\n",
                   got, (int)hz, owed);
            return 1;
        }
    }

    es_delete(h);
    evv_port_finish();
    printf("rates: the tables are IBM's formulae, and all %d rates speak "
           "to length\n", WANTED_COUNT);
    return 0;
}
