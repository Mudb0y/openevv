/* What a language decided a sentence was made of, from any engine that
 * speaks the ECI interface, loaded by name.
 *
 * This is the driver for test/harness/eti.sh, which holds ours against ETI
 * Eloquence 6.1 as Apple ships it. The same calls are made of both engines,
 * so the two can only differ in what they decide. It asks for phonemes
 * rather than samples: the two synthesisers are separate implementations and
 * differ by a few dozen samples where the text was read the same way, and
 * phonemes say whether it was.
 *
 * Each line of the case file is the language as a number, the text mode
 * (-1 to leave the default), and the text, separated by tabs. One line comes
 * out for each, the phonemes in brackets and the text after them.
 *
 * usage: eti <library> <cases> [ours]
 *
 * With a third argument the abbreviation dictionary and phrase prediction are
 * turned on, which is what 6.1 does and what ours does not unless asked
 * (docs/quirks.md says why). 6.1 has no parameter 11 to set and must not be
 * handed one, so this is asked of ours only.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*NewEx)(int);
typedef void *(*Delete)(void *);
typedef void (*Register)(void *, void *, void *);
typedef int (*SetParam)(void *, int, int);
typedef int (*AddText)(void *, const char *);
typedef int (*Generate)(void *, int, void *);

static char phon[8192];
static char said[65536];

/* Phonemes come back a buffer at a time in message 1. The parameter is a
   long on a 64-bit engine built for the Mac and an int on ours; only the low
   half is read. */
static int on_message(void *h, int msg, long param, void *data)
{
    (void)h;
    (void)data;
    if (msg == 1 && (int)param > 0)
        strncat(said, phon, (size_t)(int)param);
    return 1;
}

int main(int argc, char **argv)
{
    void *lib;
    NewEx newEx;
    Delete del;
    Register reg;
    SetParam set;
    AddText add;
    Generate gen;
    char line[8192];
    FILE *f;

    if (argc < 3) {
        fprintf(stderr, "usage: eti <library> <cases> [ours]\n");
        return 2;
    }
    lib = dlopen(argv[1], RTLD_NOW);
    if (lib == NULL) {
        fprintf(stderr, "eti: %s\n", dlerror());
        return 1;
    }
    newEx = (NewEx)dlsym(lib, "eciNewEx");
    del = (Delete)dlsym(lib, "eciDelete");
    reg = (Register)dlsym(lib, "eciRegisterCallback");
    set = (SetParam)dlsym(lib, "eciSetParam");
    add = (AddText)dlsym(lib, "eciAddText");
    gen = (Generate)dlsym(lib, "eciGeneratePhonemes");
    if (!newEx || !del || !reg || !set || !add || !gen) {
        fprintf(stderr, "eti: %s lacks an entry this needs\n", argv[1]);
        return 1;
    }
    f = fopen(argv[2], "r");
    if (f == NULL) {
        fprintf(stderr, "eti: cannot read %s\n", argv[2]);
        return 1;
    }
    while (fgets(line, sizeof line, f)) {
        char *mode, *text;
        void *h;

        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == 0 || line[0] == '#')
            continue;
        mode = strchr(line, '\t');
        text = mode ? strchr(mode + 1, '\t') : NULL;
        if (text == NULL)
            continue;
        *mode++ = 0;
        *text++ = 0;
        /* An instance a case, because the engine's second utterance is not
           its first. */
        h = newEx((int)strtol(line, NULL, 0));
        if (h == NULL) {
            printf("[no instance]\t%s\n", text);
            continue;
        }
        reg(h, (void *)on_message, NULL);
        if (argc > 3) {
            set(h, 3, 0);
            set(h, 11, 1);
        }
        if (atoi(mode) >= 0)
            set(h, 2, atoi(mode));
        /* Phonemes come only with the synthesis mode that waits to be told
           to speak, in both engines. */
        set(h, 0, 1);
        set(h, 7, 1);
        add(h, text);
        said[0] = 0;
        memset(phon, 0, sizeof phon);
        gen(h, sizeof phon, phon);
        del(h);
        printf("[%s]\t%s\n", said, text);
        fflush(stdout);
    }
    fclose(f);
    return 0;
}
