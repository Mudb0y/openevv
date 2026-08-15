/* Drive the whole engine and write what it says to a wave file.

   This links IBM's own objects and calls the published ECI interface, so
   what comes out is the engine speaking exactly as it always did. It exists
   for two reasons: to prove the engine can be driven at all outside its
   DLL, and to give the synthesizer something real to be fed. */

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
int __stdcall eciGetAvailableLanguages(unsigned *, int *);
ECIHand __stdcall eciNewEx(unsigned);
int __stdcall eciSpeaking(ECIHand);

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

int main(int argc, char **argv)
{
    const char *text = (argc > 1) ? argv[1]
        : "Hello. This is the Eloquence synthesizer speaking.";
    const char *out = (argc > 2) ? argv[2] : "speak.wav";
    ECIHand h;

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

        h = eciNew();
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

    printf("speak: %lu samples\n", (unsigned long)nsamples);
    if (nsamples == 0) {
        printf("speak: nothing came out\n");
        eciDelete(h);
        return 1;
    }

    write_wav(out, 11025);
    printf("speak: wrote %s\n", out);

    eciDelete(h);
    return 0;
}
