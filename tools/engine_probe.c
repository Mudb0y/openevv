/* Speak one line through whichever eci.dll is named, and say what came back.
 *
 * Built for both word sizes so that OpenEVV's library and IBM's original --
 * which is thirty-two bit and always will be -- can be asked the same thing
 * and their answers held against each other.
 *
 *     engine_probe <path to eci.dll> <text>
 *
 * Prints the number of samples and a checksum of them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef void *(__stdcall *NewFn)(void);
typedef int   (__stdcall *DeleteFn)(void *);
typedef int   (__stdcall *AddTextFn)(void *, const char *);
typedef int   (__stdcall *IndexFn)(void *, int);
typedef int   (__stdcall *SynthFn)(void *);
typedef int   (__stdcall *SyncFn)(void *);
typedef int   (__stdcall *BufFn)(void *, int, short *);
typedef int   (__stdcall *ParamFn)(void *, int, int);
typedef void  (__stdcall *CbFn)(void *, void *, void *);

#define SAMPLES 2048
static short buffer[SAMPLES];
static long  total;
static unsigned long sum;

static int __stdcall callback(void *h, int msg, long param, void *data)
{
    long i;
    (void)h; (void)data;
    if (msg == 0) {
        for (i = 0; i < param; ++i)
            sum = sum * 1000003u + (unsigned short)buffer[i];
        total += param;
    }
    return 1;
}

int main(int argc, char **argv)
{
    HMODULE lib;
    void *h;
    char dir[MAX_PATH], *slash;

    if (argc < 3) {
        fprintf(stderr, "usage: engine_probe <eci.dll> <text>\n");
        return 2;
    }
    /* The original wants its .syn files, which it looks for beside itself. */
    strncpy(dir, argv[1], MAX_PATH - 1);
    dir[MAX_PATH - 1] = 0;
    slash = strrchr(dir, 92);      /* a backslash, written as its code */
    if (!slash) slash = strrchr(dir, '/');
    if (slash) { *slash = 0; SetCurrentDirectoryA(dir); }

    lib = LoadLibraryA(argv[1]);
    if (!lib) { fprintf(stderr, "could not load %s (%lu)\n", argv[1], GetLastError()); return 1; }

#define GET(t, n) (t)GetProcAddress(lib, n)
    NewFn      eciNew      = GET(NewFn, "eciNew");
    DeleteFn   eciDelete   = GET(DeleteFn, "eciDelete");
    AddTextFn  eciAddText  = GET(AddTextFn, "eciAddText");
    IndexFn    eciIndex    = GET(IndexFn, "eciInsertIndex");
    SynthFn    eciSynth    = GET(SynthFn, "eciSynthesize");
    SyncFn     eciSync     = GET(SyncFn, "eciSynchronize");
    BufFn      eciBuf      = GET(BufFn, "eciSetOutputBuffer");
    ParamFn    eciParam    = GET(ParamFn, "eciSetParam");
    CbFn       eciCb       = GET(CbFn, "eciRegisterCallback");
    if (!eciNew || !eciAddText || !eciSynth || !eciBuf || !eciCb) {
        fprintf(stderr, "%s is missing entry points\n", argv[1]); return 1;
    }

    h = eciNew();
    if (!h) { fprintf(stderr, "eciNew failed\n"); return 1; }
    eciCb(h, (void *)callback, NULL);
    eciBuf(h, SAMPLES, buffer);
    eciParam(h, 1, 1);                       /* annotations on */
    eciAddText(h, argv[2]);
    eciIndex(h, 0xFFFF);
    eciSynth(h);
    eciSync(h);
    printf("%ld %08lx\n", total, sum);
    eciDelete(h);
    return 0;
}
