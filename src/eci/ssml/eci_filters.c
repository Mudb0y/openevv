/* Text on the way in, before the engine sees it.
 *
 * Two things happen here. Text arriving as UTF-16 is turned into whatever
 * the language's own code set is, and back again on the way out; and the
 * filters -- SSML being the one that matters -- are found, checked and
 * turned on.
 *
 * Both halves rest on managers that are already ours. What is here is the
 * plain-C skin the rest of the library calls them through: the managers are
 * C++ objects reached by method, these are functions taking the instance.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "evv_abi.h"
#include "eci_old.h"

/* What the instance keeps for this layer. */

/* Refusing to convert is reported as this, both against the call and in
   the running total the instance keeps. */
#define REFUSED_CONVERSION 0x1000

/* Only three languages are written in a code set wide enough to need
   converting; the rest go through untouched. The language word holds the
   language in its second byte. */
#define LANGUAGE_BYTE 0xff00
#define LANG_JAPANESE 0x0800
#define LANG_CHINESE  0x0900
#define LANG_KOREAN   0x0a00

/* How many filters this asks about at once, and how long a description it
   leaves room for. */
#define FILTERS_AT_ONCE 0x14
#define DESCRIPTION_ROOM 0x104

/* What the SSML filter calls itself. */
#define SSML_FILTER_NAME "IBM SSML Filter"

/* What an annotation turning a filter on and off looks like. The pair with
   the g is the general form and the other is the one a filter of its own
   uses; which is wanted is the caller's business. */
#define ANNO_GENERAL_ON  " `gfa%d "
#define ANNO_GENERAL_OFF " `gfd%d "
#define ANNO_OWN_ON      " `fa%d "
#define ANNO_OWN_OFF     " `fd%d "
#define ANNO_ROOM 12

extern THIS char *fm_filterTextByHandle(void *m, void *filter,
                                        const char *text);
extern THIS void  fm_getAvailableFilters(void *m, int32_t lang, uint32_t *ids,
                                         uint32_t *n);
extern THIS char **fm_getFilterDependencies(void *m, int32_t lang,
                                            uint32_t id);
extern THIS void  fm_getFilterDescription(void *m, int32_t lang, uint32_t id,
                                          char *out);
extern THIS int   fm_isActiveById(void *m, uint32_t id);
extern THIS int   fm_isActive(void *m, int32_t lang, uint32_t id);
extern THIS int   fm_isAutoload(void *m, int32_t lang, uint32_t id);
extern THIS int   fm_isUsable(void *m, const char *name, int32_t lang,
                              uint32_t id);
extern THIS int   rm_MBCSToUnicode(void *m, uint32_t lang, const char *in,
                                   uint16_t **out);
extern THIS int   rm_UnicodeToMBCS(void *m, uint32_t lang, const uint16_t *in,
                                   char **out, int32_t n);

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");

extern STDCALL int32_t api_get_available_filters(void *h2, int32_t lang,
                                                 uint32_t *ids, uint32_t *n)
    MANGLED("_eciGetAvailableFilters2@16");
extern STDCALL int32_t api_get_filter_description(void *h2, int32_t lang,
                                                  uint32_t id, char *out)
    MANGLED("_eciGetFilterDescription2@16");

/* ---- text in one code set or another --------------------------------- */

/* A UTF-16 string is counted and searched the same way a plain one is; the
   C library has neither for this width. */
uint32_t UniStrlen(const uint16_t *s)
{
    uint32_t n = 0;

    while (s[n] != 0)
        n++;
    return n;
}

uint16_t *UniStrstr(const uint16_t *hay, const uint16_t *needle)
{
    uint32_t n = UniStrlen(needle);
    uint32_t i, j;

    if (n == 0)
        return (uint16_t *)hay;

    for (i = 0; hay[i] != 0; i++) {
        for (j = 0; j < n && hay[i + j] == needle[j]; j++)
            ;
        if (j == n)
            return (uint16_t *)(hay + i);
    }
    return 0;
}

/* Whether a piece of UTF-16 opens with something the SSML reader would
   want. Only the two tags are looked for, anywhere in the text rather than
   at the front, which is what the original does. */
int8_t CheckUnicodeHeaderTag(const uint16_t *text)
{
    static const uint16_t xml[]   = { '<', '?', 'x', 'm', 'l', 0 };
    static const uint16_t speak[] = { '<', 's', 'p', 'e', 'a', 'k', 0 };

    if (text == 0)
        return 0;
    if (UniStrstr(text, xml) != 0)
        return 1;
    if (UniStrstr(text, speak) != 0)
        return 1;
    return 0;
}

/* Whether this language is written in a code set that needs converting. */
static int wide_language(int32_t lang)
{
    int32_t which = lang & LANGUAGE_BYTE;

    return which == LANG_JAPANESE || which == LANG_CHINESE
        || which == LANG_KOREAN;
}

/* Both converters answer nought when they had nothing to do or did it, and
   the refusal code when they could not; a language that needs no conversion
   has its text passed along unchanged. */
int32_t UnicodeConverter(void *p, const uint16_t *in, char **out,
                         int32_t n)
{
    OldInst *inst = p;
    int32_t lang, rc = 0;

    if (inst == 0)
        return REFUSED_CONVERSION;

    lang = OI_LANG(inst);
    if (wide_language(lang))
        rc = rm_UnicodeToMBCS(OI_ROMMGR(inst), (uint32_t)lang, in, out, n);
    else
        *out = (char *)in;

    if (rc == 0)
        return 0;

    OI_REFUSED(inst)     = REFUSED_CONVERSION;
    OI_REFUSEDALL(inst) |= REFUSED_CONVERSION;
    return REFUSED_CONVERSION;
}

int32_t MBCSConverter(void *p, const char *in, uint16_t **out)
{
    OldInst *inst = p;
    int32_t lang = OI_LANG(inst);
    int32_t rc = 0;

    if (wide_language(lang))
        rc = rm_MBCSToUnicode(OI_ROMMGR(inst), (uint32_t)lang, in, out);
    else
        *out = (uint16_t *)in;

    if (rc == 0)
        return 0;

    OI_REFUSED(inst)     = REFUSED_CONVERSION;
    OI_REFUSEDALL(inst) |= REFUSED_CONVERSION;
    return REFUSED_CONVERSION;
}

/* ---- the filters ----------------------------------------------------- */

int32_t FilterText2(void *p, void *filter, const char *text, char **out)
{
    OldInst *inst = p;
    *out = fm_filterTextByHandle(OI_FILTERMGR(inst), filter, text);
    return 0;
}

/* Whether the SSML filter in particular is on. The manager is asked for
   every filter it has and each description compared by name, because the
   number a filter carries is not fixed. */
int8_t CheckSSMLFilterActive(void *mgr)
{
    uint32_t ids[FILTERS_AT_ONCE];
    uint32_t n = FILTERS_AT_ONCE;
    char     description[DESCRIPTION_ROOM];
    uint32_t i;
    int8_t   active = 0;

    if (mgr == 0)
        return 0;

    fm_getAvailableFilters(mgr, 0, ids, &n);
    for (i = 0; i < n; i++) {
        fm_getFilterDescription(mgr, 0, ids[i], description);
        if (strcmp(description, SSML_FILTER_NAME) != 0)
            continue;
        active = fm_isActiveById(mgr, ids[i]) ? 1 : 0;
        break;
    }

    return active;
}

int8_t CheckFilterActive(void *mgr, int32_t lang, uint32_t id)
{
    if (mgr == 0)
        return 0;
    return fm_isActive(mgr, lang, id) ? 1 : 0;
}

int8_t CheckFilterAutoload(void *mgr, int32_t lang, uint32_t id)
{
    if (mgr == 0)
        return 0;
    return fm_isAutoload(mgr, lang, id) ? 1 : 0;
}

int8_t isFilterUsable(const char *name, void *mgr, int32_t lang, uint32_t id)
{
    if (mgr == 0)
        return 0;
    return fm_isUsable(mgr, name, lang, id) ? 1 : 0;
}

/* Wrap the text in the annotations that turn a filter on before it and off
   after it, unless it already carries them. Answers one whether or not it
   had to do anything; only a missing text answers nought. */
int32_t addFilterAnnotation(const char *text, char **out, int32_t id,
                            int32_t own)
{
    char     on[ANNO_ROOM], off[ANNO_ROOM];
    uint32_t added;

    if (text == 0)
        return 0;

    if (own == 0) {
        sprintf(on, ANNO_GENERAL_ON, id);
        sprintf(off, ANNO_GENERAL_OFF, id);
    } else {
        sprintf(on, ANNO_OWN_ON, id);
        sprintf(off, ANNO_OWN_OFF, id);
    }
    added = (uint32_t)(strlen(on) + strlen(off));

    if (strstr(text, on) != 0 || strstr(text, off) != 0)
        return 1;

    *out = cpp_new((uint32_t)strlen(text) + added + 1);
    memset(*out, 0, strlen(text) + added + 1);
    sprintf(*out, "%s%s%s", on, text, off);
    return 1;
}

/* Turn on every filter this language has that says it should be on by
   itself, wrapping the text in the annotation each one wants. Answers
   whether any of them was. */
int32_t enableFilter(void *p, int32_t lang, const char *text, char **out)
{
    OldInst *inst = p;
    uint32_t ids[FILTERS_AT_ONCE];
    uint32_t n = FILTERS_AT_ONCE;
    char     description[DESCRIPTION_ROOM];
    char    *wrapped = 0;
    uint32_t i;
    int32_t  any = 0;

    api_get_available_filters(OI_NEW(inst), lang, ids, &n);
    if (n == 0)
        return 0;

    for (i = 0; i < n; i++) {
        api_get_filter_description(OI_NEW(inst), lang, ids[i], description);

        if (CheckFilterActive(OI_FILTERMGR(inst), lang, (uint32_t)i) == 1)
            continue;
        if (!CheckFilterAutoload(OI_FILTERMGR(inst), lang, ids[i]))
            continue;
        if (isFilterUsable(text, OI_FILTERMGR(inst), lang, ids[i]) != 1)
            continue;

        addFilterAnnotation(text, &wrapped, (int32_t)i, lang);
        fm_getFilterDependencies(OI_FILTERMGR(inst), lang, ids[i]);
        if (*out == 0)
            *out = wrapped;
        any = 1;
    }

    return any;
}

/* ---- what a caller reaches all of this through ----------------------- */

/* The eight published calls about filters. Six of them are one line over
   the newer interface underneath; two are not.
 *
 * Registering and unregistering go straight to the manager rather than
 * through that interface, because there is nothing in the newer one to go
 * through: the pair was never given a `2' form. And making one waits for
 * the engine to fall quiet first, since loading a filter while a sentence
 * is being spoken would change the text underneath it.
 *
 * `eciActivateFilter' answers a refusal over a filter it has just turned
 * on, which is the original's: the manager's answer is always the
 * refusal, and anything but nought is reported as one. See
 * `fm_activateById' in src/eci/ssml/eci_filtermanager.c. */

extern int ev_sendParameters(OldInst *h);
extern STDCALL int32_t api_check_synth(void *h2)
    MANGLED("_eciCheckSynthesizing2@4");
extern STDCALL int32_t api_synthesize(void *h2) MANGLED("_eciSynthesize2@4");
extern STDCALL int32_t api_synchronize(void *h2)
    MANGLED("_eciSynchronize2@4");
extern STDCALL int32_t api_activate_filter(void *h2, int32_t id)
    MANGLED("_eciActivateFilter2@8");
extern STDCALL int32_t api_deactivate_filter(void *h2, int32_t id)
    MANGLED("_eciDeactivateFilter2@8");
extern STDCALL int32_t api_delete_filter(void *h2, void *filter)
    MANGLED("_eciDeleteFilter2@8");
extern STDCALL int32_t api_new_filter(void *h2, int32_t lang, int32_t id,
                                      void **out)
    MANGLED("_eciNewFilter2@16");
extern STDCALL int32_t api_update_filter(void *h2, void *filter, void *a,
                                         int32_t b, void *c, int32_t d,
                                         int32_t e)
    MANGLED("_eciUpdateFilter2@28");
extern STDCALL int32_t eo_getParam(OldInst *h, int32_t which)
    MANGLED("_eciGetParam@8");
extern THIS int fm_registerFilter(void *m, void *attrib, uint32_t id,
                                  void *entry, int8_t autoload);
extern THIS int fm_unregisterFilter(void *m, void *attrib, uint32_t id);

/* What the flat calls answer when there is no instance, and the one that
   says the engine was busy. */
#define ECI_FILTER_REFUSED   6
#define ECI_FILTER_NO_HANDLE 4
#define ECI_BUSY_SPEAKING    3
#define ECI_CONVERSION_REFUSED 3
#define ECI_FILTER_NO_ROOM     2

/* Which parameter says the language, and what it means to ask for the one
   in force rather than a particular one. */
#define ECI_PARAM_LANGUAGE   9
#define FILTER_ANY_LANGUAGE  1

/* And the bit the instance sets to say it refused because it was busy. */
#define REFUSED_WHILE_BUSY   0x2000

int32_t STDCALL es_registerFilter(OldInst *h, uint32_t id, void *entry,
                                 void *attrib, int32_t autoload)
{
    if (!h)
        return ECI_FILTER_REFUSED;

    return fm_registerFilter(OI_FILTERMGR(h), attrib, id, entry,
                             autoload != 0);
}

int32_t STDCALL es_unregisterFilter(OldInst *h, uint32_t id, void *attrib)
{
    if (!h)
        return ECI_FILTER_REFUSED;

    return fm_unregisterFilter(OI_FILTERMGR(h), attrib, id);
}

/* Turning one on and throwing away what happened, which is what the older
   call did: it answers nought whatever the manager said. */
int32_t STDCALL es_setFilter(OldInst *h, int32_t id)
{
    if (!h)
        return ECI_FILTER_NO_HANDLE;

    api_activate_filter(OI_NEW(h), id);
    return 0;
}

int32_t STDCALL es_activateFilter(OldInst *h, int32_t id)
{
    if (!h)
        return ECI_FILTER_REFUSED;

    if (api_activate_filter(OI_NEW(h), id) != 0)
        return ECI_FILTER_REFUSED;
    return 0;
}

int32_t STDCALL es_deactivateFilter(OldInst *h, int32_t id)
{
    if (!h)
        return ECI_FILTER_REFUSED;

    if (api_deactivate_filter(OI_NEW(h), id) != 0)
        return ECI_FILTER_REFUSED;
    return 0;
}

int32_t STDCALL es_deleteFilter(OldInst *h, void *filter)
{
    if (!h)
        return 0;

    api_delete_filter(OI_NEW(h), filter);
    return 0;
}

/* One is a filter's handle rather than a number, so a caller keeps what
   comes back. A language of one means whatever is in force.
 *
 * Everything queued is spoken out first. A filter loaded halfway through a
 * sentence would be asked to filter the rest of it and not the start,
 * so the call refuses while the engine is speaking and only then waits. */
void *STDCALL es_newFilter(OldInst *h, int32_t id, int32_t language)
{
    void   *filter = 0;
    int32_t rc = -1;
    int32_t lang;

    if (!h)
        return 0;

    if (api_check_synth(OI_NEW(h)) == ECI_BUSY_SPEAKING) {
        OI_REFUSED(h) = REFUSED_WHILE_BUSY;
        OI_REFUSEDALL(h) |= REFUSED_WHILE_BUSY;
        return 0;
    }

    ev_sendParameters(h);
    api_synthesize(OI_NEW(h));
    api_synchronize(OI_NEW(h));

    if (language == FILTER_ANY_LANGUAGE)
        lang = 0;
    else
        lang = eo_getParam(h, ECI_PARAM_LANGUAGE);

    if (lang >= 0)
        rc = api_new_filter(OI_NEW(h), lang, id, &filter);

    return rc < 0 ? 0 : filter;
}

/* Two strings handed to a filter, whatever it makes of them. Each may be
   UTF-16, so each is converted first and the copy freed afterwards --
   except where the converter handed back what it was given, which is how
   it says a string was already in the language's own code set.
 *
 * What comes back is nought whether the filter accepted them or not: the
 * answer is worked out, stored, and then thrown away for a nought. That is
 * the original's. */
int32_t STDCALL es_updateFilter(OldInst *h, void *filter, const char *a,
                                const char *b)
{
    char   *ca = 0;
    char   *cb = 0;
    int32_t na = 0;
    int32_t nb = 0;
    int32_t lang;

    if (!h || !filter)
        return ECI_FILTER_NO_HANDLE;

    lang = eo_getParam(h, ECI_PARAM_LANGUAGE);

    if (UnicodeConverter(h, (const uint16_t *)a, &ca, 1))
        return ECI_CONVERSION_REFUSED;

    if (a != ca) {
        char *copy = cpp_new((uint32_t)strlen(ca) + 1);

        if (copy == 0)
            return ECI_FILTER_NO_ROOM;
        strcpy(copy, ca);
        ca = copy;
    } else {
        ca = (char *)a;
    }

    cb = (char *)b;
    if (UnicodeConverter(h, (const uint16_t *)b, &cb, 1)) {
        if (a != ca)
            cpp_delete(ca);
        return ECI_CONVERSION_REFUSED;
    }

    if (ca != 0)
        na = (int32_t)strlen(ca);
    if (cb != 0)
        nb = (int32_t)strlen(cb);

    api_update_filter(OI_NEW(h), filter, ca, na, cb, nb, lang);

    if (a != ca)
        cpp_delete(ca);

    return 0;
}

ALIAS_N("_eciRegisterFilter@20", "es_registerFilter", 20);
ALIAS_N("_eciUnregisterFilter@12", "es_unregisterFilter", 12);
ALIAS_N("_eciSetFilter@8", "es_setFilter", 8);
ALIAS_N("_eciActivateFilter@8", "es_activateFilter", 8);
ALIAS_N("_eciDeactivateFilter@8", "es_deactivateFilter", 8);
ALIAS_N("_eciDeleteFilter@8", "es_deleteFilter", 8);
ALIAS_N("_eciNewFilter@12", "es_newFilter", 12);
ALIAS_N("_eciUpdateFilter@16", "es_updateFilter", 16);
