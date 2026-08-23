/* Drive the engine on the machine this is built for, write what it says to a
   wave file, and print what it answered on the way.

   This is what the tests drive. Every line it prints is a line test/suite.sh
   can hold against the same line from IBM's own binary, which is why it says
   so much and why cli/evv.c exists instead for anyone who only wants the
   audio. Both write the same samples.

   reference/speak.c drives IBM's engine and prints the same lines. It calls
   the published ECI names, because it is linked beside IBM's objects and
   those are the names they answer to; nothing here is linked beside anything,
   so this calls our own directly. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "evv_abi.h"

typedef struct OldInst OldInst;

enum ECIMessage {
    eciWaveformBuffer,
    eciPhonemeBuffer,
    eciIndexReply,
    eciPhonemeIndexReply,
    eciWordIndexReply,
    eciStringIndexReply,
    eciAudioIndexReply,
    eciSynthesisBreak
};

enum ECICallbackReturn {
    eciDataNotProcessed,
    eciDataProcessed,
    eciDataAbort
};

OldInst *STDCALL eo_new(void);
OldInst *STDCALL eo_newEx(int32_t language);
int      STDCALL es_delete(OldInst *h);
int      STDCALL et_addText(OldInst *h, const char *text);
int      STDCALL et_synthesize(OldInst *h);
int      STDCALL et_insertIndex(OldInst *h, int32_t n);
int      STDCALL ev_setOutputBuffer(OldInst *h, int32_t n, void *buf);
int             ev_setOutputToPhonemeCallback(OldInst *h, int32_t n,
                                              void *buf);
int32_t  STDCALL ev_setParam(OldInst *h, int32_t which, int32_t value);
int32_t  STDCALL eo_getParam(OldInst *h, int32_t which);
int32_t  STDCALL vc_getVoiceParam(OldInst *h, int32_t voice, int32_t which);
void     STDCALL eo_registerCallback(OldInst *h, void *cb, void *data);
void     STDCALL eo_synchronizeSynth(OldInst *h);
void    *STDCALL ed_newDict(OldInst *h);
int      STDCALL ed_setDict(OldInst *h, void *dict);
void    *STDCALL ed_getDict(OldInst *h);
int      STDCALL ed_loadDict(OldInst *h, void *dict, int32_t which,
                             const char *name);
int      STDCALL ed_deleteDict(OldInst *h, void *dict);
int      STDCALL eo_speaking(OldInst *h);
int      STDCALL eo_getAvailableLanguages(uint32_t *out, int *count);

void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

#define FRAME 2048

static short  frame[FRAME];
static short *samples;
/* Where the engine puts the phonemes it places when it is asked for those
   rather than samples: each is a name packed into a word and a length in
   milliseconds. */
static int32_t phonemes[2048];
static int     phonemes_wanted;
static size_t nsamples;
static size_t cap;

static void keep(const short *p, size_t n)
{
    if (nsamples + n > cap) {
        cap = (nsamples + n) * 2 + FRAME;
        samples = realloc(samples, cap * sizeof(*samples));
        if (samples == NULL) {
            fprintf(stderr, "speak: out of memory\n");
            exit(1);
        }
    }
    memcpy(samples + nsamples, p, n * sizeof(*p));
    nsamples += n;
}

static enum ECICallbackReturn STDCALL on_message(OldInst *h,
                                                 enum ECIMessage msg,
                                                 long param, void *data)
{
    (void)h;
    (void)data;

    if (msg == eciPhonemeBuffer) {
        long i;

        /* A name is four characters packed into a word and is not
           nul-terminated when all four are used, so it is printed by
           length rather than as a string. */
        for (i = 0; i < param; i++) {
            const char *nm = (const char *)&phonemes[i * 2];
            int j;

            printf("speak: phoneme ");
            for (j = 0; j < 4 && nm[j] != 0; j++)
                putchar(nm[j]);
            printf(" %d ms\n", (int)phonemes[i * 2 + 1]);
        }
    } else if (msg == eciWaveformBuffer)
        keep(frame, (size_t)param);
    else if (msg == eciIndexReply)
        printf("speak: index %ld\n", param);

    return eciDataProcessed;
}

/* Written a byte at a time so nothing depends on how this machine lays a
   structure out. */
static void put32(FILE *f, unsigned long v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
    fputc((int)((v >> 16) & 0xff), f);
    fputc((int)((v >> 24) & 0xff), f);
}

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
}

static void write_wav(const char *path, unsigned long rate)
{
    FILE         *f = fopen(path, "wb");
    unsigned long bytes = (unsigned long)nsamples * 2;

    if (f == NULL) {
        fprintf(stderr, "speak: cannot write %s\n", path);
        exit(1);
    }

    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, rate);
    put32(f, rate * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);
    fwrite(samples, 2, nsamples, f);
    fclose(f);
}

static void nap(long ms)
{
#if defined(_WIN32)
    Sleep((unsigned long)(ms < 0 ? 0 : ms));
#else
    struct timespec t;

    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
#endif
}

/* A case may hold bytes the command line cannot carry unchanged: Wine
   turns the UTF-8 line it is given into the process's ANSI code page
   before main sees it, and a native run gets the bytes as they were. So a
   text argument beginning with an at sign names a file to read instead,
   and both builds then see exactly the same bytes. */
static char *slurp(const char *path)
{
    FILE  *f = fopen(path, "rb");
    char  *buf;
    long   n;

    if (f == NULL) {
        fprintf(stderr, "speak: cannot read %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "speak: cannot read %s\n", path);
        exit(1);
    }
    fclose(f);
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = 0;
    return buf;
}

int main(int argc, char **argv)
{
    const char *text = (argc > 1) ? argv[1]
        : "Hello. This is the Eloquence synthesizer speaking.";
    const char *out = (argc > 2) ? argv[2] : "speak.wav";
    OldInst    *h;

    if (text[0] == '@')
        text = slurp(text + 1);

    setvbuf(stdout, NULL, _IONBF, 0);
    evv_port_start();
    evvRunStaticInitialisers();

    {
        uint32_t langs[32];
        int      n = 32;
        int      i;

        if (eo_getAvailableLanguages(langs, &n))
            printf("speak: getAvailableLanguages refused\n");
        printf("speak: %d languages\n", n);
        for (i = 0; i < n && i < 32; i++)
            printf("speak:   language 0x%x\n", langs[i]);

        /* A build may have more than one language in it, and the
           tests want to drive each of them through the same binary.
           EVV_LANGUAGE names which, as the number the API uses; with
           nothing set the engine picks, which is the first one linked. */
        {
            const char *want = getenv("EVV_LANGUAGE");

            if (want != NULL && *want != 0)
                h = eo_newEx((uint32_t)strtoul(want, NULL, 0));
            else
                h = eo_new();
        }
        if (h == NULL && n > 0)
            h = eo_newEx(langs[0]);
        if (h == NULL)
            printf("speak: the engine would not build an instance\n");
    }
    if (h == NULL)
        return 1;

    /* The callback first: the engine will not take a sample buffer until it
       has somewhere to report the samples to. */
    eo_registerCallback(h, (void *)on_message, NULL);
    if (!ev_setOutputBuffer(h, FRAME, frame)) {
        printf("speak: setOutputBuffer refused\n");
        return 1;
    }

    /* A p asks for phonemes instead of sound: what the language decided the
       words are made of, under the names its own statement table gives them.
       Nothing is written to the wave file in that mode.
     *
     * It does not report anything yet, and what is missing is written down
     * rather than guessed at. The engine places its phonemes -- placePhoneme
     * in src/eci_deltacb.c is reached, five times for one short word -- and
     * returns at once because ELOQ_WANT_PHONEMES is nought. Registering the
     * buffer sets the thread state, parameter four sets the flag through
     * setPhonemeIndiciesRun, and something puts it back before the utterance:
     * es_setCurrentState sends espr0 when the state says the engine is not in
     * phoneme mode, and the text path sends the same on a fresh utterance.
     * disptok, which spells a token and was an empty stub, is written now, so
     * the names will be there when the flag stays. */
    if (argc > 3 && strchr(argv[3], 'p')) {
        if (!ev_setOutputToPhonemeCallback(h, (int32_t)(sizeof phonemes
                                                        / sizeof phonemes[0]),
                                           phonemes)) {
            printf("speak: it would not report phonemes\n");
            return 1;
        }
        /* And the parameter that says the caller wants to be told: without
           it the engine places its phonemes and reports none of them. */
        if (ev_setParam(h, 4, 1) < 0)
            printf("speak: it would not report phoneme indices\n");
        phonemes_wanted = 1;
    }

    if (argc > 3 && strchr(argv[3], 'a')) {
        if (ev_setParam(h, 1, 1) < 0)
            printf("speak: setParam refused\n");
    }
    if (argc > 3 && strchr(argv[3], 'r')) {
        if (ev_setParam(h, 8, 1) < 0)
            printf("speak: setParam refused\n");
    }

    /* A d walks the dictionary layer, which nothing else here reaches.
       Whether the engine accepts any of it is beside the point; what is
       compared is that both builds answer the same way. */
    if (argc > 3 && strchr(argv[3], 'd')) {
        void *dict = ed_newDict(h);

        printf("speak: newDict %s\n", dict ? "made" : "refused");
        printf("speak: setDict %d\n", ed_setDict(h, dict));
        printf("speak: getDict %s\n", ed_getDict(h) == dict ? "same"
                                                             : "other");
        printf("speak: loadDict %d\n", ed_loadDict(h, dict, 0, "x"));
        printf("speak: setDict none %d\n", ed_setDict(h, NULL));
        printf("speak: deleteDict %d\n", ed_deleteDict(h, dict));
    }

    if (!et_insertIndex(h, 4242))
        printf("speak: insertIndex refused\n");

    if (!et_addText(h, text)) {
        printf("speak: addText refused\n");
        return 1;
    }
    if (!et_synthesize(h)) {
        printf("speak: synthesize refused\n");
        return 1;
    }

    /* Nothing drains the engine's message queue by itself; asking whether it
       is still speaking is what pumps it, so keep asking. */
    {
        int i;

        for (i = 0; i < 3000 && eo_speaking(h); i++)
            nap(10);
    }

    if (argc > 3) {
        int i;

        for (i = 0; i < 8; i++)
            printf("speak: voice param %d = %d\n", i,
                   vc_getVoiceParam(h, 0, i));
        for (i = 0; i < 17; i++)
            printf("speak: param %d = %d\n", i, eo_getParam(h, i));
    }

    eo_synchronizeSynth(h);
    /* A t says the same thing again on the same instance and writes it beside
       the first, which is what the reference's own t does, so the two can be
       held against each other. Every case the suite compares is the first
       utterance of a fresh process; this is how a second one gets compared. */
    if (argc > 3 && strchr(argv[3], 't')) {
        size_t first = nsamples;
        char again[1024];
        int i;

        snprintf(again, sizeof again, "%s.again.wav", out);
        write_wav(out, 11025);
        printf("speak: %lu samples to %s\n", (unsigned long)nsamples, out);

        nsamples = 0;
        if (!et_addText(h, text) || !et_synthesize(h)) {
            printf("speak: the second utterance was refused\n");
        } else {
            for (i = 0; i < 3000 && eo_speaking(h); i++)
                nap(10);
            write_wav(again, 11025);
            printf("speak: %lu samples to %s\n",
                   (unsigned long)nsamples, again);
        }
        printf("speak: first %lu, second %lu\n",
               (unsigned long)first, (unsigned long)nsamples);
        es_delete(h);
        evv_port_finish();
        return 0;
    }

    es_delete(h);
    evv_port_finish();

    write_wav(out, 11025);
    printf("speak: %lu samples to %s\n", (unsigned long)nsamples, out);
    return 0;
}
