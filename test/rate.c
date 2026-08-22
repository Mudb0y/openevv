/* Change the sample rate on an instance that speaks into a buffer.
 *
 * The suite cannot see this. cli/probe.c does register a buffer, and the
 * reference does the same, but neither ever asks for another rate, so an
 * instance that goes permanently silent on eciSetParam(eciSampleRate) matched
 * IBM's binary over all 81 cases in all six builds -- because IBM's engine
 * goes silent there too. Ours no longer does, and this is what says so.
 *
 * Three things are checked, and the first is the one that was wrong: every
 * utterance after a rate change has to arrive at all. The rate has to read
 * back as the one asked for. And 8 kHz and 11 kHz have to answer different
 * numbers of samples, because a rate merely written into the environment and
 * never handed to the engine would pass the other two.
 *
 * That third one does not say which code handed it over. Either ev_setParam
 * rebuilds the output there and then, or ev_sendChangedEnvironment does it at
 * the next utterance from the same OI_WHERE; break one and the other still
 * carries the rate, and only breaking both makes the two rates answer the same
 * count. Which is the honest shape of the question: what a caller can tell is
 * whether the rate arrived, not who carried it.
 *
 * 22 kHz is asked for but not held to that: it answers 11 kHz's sample count
 * at half the duration, which is a separate fault and not this one.
 *
 * Then the four numbers that describe a device, environment parameters 13 to
 * 16, each set to the value it already holds. They go into the same audio
 * format as the rate and lost the buffer the same way. They say nothing about
 * where a buffer's samples come from, so the count must not move either.
 *
 * The rates are asked for in a run that comes back to where it was --
 * 11, 8, 11, 8, 22, 11 -- because setting the rate back was as broken as
 * changing it, and so was setting it to the value it already had.
 *
 * usage: rate
 */

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "evv_abi.h"

enum { FRAME = 1024 };

/* eciSampleRate, and the four numbers that describe a device. Those four
   have no names in the published interface; they are the block size and the
   millisecond figures the audio format is built from. */
enum { PARAM_RATE = 5, DEVICE_FIRST = 13, DEVICE_LAST = 16 };

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
void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

static short frame[FRAME];
static long  said;

static enum ECICallbackReturn STDCALL on_message(OldInst *h,
                                                 enum ECIMessage msg,
                                                 long param, void *data)
{
    (void)h;
    (void)data;
    if (msg == eciWaveformBuffer)
        said += param;
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

/* One sentence. Answers what it said, or -1 if it would not speak. */
static long say_once(OldInst *h)
{
    long was = said;
    int  i;

    if (!et_addText(h, "The quick brown fox jumps over the lazy dog.")
        || !et_synthesize(h))
        return -1;

    /* Nothing drains the engine's message queue by itself; asking whether it
       is still speaking is what pumps it. */
    for (i = 0; i < 3000 && eo_speaking(h); i++)
        nap(10);
    eo_synchronizeSynth(h);
    return said - was;
}

int main(void)
{
    static const int32_t ASK[] = { 1, 0, 1, 0, 2, 1 };
    const int rounds = (int)(sizeof ASK / sizeof ASK[0]);
    long at[3];
    OldInst *h;
    uint32_t langs[32];
    int n = 32;
    int r;

    for (r = 0; r < 3; r++)
        at[r] = 0;

    evv_port_start();
    evvRunStaticInitialisers();

    if (eo_getAvailableLanguages(langs, &n) || n < 1) {
        printf("rate: no language\n");
        return 1;
    }
    h = eo_new();
    if (h == 0)
        h = eo_newEx(langs[0]);
    if (h == 0) {
        printf("rate: no instance\n");
        return 1;
    }

    eo_registerCallback(h, (void *)on_message, 0);
    if (!ev_setOutputBuffer(h, FRAME, frame)) {
        printf("rate: it would not take the buffer\n");
        return 1;
    }

    for (r = 0; r < rounds; r++) {
        int32_t want = ASK[r];
        long    got;

        if (ev_setParam(h, PARAM_RATE, want) < 0) {
            printf("rate: it refused rate %d\n", (int)want);
            return 1;
        }
        if (eo_getParam(h, PARAM_RATE) != want) {
            printf("rate: asked for %d and it says %d\n", (int)want,
                   eo_getParam(h, PARAM_RATE));
            return 1;
        }

        got = say_once(h);
        if (got < 0) {
            printf("rate: it would not speak at rate %d\n", (int)want);
            return 1;
        }
        if (got == 0) {
            printf("rate: silent at rate %d -- the buffer was lost\n",
                   (int)want);
            return 1;
        }
        /* The same sentence at the same rate owes the same samples, however
           many times round. What differs between utterances is which samples,
           not how many. */
        if (at[want] != 0 && got != at[want]) {
            printf("rate: %ld samples at rate %d where it said %ld before\n",
                   got, (int)want, at[want]);
            return 1;
        }
        at[want] = got;
    }

    /* The device numbers. eo_getParam answers what is in force and each is
       set straight back, so nothing about the sound is being asked to
       change -- only whether asking at all costs the buffer. */
    for (r = DEVICE_FIRST; r <= DEVICE_LAST; r++) {
        int32_t was = eo_getParam(h, r);
        long    got;

        if (ev_setParam(h, r, was) < 0) {
            printf("rate: it refused parameter %d at its own value %d\n",
                   r, (int)was);
            return 1;
        }

        got = say_once(h);
        if (got <= 0) {
            printf("rate: silent after parameter %d -- the buffer was lost\n",
                   r);
            return 1;
        }
        if (got != at[1]) {
            printf("rate: %ld samples after parameter %d where 11 kHz said"
                   " %ld\n", got, r, at[1]);
            return 1;
        }
    }

    if (at[0] == at[1]) {
        printf("rate: 8 kHz and 11 kHz both said %ld samples, so the rate"
               " went no further than the environment\n", at[0]);
        return 1;
    }

    es_delete(h);
    evv_port_finish();
    printf("rate: %d changes and %d device numbers, %ld samples at 8 kHz"
           " and %ld at 11 kHz\n", rounds,
           DEVICE_LAST - DEVICE_FIRST + 1, at[0], at[1]);
    return 0;
}
