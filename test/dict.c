/* Read a dictionary in from a file and hear that it took.
 *
 * eciLoadDict and eciSaveDict were published and left unwritten: both
 * answered eciDictNotSupported whatever they were handed. Nothing in the
 * suite could see that, because nothing in the suite loads a dictionary --
 * cli/probe.c walks the dictionary layer far enough to make one, put it in
 * force and take it away again, and the reference answers the same refusal
 * from the same stub, so both sides agreed. A caller with a file of
 * pronunciations had no way in at all, since the per-entry calls are not
 * exported by the older interface either.
 *
 * Three things are checked. Loading has to answer that it worked. The word
 * the dictionary names has to come out different from the way it comes out
 * without it. And saving has to write the entry back.
 *
 * The second is the one that matters, and it is why this speaks twice rather
 * than once: a dictionary that is merely made and put in force changes the
 * audio on its own, so holding a run with a dictionary against a run without
 * one proves nothing. Both runs here load a dictionary and differ only in
 * whether it names the word being spoken.
 *
 * usage: dict
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

enum { FRAME = 1024 };

/* eciMainDict, and what the older interface answers for success. */
enum { VOLUME_MAIN = 0, DICT_OK = 0 };

typedef struct OldInst OldInst;

enum ECIMessage { eciWaveformBuffer, eciPhonemeBuffer, eciIndexReply };
enum ECICallbackReturn { eciDataNotProcessed, eciDataProcessed, eciDataAbort };

OldInst *STDCALL eo_newEx(int32_t language);
int      STDCALL es_delete(OldInst *h);
int      STDCALL et_addText(OldInst *h, const char *text);
int      STDCALL et_synthesize(OldInst *h);
int      STDCALL ev_setOutputBuffer(OldInst *h, int32_t n, void *buf);
void     STDCALL eo_registerCallback(OldInst *h, void *cb, void *data);
void     STDCALL eo_synchronizeSynth(OldInst *h);
int      STDCALL eo_speaking(OldInst *h);
int      STDCALL eo_getAvailableLanguages(uint32_t *out, int *count);
void    *STDCALL ed_newDict(OldInst *h);
int      STDCALL ed_setDict(OldInst *h, void *dict);
int      STDCALL ed_deleteDict(OldInst *h, void *dict);
int      STDCALL ed_loadDict(OldInst *h, void *dict, int32_t kind,
                             const char *name);
int      STDCALL ed_saveDict(OldInst *h, void *dict, int32_t kind,
                             const char *name);
void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

static short frame[FRAME];
static long  said;

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

/* A dictionary file of one entry. A file's last line is dropped on the way
   past the end of it, so there is a blank one after the entry. */
static int write_dict(const char *path, const char *word, const char *say_as)
{
    FILE *f = fopen(path, "wb");

    if (!f)
        return 0;
    fprintf(f, "%s\t%s\n\n", word, say_as);
    fclose(f);
    return 1;
}

/* Speak one word through a dictionary read in from *path*. Answers how many
   samples came back, or -1 where it would not play at all. */
static long say_through(uint32_t language, const char *path, int *loaded)
{
    OldInst *h = eo_newEx(language);
    void    *dict;
    long     was;
    int      i;

    *loaded = -1;
    if (!h)
        return -1;

    eo_registerCallback(h, (void *)on_message, 0);
    if (!ev_setOutputBuffer(h, FRAME, frame)) {
        es_delete(h);
        return -1;
    }

    dict = ed_newDict(h);
    if (!dict) {
        es_delete(h);
        return -1;
    }

    *loaded = ed_loadDict(h, dict, VOLUME_MAIN, path);
    ed_setDict(h, dict);

    was = said;
    if (!et_addText(h, "nvda") || !et_synthesize(h)) {
        es_delete(h);
        return -1;
    }
    /* Nothing drains the engine's message queue by itself; asking whether it
       is still speaking is what pumps it. */
    for (i = 0; i < 3000 && eo_speaking(h); i++)
        nap(10);
    eo_synchronizeSynth(h);

    ed_setDict(h, 0);
    ed_deleteDict(h, dict);
    es_delete(h);
    return said - was;
}

int main(void)
{
    const char *named   = "dict-named.txt";
    const char *other   = "dict-other.txt";
    const char *written = "dict-written.txt";
    uint32_t    langs[32];
    int         n = 32;
    int         loadedNamed = -1, loadedOther = -1, bad = 0;
    long        withEntry, withoutEntry;
    OldInst    *h;
    void       *dict;
    char        back[256];
    FILE       *f;

    evv_port_start();
    evvRunStaticInitialisers();

    if (eo_getAvailableLanguages(langs, &n) || n < 1) {
        printf("dict: no language\n");
        return 1;
    }

    if (!write_dict(named, "nvda", "enn vee dee ay")
        || !write_dict(other, "xyzzynothing", "foo")) {
        printf("dict: could not write the dictionaries\n");
        return 1;
    }

    withoutEntry = say_through(langs[0], other, &loadedOther);
    withEntry    = say_through(langs[0], named, &loadedNamed);

    if (loadedOther != DICT_OK || loadedNamed != DICT_OK) {
        printf("dict: loading answered %d and %d, wanted %d\n",
               loadedOther, loadedNamed, DICT_OK);
        bad = 1;
    }
    if (withoutEntry <= 0 || withEntry <= 0) {
        printf("dict: one of the runs said nothing at all (%ld, %ld)\n",
               withoutEntry, withEntry);
        bad = 1;
    } else if (withoutEntry == withEntry) {
        printf("dict: the entry changed nothing -- both runs answered %ld"
               " samples, so the dictionary was not read\n", withEntry);
        bad = 1;
    } else {
        printf("dict: %ld samples without the entry, %ld with it\n",
               withoutEntry, withEntry);
    }

    /* And back out to a file again. */
    h = eo_newEx(langs[0]);
    if (h) {
        eo_registerCallback(h, (void *)on_message, 0);
        ev_setOutputBuffer(h, FRAME, frame);
        dict = ed_newDict(h);
        if (dict) {
            if (ed_loadDict(h, dict, VOLUME_MAIN, named) != DICT_OK
                || ed_saveDict(h, dict, VOLUME_MAIN, written) != DICT_OK) {
                printf("dict: saving would not answer that it worked\n");
                bad = 1;
            } else {
                back[0] = 0;
                f = fopen(written, "rb");
                if (f) {
                    size_t got = fread(back, 1, sizeof back - 1, f);

                    back[got] = 0;
                    fclose(f);
                }
                if (strstr(back, "nvda") == 0) {
                    printf("dict: what was written back has no entry in it\n");
                    bad = 1;
                }
            }
            ed_deleteDict(h, dict);
        }
        es_delete(h);
    }

    remove(named);
    remove(other);
    remove(written);
    evv_port_finish();

    if (bad)
        return 1;
    printf("dict: a dictionary read in from a file changes how a word is"
           " said, and writes back out again\n");
    return 0;
}
