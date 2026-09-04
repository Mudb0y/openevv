/* The eight dictionary calls, and what each of them answers.
 *
 * These were the last published names with no wrapper here, and they are
 * glue over machinery the engine already had -- so what wants checking is
 * not the machinery but the glue: which road a language takes, what the
 * extended volume answers for a language that has not got one, and whether
 * a word taught can be read back and walked to.
 *
 * `reference/dicttry.c' is the same driver against IBM's own objects and
 * prints the same lines, so test/harness/dict.sh holds the two against each
 * other. Every line is an answer rather than a hash, so a difference names
 * itself.
 *
 * Only the plain road can be reached from here, and that is not a shortcoming
 * of the harness. The extended calls are for Chinese, Korean and Japanese;
 * this SDK has no Chinese or Korean at all and Japanese does not build, so
 * for every language in the tree the answer to the extended volume is that
 * the volume is wrong -- which is itself worth checking, and is checked.
 */

#include <stdio.h>
#include <string.h>

#include <eci.h>

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

    if (eciGetAvailableLanguages(0, &n) || n < 1) {
        fprintf(stderr, "dict: no language in this build\n");
        return 1;
    }
    if (n > 32)
        n = 32;
    eciGetAvailableLanguages(langs, &n);

    h = eciNewEx((int)langs[0]);
    if (h == NULL_ECI_HAND) {
        fprintf(stderr, "dict: no instance\n");
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
