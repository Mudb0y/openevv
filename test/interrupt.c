/* Interrupt an utterance and then speak again on the same instance, which is
 * what a caller that lets someone stop the speech does.
 *
 * It exists because that used to fault. Answering eciDataAbort from the
 * callback makes eciSpeaking call the engine's own stop, and the stop set the
 * machine's error flag from the calling thread while the machine was walking
 * on the synthesiser's; the next backtrack then answered without putting back
 * anything it had saved, and a call a few rules later took its arguments from
 * below what had been pushed. That was a fault in vinitloc_new on a thread
 * that never called in, and it is what a screen reader met when someone
 * interrupted speech.
 *
 * It also used to go quiet from the second interruption onwards, accepting
 * text and answering no error and saying nothing. The stop put back the
 * application queue's count of what it had posted and not its count of what
 * had been collected, so the two drifted; a stale collected count that
 * happened to equal the fresh posted count read as nothing outstanding, and
 * the queue handed over none of the samples sitting in it. Only the
 * sixty-four bit builds had it, because the field was being reached by the
 * byte it sat at when a pointer was four bytes.
 *
 * So this asserts what it used to only print: every follow-up utterance is
 * worth exactly what the first whole one was. A follow-up worth nothing is
 * the silence back, and one worth anything else is the interrupt leaving
 * something else behind.
 *
 * usage: interrupt [turns]        default 12
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "evv_abi.h"

enum { FRAME = 1024 };
typedef struct OldInst OldInst;
enum ECIMessage { eciWaveformBuffer, eciPhonemeBuffer, eciIndexReply };
enum ECICallbackReturn { eciDataNotProcessed, eciDataProcessed, eciDataAbort };

OldInst *STDCALL eo_new(void);
OldInst *STDCALL eo_newEx(int32_t language);
int      STDCALL es_delete(OldInst *h);
int      STDCALL et_addText(OldInst *h, const char *text);
int      STDCALL et_synthesize(OldInst *h);
int      STDCALL ev_setOutputBuffer(OldInst *h, int32_t n, void *buf);
void     STDCALL eo_registerCallback(OldInst *h, void *cb, void *data);
void     STDCALL eo_synchronizeSynth(OldInst *h);
int      STDCALL eo_speaking(OldInst *h);
int      STDCALL eo_getAvailableLanguages(uint32_t *out, int *count);
int32_t  STDCALL es_progStatus(OldInst *h);
void     STDCALL es_errorMessage(OldInst *h, char *out);
int      STDCALL eo_clearInput(OldInst *h);
int      STDCALL eo_stop(OldInst *h);
int      STDCALL es_reset(OldInst *h);
void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

static short frame[FRAME];
static long  said;
static int   buffers, stop_at;

static enum ECICallbackReturn STDCALL on_message(OldInst *h,
                                                 enum ECIMessage msg,
                                                 long param, void *data)
{
    (void)h; (void)data;
    if (msg == eciWaveformBuffer) {
        said += param;
        if (stop_at && ++buffers >= stop_at)
            return eciDataAbort;
    }
    return eciDataProcessed;
}

static void nap(long ms)
{
    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
}

static long say(OldInst *h, const char *text, int abort_at)
{
    long was = said;
    int i;

    buffers = 0;
    stop_at = abort_at;
    if (!et_addText(h, text) || !et_synthesize(h)) {
        printf("  refused the text\n");
        return -1;
    }
    for (i = 0; i < 3000 && eo_speaking(h); i++)
        nap(10);
    eo_synchronizeSynth(h);
    return said - was;
}

int main(int argc, char **argv)
{
    int turns = argc > 1 ? atoi(argv[1]) : 12;
    uint32_t langs[32];
    int n = 32, t;
    long whole;
    OldInst *h;
    const char *lots = "The quick brown fox jumps over the lazy dog, and then"
        " says a great deal more so that there is plenty to interrupt before"
        " it has finished saying any of it at all.";

    evv_port_start();
    evvRunStaticInitialisers();
    if (eo_getAvailableLanguages(langs, &n) || n < 1)
        return 1;
    h = eo_new();
    if (h == 0)
        h = eo_newEx(langs[0]);
    if (h == 0)
        return 1;
    eo_registerCallback(h, (void *)on_message, 0);
    if (!ev_setOutputBuffer(h, FRAME, frame))
        return 1;

    /* One clean utterance first, so what a whole one is worth is known. */
    whole = say(h, "The quick brown fox.", 0);
    printf("whole:    %ld\n", whole);
    fflush(stdout);
    if (whole <= 0) {
        printf("interrupt: the first utterance said nothing\n");
        return 1;
    }

    for (t = 1; t <= turns; t++) {
        long cut = say(h, lots, 3);
        long after;

        printf("turn %2d: aborted after %ld, ", t, cut);
        fflush(stdout);
        after = say(h, "The quick brown fox.", 0);
        printf("then said %ld\n", after);
        fflush(stdout);
        if (after != whole) {
            printf("interrupt: turn %d owed %ld and got %ld\n",
                   t, whole, after);
            return 1;
        }
    }

    es_delete(h);
    evv_port_finish();
    printf("interrupt: %d turns, every follow-up worth %ld\n", turns, whole);
    return 0;
}
