/* Heteronyms the engine's own test does not turn.
 *
 * A heteronym is a word spelled one way and said two, the reading decided by
 * the grammar around it. The engine does this and does it well: `read' is RED
 * after `have' and REED after `will', and `wind', `dove', `bow' and `record'
 * all turn on the same cues. Of 88 measured by tools/measure/heteronyms.py,
 * 71 turn and 17 do not.
 *
 * Those seventeen cannot be mended where the others live. The two readings
 * sit in a dictionary as an `or' pair, but the pair is data and the choosing
 * is compiled code: an arm runs a chain of tests, or hands two frame
 * addresses to test_noun_verb and names the word by an immediate that reaches
 * the readings through code it shares. Adding one means extending that, per
 * word, inside a rule with 779 arms. docs/notes/heteronyms.md has the whole
 * argument.
 *
 * So they are mended above the engine instead. `[ encloses a pronunciation in
 * the engine's own alphabet and the engine honours it, so a filter that
 * recognises the word and decides from the words around it can write the
 * reading the engine would otherwise miss. Nothing in a rule changes and
 * every caller is served, since a filter is what eciRegisterFilter publishes.
 *
 * What it does not try to do is beat the engine. The cue is the word before,
 * which is what the engine's own test uses, so "the world record" comes out
 * as the verb here exactly as it does there. Matching that is worth more than
 * being right where the rest of the engine is wrong.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eci_filter.h"
#include "eci_hetero.h"
#include "evv_abi.h"

extern THIS Filter *filter_ctor(Filter *self) MANGLED("??0Filter@@QAE@XZ");
extern THIS void filter_dtor(Filter *self) MANGLED("??1Filter@@QAE@XZ");
extern THIS int32_t filter_activateFilter(Filter *self)
    MANGLED("?activateFilter@Filter@@UAE?AW4ECIFilterError@@XZ");
extern THIS int32_t filter_deactivateFilter(Filter *self)
    MANGLED("?deactivateFilter@Filter@@UAE?AW4ECIFilterError@@XZ");
extern THIS int8_t filter_isActive(Filter *self)
    MANGLED("?isActive@Filter@@UAE_NXZ");
extern THIS int32_t filter_getFilterLanguage(Filter *self)
    MANGLED("?getFilterLanguage@Filter@@UAE?AW4ECILanguageDialect@@XZ");

extern void *cpp_new(uint32_t bytes);
extern void cpp_delete(void *p);

typedef struct {
    Filter  base;
    int32_t lastError;
    char   *result;      /* the answer, kept until the next call */
} HeteroFilter;

#define HETERO_FILTER_NAME "openevv heteronym filter"

/* The words, and the reading each one lacks.
 *
 * Every reading here was written as an annotation and read back through
 * eciGeneratePhonemes to confirm the engine accepts it and says what it was
 * told; docs/notes/heteronyms.md lists them. `has' is the reading the word
 * already gets, which says which way round the two are: a word the engine
 * says as a verb wants the noun written when the grammar calls for a noun,
 * and the other way about.
 */
typedef struct {
    const char *word;
    const char *noun;      /* what to write where a noun is meant */
    const char *verb;      /* and where a verb is */
} Hetero;

static const Hetero words[] = {
    { "decrease",   "`[.1di.0kris]",      0                      },
    { "digest",     0,                    "`[.0dX.1JEst]"        },
    { "export",     0,                    "`[.0Ek.1spcrt]"       },
    { "exports",    0,                    "`[.0Ek.1spcrts]"      },
    { "produce",    "`[.1pro.0dus]",      0                      },
    { "transfer",   0,                    "`[.0trAns.1fR]"       },
    { "transfers",  0,                    "`[.0trAns.1fRz]"      },
    { "transplant", 0,                    "`[.0trAnz.1plAnt]"    },
    { "transport",  0,                    "`[.0trAn.1spcrt]"     },
    { "transports", 0,                    "`[.0trAn.1spcrts]"    },
};

#define WORDS (int)(sizeof words / sizeof words[0])

/* What the word before says about what this one is.
 *
 * The engine's own test looks no further than one word back and neither does
 * this. A determiner or a preposition in front of a heteronym makes it a
 * noun; a modal, an infinitive `to' or a subject pronoun makes it a verb.
 * Anything else leaves it alone, which is the important case: a word this
 * cannot place is better left as the engine has it than guessed at.
 */
static const char *const nouny[] = {
    "the", "a", "an", "this", "that", "these", "those", "my", "your", "his",
    "her", "its", "our", "their", "some", "any", "no", "each", "every",
    "of", "in", "on", "for", "with", "from", "at", "by", "into", "about",
    0
};

static const char *const verby[] = {
    "to", "will", "would", "shall", "should", "can", "could", "may",
    "might", "must", "i", "we", "you", "they", "he", "she", "who",
    "and", "or", "not", "please", "let", "helps", "help", 0
};

static int among(const char *word, size_t n, const char *const *list)
{
    int i;

    for (i = 0; list[i] != 0; i++)
        if (strlen(list[i]) == n && strncasecmp(list[i], word, n) == 0)
            return 1;
    return 0;
}

static int is_word_byte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '\'';
}

/* Which of the table this word is, or -1. */
static int which(const char *at, size_t n)
{
    int i;

    for (i = 0; i < WORDS; i++)
        if (strlen(words[i].word) == n
            && strncasecmp(words[i].word, at, n) == 0)
            return i;
    return -1;
}

/* The word before the one at `at', as an offset and a length. Answers 0 when
   there is none. */
static const char *word_before(const char *text, const char *at, size_t *n)
{
    const char *p = at;

    while (p > text && !is_word_byte(p[-1]))
        p--;
    if (p == text)
        return 0;
    at = p;
    while (p > text && is_word_byte(p[-1]))
        p--;
    *n = (size_t)(at - p);
    return *n ? p : 0;
}

/* How long the answer can be: every word replaced by the longest reading. */
static size_t room_for(const char *text)
{
    size_t most = 0;
    int i;

    for (i = 0; i < WORDS; i++) {
        size_t a = words[i].noun ? strlen(words[i].noun) : 0;
        size_t b = words[i].verb ? strlen(words[i].verb) : 0;

        if (a > most)
            most = a;
        if (b > most)
            most = b;
    }
    return strlen(text) * (most + 2) + 1;
}

char *hetero_rewrite(const char *text)
{
    size_t room = room_for(text);
    char *out = cpp_new((uint32_t)room);
    const char *p = text;
    size_t at = 0;

    if (out == 0)
        return 0;

    while (*p != 0) {
        const char *start;
        size_t n;
        int w;

        if (!is_word_byte(*p)) {
            out[at++] = *p++;
            continue;
        }

        start = p;
        while (is_word_byte(*p))
            p++;
        n = (size_t)(p - start);

        w = which(start, n);
        if (w >= 0) {
            size_t bn = 0;
            const char *before = word_before(text, start, &bn);
            const char *say = 0;

            if (before != 0 && among(before, bn, nouny))
                say = words[w].noun;
            else if (before != 0 && among(before, bn, verby))
                say = words[w].verb;

            if (say != 0) {
                memcpy(out + at, say, strlen(say));
                at += strlen(say);
                continue;
            }
        }
        memcpy(out + at, start, n);
        at += n;
    }
    out[at] = 0;
    return out;
}

/* ---- the filter ------------------------------------------------------- */

extern const FilterVtbl vtbl_heterofilter;

static THIS HeteroFilter *het_ctor(HeteroFilter *self)
{
    filter_ctor(&self->base);
    self->base.vt = &vtbl_heterofilter;
    self->lastError = FILTER_OK;
    self->result = 0;
    return self;
}

static THIS void het_dtor(HeteroFilter *self)
{
    if (self->result != 0) {
        cpp_delete(self->result);
        self->result = 0;
    }
    filter_dtor(&self->base);
}

static THIS int32_t het_filterText(HeteroFilter *self, const char *text,
                                   char **out, int8_t force)
{
    self->lastError = FILTER_OK;

    if ((filter_isActive(&self->base) || force) && text != 0) {
        char *answer = hetero_rewrite(text);

        if (self->result != 0) {
            cpp_delete(self->result);
            self->result = 0;
        }
        self->result = answer;
        *out = self->result;
    } else if (out != 0) {
        *out = 0;
    }
    return self->lastError;
}

static THIS int32_t het_deleteFilter(HeteroFilter *self)
{
    het_dtor(self);
    cpp_delete(self);
    return FILTER_OK;
}

static THIS int32_t het_updateFilter(HeteroFilter *self, const char *a,
                                     const char *b)
{
    (void)a;
    (void)b;
    return self->lastError;
}

static THIS void het_setEnvironment(HeteroFilter *self, void *env)
{
    (void)self;
    (void)env;
}

static THIS char *het_getFilterDescription(HeteroFilter *self)
{
    (void)self;
    return (char *)HETERO_FILTER_NAME;
}

static int32_t het_version[4] = { 7, 0, 0, 0 };

static THIS int32_t het_getFilterVersion(HeteroFilter *self, int32_t *out)
{
    int i;

    (void)self;
    for (i = 0; i < 4; i++)
        out[i] = het_version[i];
    return FILTER_OK;
}

static THIS char **het_getFilterDependencies(HeteroFilter *self)
{
    (void)self;
    return 0;
}

/* Nothing here reads SSML, so the SSML entry answers nothing. */
static THIS char *het_filterSSMLText(HeteroFilter *self, const char *text,
                                     int32_t length)
{
    (void)self;
    (void)text;
    (void)length;
    return 0;
}

const FilterVtbl vtbl_heterofilter = {
    (THIS int32_t (*)(Filter *, const char *, char **, int8_t))
        het_filterText,
    filter_activateFilter,
    filter_deactivateFilter,
    (THIS int32_t (*)(Filter *))het_deleteFilter,
    filter_isActive,
    (THIS int32_t (*)(Filter *, const char *, const char *))het_updateFilter,
    (THIS void (*)(Filter *, void *))het_setEnvironment,
    (THIS char *(*)(Filter *))het_getFilterDescription,
    filter_getFilterLanguage,
    (THIS int32_t (*)(Filter *, int32_t *))het_getFilterVersion,
    (THIS char **(*)(Filter *))het_getFilterDependencies,
    (THIS char *(*)(Filter *, const char *, int32_t))het_filterSSMLText
};

/* Every text is worth looking at: unlike SSML there is no document shape to
   recognise, only words that may or may not be there. */
int hetero_isUsable(const char *text)
{
    (void)text;
    return 1;
}

STDCALL int hetero_getFilterObject(uint32_t idInterface, void **out)
{
    void *object = 0;

    if (idInterface == FILTER_INTERFACE_USABLE) {
        *out = (void *)hetero_isUsable;
        return 1;
    }

    if (idInterface == 1 || idInterface == FILTER_INTERFACE_OBJECT) {
        HeteroFilter *filter = cpp_new(sizeof *filter);

        if (filter != 0)
            object = het_ctor(filter);
        if (object != 0)
            *out = object;
    }
    return *out != 0;
}

/* ---- installing it ----------------------------------------------------
 *
 * Off unless asked for, which is IBM's shape for SSML and is right for the
 * same reason. It was going to be on by itself until the cost of that turned
 * up in measurement.
 *
 * Loading any filter turns annotation reading on for the whole instance --
 * eciGetParam(eciInputType) goes from 0 to 1 the moment this loads, because a
 * filter that writes annotations needs them read. So every backtick in the
 * caller's own text is then interpreted, and there is no way to protect it:
 *
 *     a `` here.        vanishes entirely
 *     a `vs50 here.     silently changes the voice
 *     a `x here.        spoken as "backquote x", which is harmless
 *     a \` here.        spoken as "backslash backquote"
 *
 * docs/api.md said a backslash before a backtick gives a literal backtick. It
 * does not, and that was checked rather than assumed; the note there is
 * corrected now.
 *
 * Which settles it. A mis-stressed `produce' is wrong and still intelligible;
 * a swapped voice or a swallowed character is wrong in a way a listener
 * cannot detect, and undetectable is the worse class for a screen reader,
 * whose whole contract is that what is heard is what is there. The gain fires
 * on nine words behind a determiner or a modal; the loss fires on backticks,
 * which are constant in code and in Markdown. So the caller decides, since
 * the caller is the only thing in the stack that knows whether it is reading
 * prose or a program.
 *
 * EVV_HETERO=on installs it, or a caller can register it itself the way it
 * would register any filter, with hetero_getFilterObject as the entry.
 *
 * The number is the top of the range the manager allows, so that a caller
 * registering its own filters from nought upwards -- which is what the
 * published examples do -- will not land on this one.
 */

#define HETERO_FILTER_ID 0x13

extern STDCALL int32_t api_activate_filter(void *self, void *which);
extern STDCALL int32_t api_new_filter(void *self, int32_t engine,
                                      int32_t which, void **out);
extern THIS int32_t fm_registerFilter(void *self, ECIFilterAttrib *attrib,
                                      uint32_t id, GetFilterObjectFn *entry,
                                      int8_t autoload)
    MANGLED("?registerFilter@FilterManager@@QAE?AW4ECIFilterError@@PAUECIFilterAttrib@@IP6GHIPAPAX@ZE@Z");

int hetero_install(void *manager, void *instance)
{
    ECIFilterAttrib attrib;
    GetFilterObjectFn entry = hetero_getFilterObject;

    if (manager == 0 || instance == 0)
        return 0;
    {
        const char *say = getenv("EVV_HETERO");

        if (say == 0 || strcmp(say, "on") != 0)
            return 0;
    }

    memset(&attrib, 0, sizeof attrib);
    {
        /* Register, load, then turn on -- the three a caller does, in that
           order. Registering only puts it in the registry; the manager runs
           what is loaded, and loading is what gives it the language the text
           path matches against. */
        int32_t reg = fm_registerFilter(manager, &attrib, HETERO_FILTER_ID,
                                        &entry, 1);
        void   *loaded = 0;
        int32_t act = -1;

        if (reg == FILTER_OK)
            api_new_filter(instance, (int32_t)attrib.language,
                           HETERO_FILTER_ID, &loaded);
        if (loaded != 0)
            act = api_activate_filter(instance, loaded);
        if (getenv("EVV_HETERO_TRACE") != 0)
            fprintf(stderr, "hetero: register %d load %s activate %d "
                    "language %u\n", (int)reg, loaded ? "yes" : "no",
                    (int)act, (unsigned)attrib.language);
        return reg == FILTER_OK && loaded != 0 && act == 0;
    }
}
