/* Make an engine instance, speak with it, throw it away, and again.
 *
 * The suite speaks a great deal through one instance and never makes a
 * second, so a fault in what an instance owns and gives back is invisible to
 * it. This is that check, and it exists because there was one: three fields
 * of the owner block were written at the offsets they had in IBM's build --
 * 440, 468 and 472 bytes into a block that is 64 bytes here -- so every run
 * wrote over whatever the arena had handed out next, and a run of instances
 * corrupted it at about the twentieth.
 *
 * Run it with more rounds than that. Build the engine with
 * -DEVV_ARENA_GUARD=1 and it says which block was written and from where
 * rather than falling over later somewhere else.
 *
 * usage: instances [rounds]        default 30
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

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

/* One instance, from nothing to nothing. Answers what it said. */
static long round_once(void)
{
    OldInst *h;
    uint32_t langs[32];
    int      n = 32;
    long     was = said;
    int      i;

    if (eo_getAvailableLanguages(langs, &n) || n < 1)
        return -1;
    h = eo_new();
    if (h == 0)
        h = eo_newEx(langs[0]);
    if (h == 0)
        return -1;

    eo_registerCallback(h, (void *)on_message, 0);
    if (!ev_setOutputBuffer(h, FRAME, frame)
        || !et_addText(h, "The quick brown fox.")
        || !et_synthesize(h))
        return -1;

    /* Nothing drains the engine's message queue by itself; asking whether it
       is still speaking is what pumps it. */
    for (i = 0; i < 3000 && eo_speaking(h); i++)
        nap(10);
    eo_synchronizeSynth(h);
    es_delete(h);
    return said - was;
}

int main(int argc, char **argv)
{
    int  rounds = argc > 1 ? atoi(argv[1]) : 30;
    long first = 0;
    int  r;

    evv_port_start();
    evvRunStaticInitialisers();

    for (r = 1; r <= rounds; r++) {
        long got = round_once();

        if (got < 0) {
            printf("instances: round %d would not speak\n", r);
            return 1;
        }
        if (r == 1)
            first = got;
        else if (got != first) {
            /* Every instance says the same sentence, so every instance owes
               the same samples. Fewer is the engine quietly running out of
               something it should have given back. */
            printf("instances: round %d said %ld samples where the first"
                   " said %ld\n", r, got, first);
            return 1;
        }
    }

    evv_port_finish();
    printf("instances: %d rounds, %ld samples each\n", rounds, first);
    return 0;
}
