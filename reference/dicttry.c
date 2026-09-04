/* The eight dictionary calls as IBM's own engine answers them.
 *
 * The same driver as test/harness/dict.c, printing the same lines, so the
 * two can be held against each other by test/harness/dict.sh. Those eight
 * were the last published names with no wrapper in our tree, and they are
 * glue rather than transcription -- which is exactly the kind of code that
 * looks right and answers differently, so it is checked against the
 * original rather than reasoned about.
 *
 * It declares the entry points itself: these objects are IBM's and our
 * include/eci.h is ours.
 *
 * usage: dicttry.exe
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef void *ECIHand;
typedef void *ECIDictHand;
typedef int   ECIPartOfSpeech;

#define NULL_ECI_HAND   ((ECIHand)0)
#define NULL_DICT_HAND  ((ECIDictHand)0)

#define eciMainDict     0
#define eciRootDict     1
#define eciAbbvDict     2
#define eciMainDictExt  3

extern ECIHand __stdcall eciNewEx(int language);
extern int  __stdcall eciDelete(ECIHand h);
extern int  __stdcall eciGetAvailableLanguages(unsigned int *out, int *count);
extern ECIDictHand __stdcall eciNewDict(ECIHand h);
extern int  __stdcall eciSetDict(ECIHand h, ECIDictHand d);
extern int  __stdcall eciDeleteDict(ECIHand h, ECIDictHand d);
extern const char *__stdcall eciDictLookup(ECIHand h, ECIDictHand d, int vol,
                                           const void *key);
extern int __stdcall eciDictLookupA(ECIHand h, ECIDictHand d, int vol,
                                    const void *key, const char **out,
                                    ECIPartOfSpeech *part);
extern int __stdcall eciDictFindFirst(ECIHand h, ECIDictHand d, int vol,
                                      const char **key, const char **xlat);
extern int __stdcall eciDictFindFirstA(ECIHand h, ECIDictHand d, int vol,
                                       const char **key, const char **xlat,
                                       ECIPartOfSpeech *part);
extern int __stdcall eciDictFindNext(ECIHand h, ECIDictHand d, int vol,
                                     const char **key, const char **xlat);
extern int __stdcall eciUpdateDict(ECIHand h, ECIDictHand d, int vol,
                                   const void *key, const void *xlat);
extern int __stdcall eciUpdateDictA(ECIHand h, ECIDictHand d, int vol,
                                    const void *key, const void *xlat,
                                    ECIPartOfSpeech part);
extern int __stdcall eciGetAvailableFilters(ECIHand h, int language,
                                            unsigned int *ids,
                                            unsigned int *count);
extern int __stdcall eciGetFilterDescription(ECIHand h, int language,
                                             unsigned int id, char *out);

void evvRunStaticInitialisers(void);

static void say(const char *what, int rc)
{
    printf("%-22s %d\n", what, rc);
}

static void sayText(const char *what, const char *text)
{
    printf("%-22s [%s]\n", what, text ? text : "(none)");
}

int main(void)
{
    unsigned int langs[32];
    int          n = 0;
    ECIHand      h;
    ECIDictHand  d;
    const char  *key, *xlat;
    ECIPartOfSpeech part;

    evvRunStaticInitialisers();

    if (eciGetAvailableLanguages(0, &n) || n < 1) {
        fprintf(stderr, "dicttry: no language\n");
        return 1;
    }
    if (n > 32)
        n = 32;
    eciGetAvailableLanguages(langs, &n);

    h = eciNewEx((int)langs[0]);
    if (h == NULL_ECI_HAND) {
        fprintf(stderr, "dicttry: no instance\n");
        return 1;
    }

    d = eciNewDict(h);
    sayText("newDict", d ? "made" : "refused");
    say("setDict", eciSetDict(h, d));

    /* Teaching a word, in each volume the set has and in the one it has not. */
    say("update main", eciUpdateDict(h, d, eciMainDict, "openevv",
                                     "o p e n e v v"));
    say("update root", eciUpdateDict(h, d, eciRootDict, "walking", "walk"));
    say("update abbv", eciUpdateDict(h, d, eciAbbvDict, "kg", "kilogram"));
    say("update ext", eciUpdateDict(h, d, eciMainDictExt, "openevv", "x"));
    say("update ext A", eciUpdateDictA(h, d, eciMainDictExt, "openevv", "x",
                                       0));

    /* Reading them back. */
    sayText("lookup main", eciDictLookup(h, d, eciMainDict, "openevv"));
    sayText("lookup root", eciDictLookup(h, d, eciRootDict, "walking"));
    sayText("lookup abbv", eciDictLookup(h, d, eciAbbvDict, "kg"));
    sayText("lookup ext", eciDictLookup(h, d, eciMainDictExt, "openevv"));
    sayText("lookup absent", eciDictLookup(h, d, eciMainDict, "nosuchword"));

    key = xlat = 0;
    part = 0;
    say("lookupA main",
        eciDictLookupA(h, d, eciMainDict, "openevv", &key, &part));
    sayText("  found", key);
    say("  part", (int)part);

    /* And walking them. */
    key = xlat = 0;
    say("findFirst main", eciDictFindFirst(h, d, eciMainDict, &key, &xlat));
    sayText("  key", key);
    sayText("  translation", xlat);

    key = xlat = 0;
    say("findNext main", eciDictFindNext(h, d, eciMainDict, &key, &xlat));
    sayText("  key", key);
    sayText("  translation", xlat);

    key = xlat = 0;
    say("findFirst root", eciDictFindFirst(h, d, eciRootDict, &key, &xlat));
    sayText("  key", key);
    sayText("  translation", xlat);

    key = xlat = 0;
    part = 0;
    say("findFirstA main",
        eciDictFindFirstA(h, d, eciMainDict, &key, &xlat, &part));
    sayText("  key", key);
    say("  part", (int)part);

    key = xlat = 0;
    say("findFirst ext", eciDictFindFirst(h, d, eciMainDictExt, &key, &xlat));
    key = xlat = 0;
    part = 0;
    say("findFirstA ext",
        eciDictFindFirstA(h, d, eciMainDictExt, &key, &xlat, &part));
    key = xlat = 0;
    say("findNext ext", eciDictFindNext(h, d, eciMainDictExt, &key, &xlat));

    /* The two filter queries, which are empty in IBM's object as well. */
    say("availableFilters", eciGetAvailableFilters(h, (int)langs[0], 0, 0));
    say("filterDescription", eciGetFilterDescription(h, (int)langs[0], 0, 0));

    eciSetDict(h, NULL_DICT_HAND);
    eciDeleteDict(h, d);
    eciDelete(h);
    return 0;
}
