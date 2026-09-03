/* Hand one text to the engine whole and then in pieces, and see what it costs.
 *
 * The add-on hands a long message over as several utterances so that asking
 * for silence waits out one piece rather than the whole of it -- the engine
 * cannot be interrupted, so a cancel costs the synthesis time of whatever is
 * in flight. What that arrangement has to answer for is the sound, because a
 * piece boundary is a clause end to this engine: it ends the utterance there,
 * with the pause a full stop gets.
 *
 * Nothing else here can see that. test/suite.sh speaks whole utterances
 * through cli/probe.c, so every boundary it makes is one IBM's binary makes
 * too and the comparison says nothing about where a boundary may go. The
 * add-on's own two checks stand the engine in for, so they hold the queue
 * shape and the state safety and never make a sound. This is the audible side,
 * and it is what settled the shape of the driver's rule.
 *
 * A boundary at a sentence end is free. The same text cut anywhere else is
 * not, and the difference is silence at the cut: about 0.40 s each, which is
 * the pause a full stop already has. That asymmetry is the whole argument for
 * the driver preferring sentence ends and for a dot having to argue that it is
 * one -- an abbreviation and an initial end in a dot and do not end a
 * sentence, and cutting there is heard.
 *
 * So the cases divide. Those marked FREE are boundaries the add-on's rule
 * would make, and they have to cost nothing: a difference there is a fault,
 * either in this engine or in what the driver may assume of it. Those marked
 * COST are boundaries it declines, and their cost is printed as the evidence
 * for declining them rather than held to a number, since a number measured
 * here would be a number about English at one speaking rate.
 *
 * A case is one text with a vertical bar wherever a piece ends. Whole is the
 * same characters with the bars taken out, so the two runs say the same thing
 * and differ only in how many utterances it was.
 *
 * Each run gets an instance of its own, because the engine's second utterance
 * is not its first: the same sentence twice on one instance gives the same
 * sample count under a different hash. The count is what is compared here for
 * that reason, and it is enough -- a pause the engine did not mean to make is
 * samples it did not mean to produce.
 *
 * usage: pieces
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "evv_abi.h"

enum { FRAME = 1024, RATE = 11025, MAXPIECES = 64, MAXTEXT = 2048 };

/* Whether a case is a boundary the add-on would make or one it declines. */
enum { FREE, COST };

typedef struct OldInst OldInst;

enum ECIMessage { eciWaveformBuffer, eciPhonemeBuffer, eciIndexReply };
enum ECICallbackReturn { eciDataNotProcessed, eciDataProcessed, eciDataAbort };

OldInst *STDCALL eo_newEx(int32_t language);
int      STDCALL es_delete(OldInst *h);
int      STDCALL et_addText(OldInst *h, const char *text);
int      STDCALL et_synthesize(OldInst *h);
int      STDCALL ev_setOutputBuffer(OldInst *h, int32_t n, void *buf);
int      STDCALL ev_setParam(OldInst *h, int32_t which, int32_t value);
void     STDCALL eo_registerCallback(OldInst *h, void *cb, void *data);
void     STDCALL eo_synchronizeSynth(OldInst *h);
int      STDCALL eo_speaking(OldInst *h);
int      STDCALL eo_getAvailableLanguages(uint32_t *out, int *count);
void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

/* eciTextMode, which is what turns the annotations on. The add-on speaks with
   them on, so this does too. */
enum { PARAM_INPUT_TYPE = 1 };

static short frame[FRAME];
static long  said;

static const struct {
    int         kind;
    const char *what;
    const char *text;
} CASES[] = {
    { FREE, "at the sentence ends",
      "The release notes are up. |Column mode is new, and the sorting no"
      " longer breaks on an empty cell. |Reading a table now says the header"
      " first, which is the change most people asked for. |I will look at the"
      " rest next week. |Please tell me what else you find." },
    { FREE, "after a closing quotation mark",
      "He said \"the header is read first.\" |Then he closed the table and"
      " went home." },
    { COST, "every eighty characters at whitespace",
      "The release notes are up. Column mode is new, and the sorting no"
      " longer |breaks on an empty cell. Reading a table now says the header"
      " first, which |is the change most people asked for. I will look at the"
      " rest next week. |Please tell me what else you find." },
    { COST, "after a title",
      "Mr. |Jones asked whether the header is read first, and Mrs. |Adams"
      " said it is." },
    { COST, "at each initial",
      "The book by J. |R. |R. |Tolkien is on the shelf by the door." },
    { COST, "after a dotted abbreviation",
      "Use a smaller step, e.g. |two, and the sorting holds." },
};

enum { NCASES = (int)(sizeof CASES / sizeof CASES[0]) };

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

/* Speak these pieces, one utterance each, on an instance of their own.
   Answers the samples, or -1 where the engine would not take it. */
static long speak(char pieces[MAXPIECES][MAXTEXT], int n)
{
    uint32_t langs[32];
    int      count = 32;
    long     was = said;
    OldInst *h;
    int      i, k;

    if (eo_getAvailableLanguages(langs, &count) || count < 1)
        return -1;
    h = eo_newEx(langs[0]);
    if (h == 0)
        return -1;

    eo_registerCallback(h, (void *)on_message, 0);
    if (!ev_setOutputBuffer(h, FRAME, frame)) {
        es_delete(h);
        return -1;
    }
    ev_setParam(h, PARAM_INPUT_TYPE, 1);

    for (i = 0; i < n; i++) {
        if (!et_addText(h, pieces[i]) || !et_synthesize(h)) {
            es_delete(h);
            return -1;
        }
        /* Nothing drains the engine's message queue by itself; asking whether
           it is still speaking is what pumps it. */
        for (k = 0; k < 3000 && eo_speaking(h); k++)
            nap(5);
        eo_synchronizeSynth(h);
    }
    es_delete(h);
    return said - was;
}

/* The case's text as the pieces it names, and as one piece. */
static int cut(const char *text, char pieces[MAXPIECES][MAXTEXT],
               char whole[MAXPIECES][MAXTEXT])
{
    int n = 0, len = 0, w = 0;

    for (; *text; text++) {
        if (*text == '|') {
            pieces[n][len] = 0;
            n++;
            len = 0;
            continue;
        }
        pieces[n][len++] = *text;
        whole[0][w++] = *text;
    }
    pieces[n][len] = 0;
    whole[0][w] = 0;
    return n + 1;
}

int main(void)
{
    static char pieces[MAXPIECES][MAXTEXT], whole[MAXPIECES][MAXTEXT];
    int         c, bad = 0;

    evv_port_start();
    evvRunStaticInitialisers();

    for (c = 0; c < NCASES; c++) {
        int  n = cut(CASES[c].text, pieces, whole);
        long one = speak(whole, 1);
        long many = speak(pieces, n);

        if (one < 0 || many < 0) {
            printf("pieces: the engine would not speak %s\n", CASES[c].what);
            bad++;
            continue;
        }
        printf("pieces: %-38s %d pieces, %6ld against %6ld samples,"
               " %+.2f s\n", CASES[c].what, n, many, one,
               (double)(many - one) / RATE);
        if (CASES[c].kind == FREE && many != one) {
            /* A boundary the add-on would make has to be one the engine was
               going to make anyway. If this ever fails, it is the driver's
               rule that has to change, not this number. */
            printf("pieces: a boundary %s cost %ld samples, and the add-on"
                   " makes that boundary\n", CASES[c].what, many - one);
            bad++;
        }
    }

    evv_port_finish();
    if (bad) {
        printf("pieces: %d of %d cases wrong\n", bad, NCASES);
        return 1;
    }
    printf("pieces: %d cases, every boundary the add-on makes cost nothing\n",
           NCASES);
    return 0;
}
