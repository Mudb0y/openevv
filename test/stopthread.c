/* Stop an utterance from a thread that is not the one speaking.
 *
 * This is the case a screen reader actually makes: the reader's own thread
 * calls eciStop because a key was pressed, while the engine is walking rules
 * on the synthesiser's thread. It is also where the original crash was --
 * es_engsynFlush set the machine's error flag from the calling thread while
 * the machine was mid-walk, and the next backtrack answered without putting
 * back what it had saved.
 *
 * That was fixed by throwing the error only when ELOQ_BUSY says the machine is
 * not walking, and then the queue-count fix changed what a stop leaves behind.
 * Neither was re-measured from a second thread afterwards, so docs/status.md
 * carried "whether the stop door is still a race is an open question" until
 * this. test/interrupt.c does not answer it: it interrupts by answering
 * eciDataAbort from the callback, which happens on the engine's own thread and
 * never crosses one.
 *
 * What every turn requires. The process has to survive, which is the crash.
 * The stop has to have been made while the engine was still handing samples
 * over, which is the condition being tested rather than an incidental. And
 * every utterance after a stop has to be worth exactly what a whole one is
 * worth, which is the silence: a follow-up worth nothing is the queue believing
 * it has caught up, and one worth anything else is the stop having left
 * something behind.
 *
 * What it deliberately does NOT require is that the interrupted utterance come
 * out short, and that is worth explaining, because requiring it is the obvious
 * thing to do and it is wrong. A stop cannot make the engine abandon an
 * utterance -- that is settled, and stm_qtSuspend waits for the synthesis
 * thread to finish the message it is on. All a stop can do is stop the
 * remaining buffers being handed over, so whether the count comes out short
 * depends on whether the suspension happens to land between two buffers. With
 * the callback returning at once the engine can finish the whole thing before
 * another thread reacts at all; with the callback pacing itself like a real
 * player, the pacing runs on the engine's own thread and the stop then waits
 * for the whole delivery, so it can never truncate. Both were tried. Neither
 * is a fault and neither is stable, so the count is printed and not asserted
 * on.
 *
 * The stop is counted off rather than timed. The stopper waits until the
 * callback has taken a given number of buffers and then stops, and that number
 * differs every turn, so successive turns interrupt at points spread across the
 * delivery. An earlier form waited a number of milliseconds instead and was
 * worthless: under Wine the whole utterance sometimes finished before the
 * sleeping stopper woke. A test whose meaning depends on how fast the host
 * happens to be is not a test.
 *
 * Nothing here times anything, and no number here is a latency.
 *
 * It is built for Windows as well as for here, which test/landing.c is not,
 * because Wine is where this used to fail: eight of twelve turns before the
 * busy guard, against none of twelve on real Windows. `make stopthread' is
 * this machine and `make win-stopthread' is the same thing under Wine.
 *
 * usage: stopthread [turns]        default 24
 */
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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
int      STDCALL eo_stop(OldInst *h);
void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

static short frame[FRAME];
static long  said;

/* Written by one thread and read by the other, each of them one word that only
   ever moves one way during a turn, which needs no lock of its own. */
static volatile long buffers;      /* how many the callback has taken */
static volatile long stop_after;   /* how many this turn lets through */
static volatile int  delivering;   /* between synthesize and the engine idle */
static volatile int  stopped_live; /* the stop was made while it was */

static enum ECICallbackReturn STDCALL on_message(OldInst *h,
                                                 enum ECIMessage msg,
                                                 long param, void *data)
{
    (void)h; (void)data;
    if (msg == eciWaveformBuffer) {
        said += param;
        buffers++;
    }
    return eciDataProcessed;
}

static void nap(long ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
#endif
}

/* One thread, made the way the platform makes one. The engine's porting layer
   does this too, but it is not for a caller to use, and a harness that went
   through it would be testing that instead of the door it means to. */
#ifdef _WIN32
typedef HANDLE evv_thread;
#define STOPPER_RET DWORD WINAPI
#else
typedef pthread_t evv_thread;
#define STOPPER_RET void *
#endif

static STOPPER_RET stopper(void *arg)
{
    OldInst *h = arg;
    long i;

    /* Only the counter and the flag are watched. Nothing else in the engine is
       asked anything from here: this thread's one job is the stop, and another
       call from outside would be a second question mixed into the one being
       asked. */
    for (i = 0; i < 60000 && buffers < stop_after && delivering; i++)
        nap(1);

    stopped_live = delivering;
    eo_stop(h);
    return 0;
}

static int thread_start(evv_thread *t, OldInst *h)
{
#ifdef _WIN32
    *t = CreateThread(NULL, 0, stopper, h, 0, NULL);
    return *t != NULL;
#else
    return pthread_create(t, 0, stopper, h) == 0;
#endif
}

static void thread_wait(evv_thread t)
{
#ifdef _WIN32
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
#else
    pthread_join(t, 0);
#endif
}

/* Speak it through, nobody interfering. */
static long say(OldInst *h, const char *text)
{
    long was = said;
    int  i;

    if (!et_addText(h, text) || !et_synthesize(h))
        return -1;
    for (i = 0; i < 3000 && eo_speaking(h); i++)
        nap(10);
    eo_synchronizeSynth(h);
    return said - was;
}

/* Speak it and have another thread stop it partway through the delivery. */
static long say_and_stop(OldInst *h, const char *text, long after)
{
    evv_thread th;
    long was = said;
    int  i;

    buffers = 0;
    stop_after = after;
    stopped_live = 0;
    delivering = 1;

    if (!et_addText(h, text) || !et_synthesize(h)) {
        delivering = 0;
        return -1;
    }
    if (!thread_start(&th, h)) {
        delivering = 0;
        return -1;
    }
    for (i = 0; i < 3000 && eo_speaking(h); i++)
        nap(1);
    delivering = 0;
    thread_wait(th);
    eo_synchronizeSynth(h);
    return said - was;
}

int main(int argc, char **argv)
{
    int turns = argc > 1 ? atoi(argv[1]) : 24;
    uint32_t langs[32];
    int n = 32, t, live = 0;
    long whole, through;
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

    whole = say(h, "The quick brown fox.");
    through = say(h, lots);
    printf("whole: %ld   the long one spoken through: %ld\n", whole, through);
    fflush(stdout);
    if (whole <= 0 || through <= 0) {
        printf("stopthread: nothing was said before any stop\n");
        return 1;
    }

    for (t = 1; t <= turns; t++) {
        long cut, after, letthrough;

        /* A different point of the delivery each turn, kept short of the end so
           the stop has somewhere to land. */
        letthrough = 1 + ((long)(t - 1) * 7) % (through / FRAME / 2 + 1);
        cut = say_and_stop(h, lots, letthrough);
        printf("turn %2d: stop after %3ld buffers, %-12s got %6ld of %ld, ",
               t, letthrough, stopped_live ? "mid-delivery" : "too late,",
               cut, through);
        fflush(stdout);
        if (stopped_live)
            live++;
        after = say(h, "The quick brown fox.");
        printf("then said %ld\n", after);
        fflush(stdout);

        if (cut < 0 || after < 0) {
            printf("stopthread: turn %d had text refused\n", t);
            return 1;
        }
        if (after != whole) {
            printf("stopthread: turn %d owed %ld and got %ld\n",
                   t, whole, after);
            return 1;
        }
    }

    es_delete(h);
    evv_port_finish();

    /* If no turn managed to stop a live engine the run says nothing at all,
       and saying so is the whole point of counting them. */
    if (live == 0) {
        printf("stopthread: not one of %d stops was made while the engine was"
               " delivering, so nothing here was tested\n", turns);
        return 1;
    }
    printf("stopthread: %d turns, %d of them stopping a live engine, every"
           " follow-up worth %ld\n", turns, live, whole);
    return 0;
}
