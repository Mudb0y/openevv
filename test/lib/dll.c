/* Speak through the library, the way something else would.
 *
 * Nothing here links against the engine: it loads the library by name, asks
 * for each entry point by name, and calls through the pointers -- which is
 * exactly what a screen reader add-on does through ctypes, and what
 * speech-dispatcher's Eloquence module does through dlsym. So this checks the
 * two things a program using the library depends on and the ordinary tests
 * cannot see: that the published names are exported under those spellings,
 * and that a callback and a sample buffer survive the crossing.
 *
 * It does that on both kinds of library. `eci.dll' is the default name on
 * Windows and `./libeci.so' everywhere else, and EVV_ECI_LIB names another.
 * The version resource is a Windows question and is only asked there.
 *
 * It takes the same arguments as build/evv, so test/hash.sh can hold what the
 * library says against what everything else says.
 *
 * Text that begins with a `<' is read as SSML first, which is a third thing
 * a program depends on: that the filter interface is exported, that the
 * reader's own entry point is too, and that handing one to the other across
 * the library boundary works. With IBM's engine the entry point came out of
 * a DLL of its own; ours is in eci.dll, under the same name.
 *
 *   dlltest.exe -o out.wav "Some text."
 *   dlltest.exe -o out.wav "<speak version=\"1.0\" xml:lang=\"en-US\">Hi.</speak>"
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#define ECICALL     __stdcall
#define LIB_DEFAULT "eci.dll"
#define LIB_OPEN(p) ((void *)LoadLibraryA(p))
#define LIB_SYM(l, n) ((void *)GetProcAddress((HMODULE)(l), (n)))
#define LIB_WAIT(ms)  Sleep(ms)
#else
#include <dlfcn.h>
#include <unistd.h>
#define ECICALL
#define LIB_DEFAULT "./libeci.so"
#define LIB_OPEN(p) dlopen((p), RTLD_NOW)
#define LIB_SYM(l, n) dlsym((l), (n))
#define LIB_WAIT(ms)  usleep((ms) * 1000)
#endif

#define FRAME 2048

static short  frame[FRAME];
static short *samples;
static size_t nsamples, cap;

static void keep(const short *p, size_t n)
{
    if (nsamples + n > cap) {
        cap = (nsamples + n) * 2 + FRAME;
        samples = realloc(samples, cap * sizeof *samples);
        if (samples == 0)
            exit(1);
    }
    memcpy(samples + nsamples, p, n * sizeof *p);
    nsamples += n;
}

/* The add-on's callback is made with WINFUNCTYPE, which is stdcall; on x86-64
   that is the only convention there is, and off Windows there is nothing to
   say. The engine passes the count as a four-byte value whatever IBM's own
   header spells it, so this takes an int. */
static int ECICALL on_message(void *h, int msg, int param, void *data)
{
    (void)h;
    (void)data;
    if (msg == 0)               /* eciWaveformBuffer */
        keep(frame, (size_t)param);
    return 1;                   /* eciDataProcessed */
}

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

static int write_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    unsigned long bytes = (unsigned long)nsamples * 2;

    if (f == 0)
        return 0;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, 11025);
    put32(f, 11025 * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);
    fwrite(samples, 2, nsamples, f);
    fclose(f);
    return 1;
}

/* What the library says about itself, which is not decoration: the most used
   screen reader driver reads ProductName out of the version resource to decide
   which engine it is talking to, and NVDA's own reader raises rather than
   loading a file that has no version information at all. So the harness says
   what it finds, and finding nothing is a failure like any other. */
#if defined(_WIN32)
static int say_product(const char *path)
{
    unsigned long room = GetFileVersionInfoSizeA(path, 0);
    void         *info;
    void         *value = 0;
    unsigned int  bytes = 0;

    if (room == 0) {
        fprintf(stderr, "dlltest: %s has no version information; a driver that"
                " reads it will refuse to load this\n", path);
        return 0;
    }
    info = malloc(room);
    if (info == 0 || !GetFileVersionInfoA(path, 0, room, info)) {
        fprintf(stderr, "dlltest: cannot read %s's version information\n", path);
        return 0;
    }
    if (!VerQueryValueA(info, "\\StringFileInfo\\040904b0\\ProductName",
                        &value, &bytes) || bytes == 0) {
        fprintf(stderr, "dlltest: %s names no product\n", path);
        return 0;
    }
    printf("dlltest: %s says it is %s\n", path, (const char *)value);
    free(info);
    return 1;
}
#else
/* An ELF library has no version resource and no driver reads one out of it. */
static int say_product(const char *path) { (void)path; return 1; }
#endif

#define GET(name) \
    do { \
        name = (void *)LIB_SYM(dll, #name); \
        if (name == 0) { \
            fprintf(stderr, "dlltest: %s is not exported\n", #name); \
            return 1; \
        } \
    } while (0)

int main(int argc, char **argv)
{
    void   *dll;
    void   *h;
    const char *lib = getenv("EVV_ECI_LIB");
    const char *out = "out.wav";
    const char *text = "Hello. This is the Eloquence synthesizer speaking.";
    int     i;

    void *(ECICALL *eciNewEx)(int);
    int   (ECICALL *eciGetAvailableLanguages)(unsigned int *, int *);
    void  (ECICALL *eciRegisterCallback)(void *, void *, void *);
    int   (ECICALL *eciSetOutputBuffer)(void *, int, short *);
    int   (ECICALL *eciSetParam)(void *, int, int);
    int   (ECICALL *eciAddText)(void *, const char *);
    int   (ECICALL *eciInsertIndex)(void *, int);
    int   (ECICALL *eciSynthesize)(void *);
    int   (ECICALL *eciSpeaking)(void *);
    void *(ECICALL *eciDelete)(void *);
    int   (ECICALL *eciRegisterFilter)(void *, unsigned int, void *, void *, int);
    void *(ECICALL *eciNewFilter)(void *, int, int);
    int   (ECICALL *eciActivateFilter)(void *, void *);
    int   (ECICALL *eciGetFilteredText)(void *, void *, const void *, const void **);
    int   (ECICALL *ssmlFilterGetObject)(unsigned int, void **);

    unsigned int langs[32];
    int          nlangs = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            out = argv[++i];
        else
            text = argv[i];
    }

    if (lib == 0)
        lib = LIB_DEFAULT;

    if (!say_product(lib))
        return 1;

    dll = LIB_OPEN(lib);
    if (dll == 0) {
        fprintf(stderr, "dlltest: cannot load %s\n", lib);
        return 1;
    }

    GET(eciGetAvailableLanguages);
    GET(eciNewEx);
    GET(eciRegisterCallback);
    GET(eciSetOutputBuffer);
    GET(eciSetParam);
    GET(eciAddText);
    GET(eciInsertIndex);
    GET(eciSynthesize);
    GET(eciSpeaking);
    GET(eciDelete);
    GET(eciRegisterFilter);
    GET(eciNewFilter);
    GET(eciActivateFilter);
    GET(eciGetFilteredText);
    GET(ssmlFilterGetObject);

    /* Asked twice, as the add-on asks: with no room, which answers how many
       there are, and then with room. Asking for the count with room left over
       is a parameter error, which is what the original says too. */
    if (eciGetAvailableLanguages(0, &nlangs) || nlangs < 1) {
        fprintf(stderr, "dlltest: it has no language in it\n");
        return 1;
    }
    if (nlangs > 32)
        nlangs = 32;
    eciGetAvailableLanguages(langs, &nlangs);

    h = eciNewEx((int)langs[0]);
    if (h == 0) {
        fprintf(stderr, "dlltest: it would not make an instance\n");
        return 1;
    }

    eciRegisterCallback(h, (void *)on_message, 0);
    if (!eciSetOutputBuffer(h, FRAME, frame)) {
        fprintf(stderr, "dlltest: it refused a sample buffer\n");
        return 1;
    }
    eciSetParam(h, 1, 1);       /* annotations, as the add-on asks for */

    /* A document goes through the reader first, and what comes out of it is
       annotations, which is why the line above is not optional here. */
    if (text[0] == '<') {
        struct { char name[80]; int language; } attrib;
        void *entry = (void *)ssmlFilterGetObject;
        void *filter;
        const void *read = 0;
        char *writable;

        memset(&attrib, 0, sizeof attrib);
        if (eciRegisterFilter(h, 0, &entry, &attrib, 1) != 0) {
            fprintf(stderr, "dlltest: it would not register the SSML"
                    " filter\n");
            return 1;
        }
        printf("dlltest: registered [%s]\n", attrib.name);

        filter = eciNewFilter(h, 0, 1);
        if (filter == 0) {
            fprintf(stderr, "dlltest: it would not make the SSML filter\n");
            return 1;
        }
        eciActivateFilter(h, filter);

        /* A copy it may write on: the reader ends the digits of a numeric
           character reference by writing a NUL over the semicolon, in the
           document it was handed. */
        writable = malloc(strlen(text) + 1);
        if (writable == 0)
            return 1;
        strcpy(writable, text);
        if (eciGetFilteredText(h, filter, writable, &read) != 0 || read == 0) {
            fprintf(stderr, "dlltest: it would not read the document\n");
            return 1;
        }
        printf("dlltest: read as [%s]\n", (const char *)read);
        text = (const char *)read;
    }

    eciInsertIndex(h, 4242);
    if (!eciAddText(h, text) || !eciSynthesize(h)) {
        fprintf(stderr, "dlltest: it refused the text\n");
        return 1;
    }
    for (i = 0; i < 30000 && eciSpeaking(h); i++)
        LIB_WAIT(10);

    eciDelete(h);

    if (!write_wav(out)) {
        fprintf(stderr, "dlltest: cannot write %s\n", out);
        return 1;
    }
    printf("dlltest: %lu samples through %s to %s\n",
           (unsigned long)nsamples, lib, out);
    return 0;
}
