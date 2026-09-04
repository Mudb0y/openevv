/* Ask IBM's own engine what the language decided a word was made of.
 *
 * `eciGeneratePhonemes' had no wrapper in our tree, so it was transcribed
 * out of `eci.obj' rather than reasoned about -- and what a transcription
 * cannot say is whether the phonemes arrive at all. This is the side that
 * says: the same driver as test/harness/phonemes.c, printing the same
 * shape, so the two can be held against each other line for line.
 *
 * The order matters and is what this fixes. The call wants a callback
 * registered, because that is the only way a phoneme buffer can be
 * reported, and it wants the queued synthesis mode, because it walks the
 * queue that mode builds. Both are IBM's own tests.
 *
 * The text comes from a file rather than the command line, because an
 * argument handed to a Windows binary under Wine is recoded on the way in
 * and a case with an accented letter then reaches the two engines as
 * different bytes.
 *
 * usage: phontry.exe <cases.txt>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef void *ECIHand;

/* The messages, as the older interface numbers them. */
#define ECI_PHONEMES       1

/* The settings this needs by number: the queued synthesis mode, and whether
   the caller wants to be told about each phoneme as it is reached. */
#define ECI_SYNTHMODE      0
#define ECI_WANTPHONEMES   7

#define PHON_ROOM 8192
#define LINE_ROOM 4096

extern ECIHand __stdcall eciNewEx(int language);
extern int  __stdcall eciDelete(ECIHand h);
extern int  __stdcall eciGetAvailableLanguages(unsigned int *out, int *count);
extern void __stdcall eciRegisterCallback(ECIHand h, void *cb, void *data);
extern int  __stdcall eciSetParam(ECIHand h, int which, int value);
extern int  __stdcall eciAddText(ECIHand h, const char *text);
extern int  __stdcall eciGeneratePhonemes(ECIHand h, int room, void *buffer);

void evvRunStaticInitialisers(void);

static char phon[PHON_ROOM];
static char said[PHON_ROOM * 4];

static int __stdcall on_message(ECIHand h, int msg, long param, void *data)
{
    (void)h;
    (void)data;
    if (msg == ECI_PHONEMES)
        strncat(said, phon, (size_t)param);
    return 1;                   /* eciDataProcessed */
}

int main(int argc, char **argv)
{
    unsigned int langs[32];
    int          n = 0;
    ECIHand      h;
    FILE        *f;
    char         line[LINE_ROOM];

    evvRunStaticInitialisers();

    if (argc != 2) {
        fprintf(stderr, "usage: phontry.exe <cases.txt>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == 0) {
        fprintf(stderr, "phontry: cannot read %s\n", argv[1]);
        return 1;
    }

    if (eciGetAvailableLanguages(0, &n) || n < 1) {
        fprintf(stderr, "phontry: no language\n");
        return 1;
    }
    if (n > 32)
        n = 32;
    eciGetAvailableLanguages(langs, &n);

    while (fgets(line, sizeof line, f) != 0) {
        size_t len = strlen(line);

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        if (len == 0)
            continue;

        h = eciNewEx((int)langs[0]);
        if (h == 0) {
            fprintf(stderr, "phontry: no instance\n");
            return 1;
        }

        eciRegisterCallback(h, (void *)on_message, 0);
        eciSetParam(h, ECI_WANTPHONEMES, 1);
        eciSetParam(h, ECI_SYNTHMODE, 1);
        eciAddText(h, line);

        said[0] = 0;
        memset(phon, 0, sizeof phon);
        eciGeneratePhonemes(h, sizeof phon, phon);
        printf("[%s]\n", said);

        eciDelete(h);
    }

    fclose(f);
    return 0;
}
