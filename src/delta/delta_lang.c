/* Which language is in force, and the machines that belong to one.
 *
 * The header beside this says why a language is a table of tables rather
 * than a set of names. This is the small amount of code that goes with it:
 * which one this thread is speaking, which one made a given machine, and
 * the settings of all of them joined into the one blob the original's ini
 * reader expects.
 *
 * Nothing here knows how many languages there are. delta_languages[] is
 * written by the build, because that is what decides.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "delta.h"
#include "evv_arena.h"

/* Each module states its own numbers in another translation unit, where C
   will not have them in an initialiser, so each has a small function that
   fills them in. delta_lang_bind_all calls them; the build writes it, since
   it is the build that knows how many there are. Everything that reads one
   of those numbers comes through here first. */
static void bind(void)
{
    static int bound;

    if (!bound) {
        bound = 1;
        delta_lang_bind_all();
    }
}

/* What this thread is speaking. The reading and the setting are inline in
   the header, since every reach for a table of the language goes through
   them; the header says why, and says which paths set one. */
__thread const delta_language *delta_lang_current = 0;

/* The half that cannot be inline, and it never comes back: every path that
   runs the machine sets a language first, so nothing in force here is a
   language to guess at, it is a path nobody thought about. */
void delta_lang_none(void)
{
    fprintf(stderr, "evv: a table of the language was read with no"
            " language in force\n");
    abort();
}

const delta_language *delta_lang_by_id(int32_t id)
{
    int i;

    bind();
    for (i = 0; delta_languages[i] != 0; i++)
        if (delta_languages[i]->id == id)
            return delta_languages[i];
    return 0;
}

/* ---- a machine and the language that made it ------------------------ */

/* In front of the state rather than inside it. The state is IBM's layout
   from its first byte -- the rules address the globals as offsets from it,
   and the language says how far those run -- so a word of ours anywhere
   inside would land on something. Sixteen bytes so that what follows keeps
   whatever alignment the arena gave the block. */
typedef struct {
    const delta_language *lang;
    void                 *pad;
} delta_head;

delta_state *delta_lang_alloc(const delta_language *l)
{
    delta_head *h;

    if (l == 0)
        return 0;

    bind();
    h = malloc(sizeof *h + (size_t)l->state_bytes);
    if (h == 0)
        return 0;

    h->lang = l;
    h->pad  = 0;
    return (delta_state *)(h + 1);
}

void delta_lang_free(delta_state *d)
{
    if (d != 0)
        free((delta_head *)d - 1);
}

const delta_language *delta_lang_of(const struct delta_state *d)
{
    return d != 0 ? ((const delta_head *)d)[-1].lang : 0;
}

/* ---- the settings of every language, as one blob -------------------- */

/* The original's reader walks sections and stops on a byte of 0xff, and
   eciGetAvailableLanguages answers out of the section names, so a build with
   two languages in it wants one blob with both their sections. Each module
   carries its own, and each begins with a [LanguageIndependent] section that
   says the same thing about the filters; the first one is kept and the rest
   are stepped over, so what comes out reads as one file rather than as two
   stuck together. */

static char   *ini_all;
static int32_t ini_all_size;

/* Where a blob's first language section starts. A language section is named
   for the language written as numbers, so it is the first bracket with a
   digit after it; nought if there is none, which cannot happen in a module
   the lifter wrote but is worth not walking off the end of. */
static int32_t first_language_section(const char *b, int32_t n)
{
    int32_t i;

    for (i = 0; i + 1 < n; i++)
        if (b[i] == '[' && b[i + 1] >= '0' && b[i + 1] <= '9')
            return i;
    return 0;
}

/* Up to but not including the end marker. */
static int32_t body_of(const char *b, int32_t n)
{
    int32_t i;

    for (i = 0; i < n; i++)
        if ((signed char)b[i] == -1)
            return i;
    return n;
}

static void ini_build(void)
{
    int32_t room = 1;
    int32_t at = 0;
    int     i;

    bind();

    for (i = 0; delta_languages[i] != 0; i++)
        room += delta_languages[i]->ini_size + 2;

    ini_all = malloc((size_t)room);
    if (ini_all == 0)
        return;

    for (i = 0; delta_languages[i] != 0; i++) {
        const char *b = delta_languages[i]->ini;
        int32_t     n = body_of(b, delta_languages[i]->ini_size);
        int32_t     from = (i == 0) ? 0 : first_language_section(b, n);

        /* A blank line before the bracket, which is how one section is
           separated from the next inside a module's own blob. The reader's
           end-of-section walk stops on it and the key lookup takes stopping
           there to mean the key is absent; butted straight together it stops
           on the bracket instead. */
        if (i != 0) {
            ini_all[at++] = '\n';
            ini_all[at++] = '\n';
        }

        memcpy(ini_all + at, b + from, (size_t)(n - from));
        at += n - from;
    }

    ini_all[at++] = (char)0xff;
    ini_all_size = at;
}

const char *delta_lang_ini(void)
{
    if (ini_all == 0)
        ini_build();
    return ini_all;
}

int32_t delta_lang_ini_size(void)
{
    if (ini_all == 0)
        ini_build();
    return ini_all_size;
}
