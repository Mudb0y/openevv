/* Ask the settings reader for keys that are not there.
 *
 * Neither suite can see this. A key that is absent has to come back as
 * nothing, and until 23 August 2026 one came back holding the *next*
 * section's first value instead -- so a two-language build died in
 * loadStandardConcatenativeVoice, inside sscanf, on a value with one field
 * where eight were being read. One language never showed it, because with a
 * single section there is no next section to borrow from: the last section is
 * followed by the end marker, and the end marker is one of the three bytes the
 * reader does check for.
 *
 * The defect is IBM's, verified in their own analysis/enus/win_iniread.obj:
 * getString decides a key is absent by reading the byte where the search
 * stopped and comparing it against NUL, newline and 0xff, and against nothing
 * else. It cannot fire in IBM's engine because their readFileIntoMemory hands
 * over one embedded blob for one language, whose sections a blank line apart.
 * Two sections butted together is a shape their data never has, and ours only
 * has it because delta_lang.c joins several modules' blobs into one.
 *
 * So both halves are checked here. The first is a blob written by hand with
 * the sections deliberately butted, which is the fault in three lines and does
 * not care which languages were linked. The second is the blob this build
 * actually carries, held to the rule the voice table needs: every key it asks
 * for is either absent or carries the eight numbers that table reads. A real
 * value has a ninth field, the dataset's path, which the table does not read;
 * the value that used to arrive by mistake was a path on its own. That half
 * grows teeth as languages are added, without being rewritten.
 *
 * Nothing here wants Wine, IBM's objects, or a sound card.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/eci_engine.h"

extern IniFileReader *ini_ctor(IniFileReader *r);
extern char          *ini_getString(IniFileReader *r, const char *section,
                                    const char *key);
extern const char    *ini_getFirstSection(IniFileReader *r);
extern const char    *ini_getNextSection(IniFileReader *r);
extern const char    *delta_lang_ini(void);
extern int32_t        delta_lang_ini_size(void);
extern void           cpp_delete(void *p);

static int failures;

static void fail(const char *what)
{
    printf("inikeys: FAIL %s\n", what);
    failures++;
}

/* A reader over a blob of our own rather than the one that was linked. The
   constructor fetches the build's blob, so the fields are set after it. */
static void reader_over(IniFileReader *r, const char *blob, int32_t size)
{
    ini_ctor(r);
    r->text = blob;
    r->size = size;
    r->at   = 0;
}

/* Lines end in NUL, a blank line is a newline, and 0xff ends the blob. The
   sections here are butted straight together with no blank line, which is
   what the join used to produce. */
static const char butted[] =
    "[LanguageIndependent]\0Filter=1\0"
    "[1.0]\0Voice1Dataset1.0.11025=0 50 60 30 0 0 55 92 Voices\\x\0"
    "[4.0]\0Path=eci\0\xff";

/* The same, separated the way a module's own blob separates them. */
static const char spaced[] =
    "[LanguageIndependent]\0Filter=1\0"
    "[1.0]\0Voice1Dataset1.0.11025=0 50 60 30 0 0 55 92 Voices\\x\0\n\n"
    "[4.0]\0Path=eci\0\xff";

static void by_hand(const char *what, const char *blob, int32_t size)
{
    IniFileReader r;
    char         *value;

    reader_over(&r, blob, size);
    value = ini_getString(&r, "1.0", "Voice1Dataset1.0.11025");
    if (value == 0 || strcmp(value, "0 50 60 30 0 0 55 92 Voices\\x") != 0)
        fail("a key that is there did not come back");
    else
        printf("inikeys: %s, present key reads <%s>\n", what, value);
    if (value)
        cpp_delete(value);

    /* Absent from [1.0]. The next section's first value is "eci", which is
       what used to come back. */
    reader_over(&r, blob, size);
    value = ini_getString(&r, "1.0", "Voice8Dataset1.0.8000");
    if (value != 0) {
        printf("inikeys: %s, absent key came back holding <%s>\n", what, value);
        fail("an absent key came back with a value");
        cpp_delete(value);
    } else {
        printf("inikeys: %s, absent key reads as absent\n", what);
    }
}

/* Every dataset key the voice table asks for, over every section this build
   carries: absent, or eight numbers and then whatever else. Anything else is
   the same defect arriving by another road. */
static void as_linked(void)
{
    static const int32_t rates[3] = { 8000, 11025, 22050 };

    IniFileReader r;
    const char   *name;
    int           sections = 0;
    int           asked = 0;
    int           present = 0;

    ini_ctor(&r);
    if (r.text == 0 || r.text != delta_lang_ini()
        || r.size != delta_lang_ini_size()) {
        fail("the reader did not open this build's own settings");
        return;
    }

    for (name = ini_getFirstSection(&r); name != 0;
         name = ini_getNextSection(&r)) {
        char section[0x20];
        int  major, minor, voice, rate;
        size_t n = strlen(name);

        if (n < 3 || n - 2 >= sizeof section)
            continue;
        memcpy(section, name + 1, n - 2);
        section[n - 2] = 0;
        sections++;
        if (sscanf(section, "%d . %d", &major, &minor) != 2)
            continue;

        for (voice = 1; voice <= 8; voice++)
            for (rate = 0; rate < 3; rate++) {
                IniFileReader q;
                char          key[0x20];
                char         *value;
                int           fields = 0;
                int           numbers = 0;
                char         *save, *tok, *copy, *rest;

                sprintf(key, "Voice%uDataset%u.%u.%u", (unsigned)voice,
                        (unsigned)major, (unsigned)minor,
                        (unsigned)rates[rate]);

                /* A fresh reader per lookup: getString walks the one it is
                   given, and the loop above is walking this one. */
                ini_ctor(&q);
                value = ini_getString(&q, section, key);
                asked++;
                if (value == 0)
                    continue;
                present++;

                copy = strdup(value);
                cpp_delete(value);
                if (copy == 0)
                    continue;
                for (tok = strtok_r(copy, " ", &save); tok != 0;
                     tok = strtok_r(NULL, " ", &save)) {
                    fields++;
                    if (fields > 8)
                        continue;
                    (void)strtol(tok, &rest, 10);
                    if (rest != tok && *rest == 0)
                        numbers++;
                }
                if (fields < 8 || numbers != 8) {
                    printf("inikeys: [%s] %s has %d fields, %d of the first"
                           " eight numbers: <%s>\n",
                           section, key, fields, numbers, copy);
                    fail("a key came back with something else's value");
                }
                free(copy);
            }
    }

    printf("inikeys: %d sections, %d keys asked for, %d present\n",
           sections, asked, present);
    if (sections < 2)
        fail("fewer than two sections, so nothing was really tested");
    if (present == 0)
        fail("no dataset key was found at all");
}

int main(void)
{
    printf("inikeys: start\n");
    by_hand("butted", butted, (int32_t)sizeof butted - 1);
    by_hand("spaced", spaced, (int32_t)sizeof spaced - 1);
    as_linked();

    if (failures != 0) {
        printf("inikeys: %d failures\n", failures);
        return 1;
    }
    printf("inikeys: ok\n");
    return 0;
}
