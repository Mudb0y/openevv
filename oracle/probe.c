/* Interposed engine internals, for finding out where construction gives up.

   The engine reports almost nothing on its own trace log when an instance
   fails to build, so the few steps that decide it are wrapped here: each
   object's own definition is renamed out of the way and these take the
   original names, print what was asked and what was answered, and pass the
   call straight through. See the Makefile for the renaming.

   Nothing here changes what the engine does. */

#include <stdio.h>

int __stdcall ibm_eciNew2(void *out, unsigned language);
int __stdcall ibm_eciSetOutputDevice(void *inst, int device);
int __stdcall ibm_eciGetParam2(void *h, int a, int b, void *out);
int __stdcall ibm_eciNewAudioFormat2(void *h, void *fmt);
int __stdcall ibm_eciRegisterSampleBuffer2(void *h, int a, void *b, int c);
int __stdcall ibm_eciRegisterPhonemeBuffer2(void *h, int a, void *b, int c);
void *ibm_soundFileName(void *sf, const char *name);
int ibm_soundFileSetDevice(void *sf, int dev, int arg);
int ibm_soundFileSetFile(void *sf, const char *name, int arg);
void ibm_output_speech(void *h, long n);
int __stdcall ibm_eciAddText2(void *h, void *a, void *b, void *c, void *d, void *e);
int __stdcall ibm_eciSynthesize2(void *h);
int __stdcall ibm_eciSynchronize2(void *h);
int ibm_insertPhoneme(void *a, int b, int c, int d);
int ibm_synthesize(void *a, int b, int c, int d);
void *__attribute__((thiscall)) ibm_getEngine(void *self, const void *lang);
char *__attribute__((thiscall)) ibm_filterText(void *self, const char *t, long n);
void *__attribute__((thiscall)) ibm_getEngineData(void *self, const void *lang);
int __attribute__((thiscall)) ibm_romAddText(void *self, const char *t, int a, int b);
int __stdcall ibm_engsynProcessSentences(void *e, const char *t);
int __stdcall ibm_engsynFlush(void *e, int a);
void *ibm_delta_new(void);

static void say(const char *what, int result)
{
    fprintf(stderr, "probe: %s answered %d\n", what, result);
    fflush(stderr);
}

int __stdcall eciNew2(void *out, unsigned language)
{
    int r = ibm_eciNew2(out, language);

    say("eciNew2", r);
    return r;
}

int __stdcall eciGetParam2(void *h, int a, int b, void *out)
{
    int r = ibm_eciGetParam2(h, a, b, out);

    fprintf(stderr, "probe: eciGetParam2(%p, %d, %d) answered %d\n",
            h, a, b, r);
    fflush(stderr);
    return r;
}

int __stdcall eciSetOutputDevice(void *inst, int device)
{
    int r = ibm_eciSetOutputDevice(inst, device);

    fprintf(stderr, "probe: eciSetOutputDevice(device %d) answered %d\n",
            device, r);
    fflush(stderr);
    return r;
}

int __stdcall eciNewAudioFormat2(void *h, void *fmt)
{
    int r = ibm_eciNewAudioFormat2(h, fmt);

    say("eciNewAudioFormat2", r);
    return r;
}

int __stdcall eciRegisterSampleBuffer2(void *h, int a, void *b, int c)
{
    int r = ibm_eciRegisterSampleBuffer2(h, a, b, c);

    say("eciRegisterSampleBuffer2", r);
    return r;
}

int __stdcall eciRegisterPhonemeBuffer2(void *h, int a, void *b, int c)
{
    int r = ibm_eciRegisterPhonemeBuffer2(h, a, b, c);

    say("eciRegisterPhonemeBuffer2", r);
    return r;
}

static void dump(const char *what, const int *f)
{
    fprintf(stderr, "probe: %s channels %d rate %d bits %d buffers %d"
            " size %d preroll %d/%d\n", what, f[1], f[4], f[5], f[11], f[12],
            f[13], f[14]);
    fflush(stderr);
}

int soundFileSetDevice(void *sf, int dev, int arg)
{
    int r;

    dump("soundFileSetDevice asked", sf);
    r = ibm_soundFileSetDevice(sf, dev, arg);
    say("soundFileSetDevice", r);
    return r;
}

int soundFileSetFile(void *sf, const char *name, int arg)
{
    int r;

    dump("soundFileSetFile asked", sf);
    r = ibm_soundFileSetFile(sf, name, arg);
    say("soundFileSetFile", r);
    return r;
}

void *soundFileName(void *sf, const char *name)
{
    const int *f = sf;
    void *r;

    fprintf(stderr, "probe: soundFileName(\"%s\") channels %d rate %d bits %d"
            " buffers %d size %d\n",
            name ? name : "(none)", f[1], f[4], f[5], f[11], f[12]);
    r = ibm_soundFileName(sf, name);
    fprintf(stderr, "probe: soundFileName answered %p, format %p\n", r,
            (void *)(size_t)f[3]);
    fflush(stderr);
    return r;
}

static long spoken;

void evv_output_speech(void *h, long n)
{
    spoken += n;
    if (spoken == n || (spoken % 10000) < n)
        fprintf(stderr, "probe: output_speech %ld samples so far\n", spoken);
    fflush(stderr);
    ibm_output_speech(h, n);
}

int __stdcall eciAddText2(void *h, void *a, void *b, void *c, void *d, void *e)
{
    int r = ibm_eciAddText2(h, a, b, c, d, e);

    say("eciAddText2", r);
    return r;
}

int __stdcall eciSynthesize2(void *h)
{
    int r = ibm_eciSynthesize2(h);

    say("eciSynthesize2", r);
    return r;
}

int __stdcall eciSynchronize2(void *h)
{
    int r = ibm_eciSynchronize2(h);

    say("eciSynchronize2", r);
    return r;
}

static int phonemes;

int insertPhoneme(void *a, int b, int c, int d)
{
    if (phonemes++ < 12)
        fprintf(stderr, "probe: insertPhoneme %d (%d, %d, %d)\n",
                phonemes, b, c, d);
    fflush(stderr);
    return ibm_insertPhoneme(a, b, c, d);
}

int synthesize(void *a, int b, int c, int d)
{
    int r = ibm_synthesize(a, b, c, d);

    fprintf(stderr, "probe: synthesize answered %d after %d phonemes\n",
            r, phonemes);
    fflush(stderr);
    return r;
}

void *__attribute__((thiscall)) evv_getEngine(void *self, const void *lang)
{
    void *r = ibm_getEngine(self, lang);

    fprintf(stderr, "probe: EngineArray::getEngine answered %p\n", r);
    fflush(stderr);
    return r;
}

char *__attribute__((thiscall)) evv_filterText(void *self, const char *t,
                                               long n)
{
    char *r = ibm_filterText(self, t, n);

    fprintf(stderr, "probe: filterText(\"%s\", %ld) answered \"%s\"\n",
            t ? t : "(none)", n, r ? r : "(none)");
    fflush(stderr);
    return r;
}

void *__attribute__((thiscall)) evv_getEngineData(void *self, const void *lang)
{
    void *r = ibm_getEngineData(self, lang);

    fprintf(stderr, "probe: EngineArray::getEngineData answered %p\n", r);
    fflush(stderr);
    return r;
}

int __attribute__((thiscall)) evv_romAddText(void *self, const char *t,
                                             int a, int b)
{
    int r = ibm_romAddText(self, t, a, b);

    fprintf(stderr, "probe: RomanizerManager::addText(\"%s\") answered %d\n",
            t ? t : "(none)", r);
    fflush(stderr);
    return r;
}

int __stdcall engsynProcessSentences(void *e, const char *t)
{
    int r = ibm_engsynProcessSentences(e, t);

    fprintf(stderr, "probe: engsynProcessSentences(\"%s\") answered %d\n",
            t ? t : "(none)", r);
    fflush(stderr);
    return r;
}

int __stdcall engsynFlush(void *e, int a)
{
    int r = ibm_engsynFlush(e, a);

    fprintf(stderr, "probe: engsynFlush(%d) answered %d\n", a, r);
    fflush(stderr);
    return r;
}

void *delta_new(void)
{
    void *r = ibm_delta_new();

    fprintf(stderr, "probe: delta_new answered %p\n", r);
    fflush(stderr);
    return r;
}
