/* Drive IBM's own engine and write what it says to a wave file.

   This links IBM's objects and calls the published ECI interface, so what
   comes out is the engine speaking exactly as it always did. That is what
   makes it the reference: test/suite.sh speaks every case through this and
   through cli/probe.c, which prints the same lines, and a case passes only
   when the samples and the lines both agree. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *ECIHand;

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

typedef enum ECICallbackReturn (__stdcall *ECICallback)(ECIHand,
                                                        enum ECIMessage,
                                                        long, void *);

ECIHand __stdcall eciNew(void);
ECIHand __stdcall eciDelete(ECIHand);
int __stdcall eciAddText(ECIHand, const char *);
int __stdcall eciSynthesize(ECIHand);
int __stdcall eciSynchronizeSynth(ECIHand);
int __stdcall eciSetOutputBuffer(ECIHand, int, short *);
void __stdcall eciRegisterCallback(ECIHand, ECICallback, void *);
int __stdcall eciSetParam(ECIHand, int, int);
int __stdcall eciGetVoiceParam(ECIHand, int, int);
int __stdcall eciGetParam(ECIHand, int);
void *__stdcall eciNewDict(ECIHand);
int __stdcall eciSetDict(ECIHand, void *);
void *__stdcall eciGetDict(ECIHand);
int __stdcall eciDeleteDict(ECIHand, void *);
int __stdcall eciLoadDict(ECIHand, void *, int, const char *);
int __stdcall eciGetAvailableLanguages(unsigned *, int *);
ECIHand __stdcall eciNewEx(unsigned);
int __stdcall eciSpeaking(ECIHand);
int __stdcall eciInsertIndex(ECIHand, int);

void evvRunStaticInitialisers(void);

#define FRAME 2048

static short frame[FRAME];
static short *samples;
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

static enum ECICallbackReturn __stdcall on_message(ECIHand h,
                                                   enum ECIMessage msg,
                                                   long param, void *data)
{
    (void)h;
    (void)data;

    if (msg == eciWaveformBuffer)
        keep(frame, (size_t)param);
    else if (msg == eciIndexReply)
        printf("speak: index %ld\n", param);

    return eciDataProcessed;
}

/* A wave file, written a byte at a time so nothing depends on how this
   machine lays a structure out. */
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
    FILE *f = fopen(path, "wb");
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
    ECIHand h;

    if (text[0] == '@')
        text = slurp(text + 1);

    setvbuf(stdout, NULL, _IONBF, 0);
    evvRunStaticInitialisers();

    {
        unsigned langs[32];
        int n = 32;
        int i;

        if (eciGetAvailableLanguages(langs, &n))
            printf("speak: eciGetAvailableLanguages refused\n");
        printf("speak: %d languages\n", n);
        for (i = 0; i < n && i < 32; i++)
            printf("speak:   language 0x%x\n", langs[i]);

        /* Which language to make the instance for. eciNew is not the same
           as eciNewEx with the only language a module has: for Japanese it
           answers an instance that speaks nothing at all, because the
           language handed over at creation is what carries the codeset. So
           the variable cli/probe.c already reads is read here too, and both
           sides make the instance the same way. */
        {
            const char *want = getenv("EVV_LANGUAGE");

            if (want != NULL && *want != 0)
                h = eciNewEx((unsigned)strtoul(want, NULL, 0));
            else
                h = eciNew();
        }
        if (h == NULL && n > 0)
            h = eciNewEx(langs[0]);
        if (h == NULL)
            printf("speak: the engine would not build an instance\n");
    }
    if (h == NULL)
        return 1;

    /* The callback first: the engine will not take a sample buffer until it
       has somewhere to report the samples to. */
    eciRegisterCallback(h, on_message, NULL);
    if (!eciSetOutputBuffer(h, FRAME, frame)) {
        printf("speak: eciSetOutputBuffer refused\n");
        return 1;
    }

    /* A third argument turns the annotation input type on, which is the
       only way to reach the layer that reads annotations before the engine
       does. Without it that whole path is never walked. */
    if (argc > 3 && strchr(argv[3], 'a')) {
        if (eciSetParam(h, 1, 1) < 0)
            printf("speak: eciSetParam refused\n");
    }

    /* And an r asks for the parameters in a person's units rather than the
       engine's, which is the only way the second copy each voice keeps of
       its pitch, speed and volume is ever read. */
    if (argc > 3 && strchr(argv[3], 'r')) {
        if (eciSetParam(h, 8, 1) < 0)
            printf("speak: eciSetParam refused\n");
    }

    /* A d walks the dictionary layer, which nothing else here reaches.
       Whether the engine accepts any of it is beside the point; what is
       compared is that both builds answer the same way. */
    if (argc > 3 && strchr(argv[3], 'd')) {
        void *dict = eciNewDict(h);

        printf("speak: newDict %s\n", dict ? "made" : "refused");
        printf("speak: setDict %d\n", eciSetDict(h, dict));
        printf("speak: getDict %s\n", eciGetDict(h) == dict ? "same"
                                                            : "other");
        printf("speak: loadDict %d\n", eciLoadDict(h, dict, 0, "x"));
        printf("speak: setDict none %d\n", eciSetDict(h, NULL));
        printf("speak: deleteDict %d\n", eciDeleteDict(h, dict));
    }

    /* An index mark in the middle of the text, so that the path that
       reports one back is walked at all. */
    if (!eciInsertIndex(h, 4242))
        printf("speak: eciInsertIndex refused\n");

    if (!eciAddText(h, text)) {
        printf("speak: eciAddText refused\n");
        return 1;
    }
    if (!eciSynthesize(h)) {
        printf("speak: eciSynthesize refused\n");
        return 1;
    }
    /* The synthesis thread hands its results back through the engine's own
       message queue, and nothing drains that queue by itself. Asking whether
       it is still speaking is what pumps it, so keep asking. */
    {
        int i;

        for (i = 0; i < 3000 && eciSpeaking(h); i++)
            Sleep(10);
    }

    /* In annotation mode, show what the annotations left behind in the
       instance's own records. The engine acts on the annotations itself, so
       this is the only place their effect on those records shows. */
    if (argc > 3) {
        int i;

        for (i = 0; i < 8; i++)
            printf("speak: voice param %d = %d\n", i,
                   eciGetVoiceParam(h, 0, i));
        for (i = 0; i < 17; i++)
            printf("speak: param %d = %d\n", i, eciGetParam(h, i));
    }

    printf("speak: %lu samples\n", (unsigned long)nsamples);
    if (nsamples == 0) {
        printf("speak: nothing came out\n");
        eciDelete(h);
        return 1;
    }

    write_wav(out, 11025);
    printf("speak: wrote %s\n", out);

    /* A t says the same thing again on the same instance and writes it
       beside the first, which is the only way to ask whether the engine
       repeats itself. Ours does not; the question is whether IBM's does,
       because every case the suite compares is the first utterance of a
       fresh process and a second one has never been held against anything. */
    if (argc > 3 && strchr(argv[3], 't')) {
        char again[1024];
        size_t first = nsamples;

        snprintf(again, sizeof again, "%s.again.wav", out);
        nsamples = 0;
        if (!eciAddText(h, text) || !eciSynthesize(h))
            printf("speak: the second utterance was refused\n");
        else {
            /* Pumped the same way the first one is: asking whether it is
               still speaking is what drains the queue, and synchronizeSynth
               does nothing here. Driving the two differently is what made
               the first attempt at this say the engine had gone silent. */
            int i;

            for (i = 0; i < 3000 && eciSpeaking(h); i++)
                Sleep(10);
            printf("speak: %lu samples the second time\n",
                   (unsigned long)nsamples);
            if (nsamples != 0) {
                write_wav(again, 11025);
                printf("speak: wrote %s\n", again);
            }
        }
        printf("speak: first %lu, second %lu\n",
               (unsigned long)first, (unsigned long)nsamples);
    }

    eciDelete(h);
    return 0;
}
