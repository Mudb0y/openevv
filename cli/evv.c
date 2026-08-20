/* Speak some text with the engine built for this machine.
 *
 * This is the command a person runs. It writes a wave file, or writes the
 * wave to standard output so it can be piped into a player, because nothing
 * in the engine sends samples to a sound card yet.
 *
 * The driver the tests drive is cli/probe.c, not this. That one prints what
 * the engine answered at every step so those answers can be set against
 * IBM's binary; this one keeps quiet and makes audio.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <unistd.h>

#include "evv_abi.h"

typedef struct OldInst OldInst;

enum ECIMessage {
    eciWaveformBuffer,
    eciPhonemeBuffer,
    eciIndexReply
};

enum ECICallbackReturn {
    eciDataNotProcessed,
    eciDataProcessed,
    eciDataAbort
};

/* The engine's own parameters, and a voice's. Only the few this needs. */
enum { P_REAL_WORLD_UNITS = 8 };
enum { V_GENDER, V_HEAD_SIZE, V_PITCH, V_FLUCTUATION, V_ROUGHNESS,
       V_BREATHINESS, V_SPEED, V_VOLUME, V_COUNT };

static const char *const voice_param_name[V_COUNT] = {
    "gender", "head size", "pitch", "fluctuation",
    "roughness", "breathiness", "speed", "volume"
};

OldInst *STDCALL eo_new(void);
OldInst *STDCALL eo_newEx(int32_t language);
int      STDCALL es_delete(OldInst *h);
int      STDCALL et_addText(OldInst *h, const char *text);
int      STDCALL et_synthesize(OldInst *h);
int      STDCALL ev_setOutputBuffer(OldInst *h, int32_t n, void *buf);
int32_t  STDCALL ev_setParam(OldInst *h, int32_t which, int32_t value);
int32_t  STDCALL vc_getVoiceParam(OldInst *h, int32_t voice, int32_t which);
int      STDCALL vc_setVoiceParam(OldInst *h, int32_t voice, int32_t which,
                                  int32_t value);
int      STDCALL vc_copyVoice(OldInst *h, int32_t from, int32_t to);
void     STDCALL eo_registerCallback(OldInst *h, void *cb, void *data);
void     STDCALL eo_synchronizeSynth(OldInst *h);
int      STDCALL eo_speaking(OldInst *h);
int      STDCALL eo_getAvailableLanguages(uint32_t *out, int *count);

void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

/* The formant voice runs at eleven thousand and twenty-five samples a second
   and nothing here changes that. The engine's sample rate parameter belongs
   to the concatenative voices, which this extraction does not have. */
#define RATE  11025
#define FRAME 2048

static short  frame[FRAME];
static short *samples;
static size_t nsamples;
static size_t cap;

static void die(const char *what)
{
    fprintf(stderr, "evv: %s\n", what);
    exit(1);
}

static void keep(const short *p, size_t n)
{
    if (nsamples + n > cap) {
        cap = (nsamples + n) * 2 + FRAME;
        samples = realloc(samples, cap * sizeof(*samples));
        if (samples == NULL)
            die("out of memory");
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

    if (msg == eciWaveformBuffer)
        keep(frame, (size_t)param);
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

static void write_wav(FILE *f)
{
    unsigned long bytes = (unsigned long)nsamples * 2;

    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, RATE);
    put32(f, RATE * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);
    fwrite(samples, 2, nsamples, f);
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

static char *slurp(FILE *f)
{
    size_t n = 0, room = 4096;
    char  *buf = malloc(room);

    if (buf == NULL)
        die("out of memory");
    for (;;) {
        size_t got = fread(buf + n, 1, room - n - 1, f);

        n += got;
        if (n + 1 < room)
            break;
        room *= 2;
        buf = realloc(buf, room);
        if (buf == NULL)
            die("out of memory");
    }
    buf[n] = 0;
    return buf;
}

static char *join(int argc, char **argv)
{
    size_t n = 0;
    int    i;
    char  *out;

    for (i = 0; i < argc; i++)
        n += strlen(argv[i]) + 1;
    out = malloc(n + 1);
    if (out == NULL)
        die("out of memory");
    out[0] = 0;
    for (i = 0; i < argc; i++) {
        if (i)
            strcat(out, " ");
        strcat(out, argv[i]);
    }
    return out;
}

static void usage(FILE *f)
{
    fprintf(f,
"usage: evv [options] [text ...]\n"
"\n"
"Speaks the text and writes the audio as a wave file. With no text, reads\n"
"it from standard input; with no -o, writes the wave to standard output\n"
"when that is not a terminal.\n"
"\n"
"  -o FILE   write the wave there, or to standard output for -\n"
"  -f FILE   read the text from there, or from standard input for -\n"
"  -v N      speak in voice N, 1 to 8\n"
"  -s N      speed\n"
"  -p N      pitch\n"
"  -V N      volume\n"
"  -r        take every number above in a person's units instead of the\n"
"            engine's: words per minute for speed, hertz for pitch\n"
"  -l        say what each voice is set to, and stop\n"
"  -h        this\n"
"\n"
"  evv -o hello.wav \"Hello from Eloquence.\"\n"
"  evv \"Hello from Eloquence.\" | aplay -q -\n");
}

int main(int argc, char **argv)
{
    const char *out = NULL, *from = NULL;
    int         voice = 0, real = 0, list = 0;
    int         set[V_COUNT];
    char       *text;
    OldInst    *h;
    FILE       *f;
    int         i;

    for (i = 0; i < V_COUNT; i++)
        set[i] = -1;

    while ((i = getopt(argc, argv, "o:f:v:s:p:V:rlh")) != -1) {
        switch (i) {
        case 'o': out = optarg; break;
        case 'f': from = optarg; break;
        case 'v': voice = atoi(optarg); break;
        case 's': set[V_SPEED] = atoi(optarg); break;
        case 'p': set[V_PITCH] = atoi(optarg); break;
        case 'V': set[V_VOLUME] = atoi(optarg); break;
        case 'r': real = 1; break;
        case 'l': list = 1; break;
        case 'h': usage(stdout); return 0;
        default:  usage(stderr); return 2;
        }
    }

    if (voice < 0 || voice > 8) {
        fprintf(stderr, "evv: there is no voice %d; they are 1 to 8\n", voice);
        return 2;
    }

    if (list)
        text = NULL;
    else if (from != NULL) {
        if (strcmp(from, "-") == 0)
            text = slurp(stdin);
        else {
            FILE *in = fopen(from, "rb");

            if (in == NULL) {
                fprintf(stderr, "evv: cannot read %s\n", from);
                return 1;
            }
            text = slurp(in);
            fclose(in);
        }
    } else if (optind < argc)
        text = join(argc - optind, argv + optind);
    else if (!isatty(0))
        text = slurp(stdin);
    else {
        usage(stderr);
        return 2;
    }

    /* Where the wave goes is settled before the engine starts, so a mistake
       in it costs nothing. Standard output only when it is not a terminal:
       a wave file down a terminal is a wasted minute and a lot of noise. */
    if (list)
        f = NULL;
    else if (out == NULL || strcmp(out, "-") == 0) {
        if (out == NULL && isatty(1)) {
            fprintf(stderr, "evv: name a file with -o, or pipe me into a "
                            "player\n");
            return 2;
        }
        f = stdout;
    } else {
        f = fopen(out, "wb");
        if (f == NULL) {
            fprintf(stderr, "evv: cannot write %s\n", out);
            return 1;
        }
    }

    evv_port_start();
    evvRunStaticInitialisers();

    {
        uint32_t langs[32];
        int      n = 32;

        if (eo_getAvailableLanguages(langs, &n) || n < 1)
            die("the engine has no language in it");
        h = eo_new();
        if (h == NULL)
            h = eo_newEx(langs[0]);
        if (h == NULL)
            die("the engine would not build an instance");
    }

    /* A person's units are a parameter of the engine, not of the voice, and
       it has to be on before a voice setting is read or written in them. */
    if (real && ev_setParam(h, P_REAL_WORLD_UNITS, 1) < 0)
        die("this engine will not answer in a person's units");

    if (voice > 0 && !vc_copyVoice(h, voice, 0)) {
        fprintf(stderr, "evv: the engine refused voice %d\n", voice);
        return 1;
    }

    if (list) {
        int v;

        printf("%s\n", real ? "in a person's units:"
                            : "in the engine's own units:");
        for (v = 1; v <= 8; v++) {
            printf("voice %d:", v);
            for (i = 0; i < V_COUNT; i++)
                printf(" %s %d", voice_param_name[i],
                       vc_getVoiceParam(h, v, i));
            printf("\n");
        }
        es_delete(h);
        evv_port_finish();
        return 0;
    }

    for (i = 0; i < V_COUNT; i++)
        if (set[i] >= 0 && !vc_setVoiceParam(h, 0, i, set[i]))
            fprintf(stderr, "evv: the engine refused %s %d\n",
                    voice_param_name[i], set[i]);

    /* The callback first: the engine will not take a sample buffer until it
       has somewhere to report the samples to. */
    eo_registerCallback(h, (void *)on_message, NULL);
    if (!ev_setOutputBuffer(h, FRAME, frame))
        die("the engine refused a sample buffer");

    if (!et_addText(h, text))
        die("the engine refused the text");
    if (!et_synthesize(h))
        die("the engine refused to speak");

    /* Nothing drains the engine's message queue by itself; asking whether it
       is still speaking is what pumps it, so keep asking. */
    for (i = 0; i < 30000 && eo_speaking(h); i++)
        nap(10);

    eo_synchronizeSynth(h);
    es_delete(h);
    evv_port_finish();

    write_wav(f);
    if (f != stdout)
        fclose(f);
    else
        fflush(f);
    if (nsamples == 0) {
        fprintf(stderr, "evv: the engine said nothing\n");
        return 1;
    }
    return 0;
}
