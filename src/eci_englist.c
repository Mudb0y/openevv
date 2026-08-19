/* Which languages the engine has, and what it keeps for each.
 *
 * The list is a flat array indexed by language and dialect together, sized
 * once at construction from whatever the built-in settings declare. Finding
 * the widest language and dialect numbered there means walking every section
 * of the ini before anything else can happen, which is why the constructor
 * does that first and then allocates.
 *
 * A language identifier packs the language into the top half of a word and
 * the dialect into the bottom. Languages are numbered from one and dialects
 * from nought, which is why the index arithmetic takes one off the language
 * and not off the dialect.
 *
 * Walking the languages is walking the ini's sections, skipping any whose
 * name does not start with a digit -- those are the settings that belong to
 * no language in particular.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_engine.h"

/* The reader is embedded in the list, so its size has to be right. */

/* A language identifier: the packed word, then the same as text. */
#define LANG_PACKED(l)  (*(const int32_t *)(l))
#define LANG_TEXT(l)    ((char *)(l) + 4)
#define LANG_OF(l)      ((int32_t)((LANG_PACKED(l) >> 16) & 0xff))
#define DIALECT_OF(l)   ((int32_t)(LANG_PACKED(l) & 0xff))

/* How long a section name this will carry into an identifier. */
#define LANG_TEXT_ROOM 0xc

/* What a slot's own data records about its callbacks when the ini says
   nothing. */
#define CALLBACK_DEFAULT 0x3f

/* How anything in a slot is told to delete itself: through slot nought of
   its own table, with the object in ecx and "yes, free it" pushed. */
typedef THIS void *(*DeleteFn)(void *self, int32_t freeIt);
#define DELETE_ITSELF(p) \
    ((*(DeleteFn *)(*(void ***)(p)))((p), 1))

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

extern THIS IniFileReader *ini_ctor(IniFileReader *r)
    MANGLED("??0IniFileReader@@QAE@XZ");
extern THIS void ini_dtor(IniFileReader *r) MANGLED("??1IniFileReader@@QAE@XZ");
extern THIS const char *ini_getFirstSection(IniFileReader *r)
    MANGLED("?getFirstSection@IniFileReader@@QAEPBDXZ");
extern THIS const char *ini_getNextSection(IniFileReader *r)
    MANGLED("?getNextSection@IniFileReader@@QAEPBDXZ");
extern THIS char *ini_getString(IniFileReader *r, const char *section,
                                const char *key)
    MANGLED("?getString@IniFileReader@@QAEPBDPBD0@Z");
extern THIS void li_setPackedInt(void *l)
    MANGLED("?setPackedInt@LangIdentifier@@AAEXXZ");

THIS int32_t eng_getFirstLanguage(EngineList *el, void *lang);
THIS int32_t eng_getNextLanguage(EngineList *el, void *lang);
THIS void    eng_setMaxLangAndDialect(EngineList *el);
THIS int32_t eng_rangeCheck(EngineList *el, const void *lang);

/* Where a language and dialect land in the flat array. */
static int32_t eng_slot(const EngineList *el, const void *lang)
{
    return (LANG_OF(lang) - 1) * el->dialects + DIALECT_OF(lang);
}

/* Take a section name, without its brackets, into an identifier, and work
   the packed word back out of it. */
static int32_t eng_nameToLang(const char *section, void *lang)
{
    size_t n = strlen(section);
    char  *name = (char *)cpp_new((uint32_t)n + 1);

    if (!name)
        return 0;

    strncpy(name, section + 1, n - 2);
    name[n - 2] = 0;

    *(int32_t *)lang = 0;
    strncpy(LANG_TEXT(lang), name, LANG_TEXT_ROOM);
    *((char *)lang + 0x10) = 0;
    li_setPackedInt(lang);

    cpp_delete(name);
    return 1;
}

/* The first section whose name starts with a digit. */
THIS int32_t eng_getFirstLanguage(EngineList *el, void *lang)
{
    const char *section = ini_getFirstSection(&el->ini);

    while (section && !isdigit((unsigned char)section[1]))
        section = ini_getNextSection(&el->ini);

    if (!section)
        return 0;

    return eng_nameToLang(section, lang);
}

/* And the one after it, whatever its name. */
THIS int32_t eng_getNextLanguage(EngineList *el, void *lang)
{
    const char *section = ini_getNextSection(&el->ini);

    if (!section)
        return 0;

    return eng_nameToLang(section, lang);
}

/* Walk every language the ini declares and remember the widest of each
   number, so the array can be sized to hold all of them. */
THIS void eng_setMaxLangAndDialect(EngineList *el)
{
    uint8_t lang[0x14];

    *(int32_t *)lang = 0;
    memset(LANG_TEXT(lang), 0, sizeof(lang) - 4);

    if (!eng_getFirstLanguage(el, lang))
        return;

    el->langs = (uint8_t)(LANG_PACKED(lang) >> 16);
    el->dialects = (uint8_t)(LANG_PACKED(lang) & 0xff);

    while (eng_getNextLanguage(el, lang)) {
        if (LANG_OF(lang) > el->langs)
            el->langs = (uint8_t)(LANG_PACKED(lang) >> 16);
        if (DIALECT_OF(lang) > el->dialects)
            el->dialects = (uint8_t)(LANG_PACKED(lang) & 0xff);
    }
}

/* Dialects are numbered from nought, so the count is one more than the
   widest seen. A list that cannot allocate says it holds nothing rather
   than pretending. */
THIS EngineList *eng_ctor(EngineList *el)
{
    el->data = 0;
    el->langs = 0;
    el->dialects = 0;

    ini_ctor(&el->ini);
    eng_setMaxLangAndDialect(el);

    if (el->langs != 0) {
        el->dialects = (uint8_t)(el->dialects + 1);
        el->data = cpp_new((uint32_t)(el->langs * el->dialects)
                           * (uint32_t)sizeof *el->data);
        if (el->data) {
            memset(el->data, 0,
                   (size_t)(el->langs * el->dialects) * sizeof *el->data);
        } else {
            el->langs = 0;
            el->dialects = 0;
        }
    }

    return el;
}

/* Everything in it is told to delete itself, through its own table. */
THIS void eng_dtor(EngineList *el)
{
    if (el->data) {
        void  **at = el->data;
        int16_t i;

        for (i = 0; i < el->langs * el->dialects; i++) {
            if (*at) {
                void *one = *at;

                if (one)
                    DELETE_ITSELF(one);
                *at = 0;
            }
            at++;
        }

        cpp_delete(el->data);
        el->data = 0;
    }

    ini_dtor(&el->ini);
}

/* Is this a language and dialect the list has room for? */
THIS int32_t eng_rangeCheck(EngineList *el, const void *lang)
{
    if (LANG_OF(lang) > el->langs)
        return 0;
    if (LANG_OF(lang) <= 0)
        return 0;
    if (DIALECT_OF(lang) > el->dialects - 1)
        return 0;
    if (DIALECT_OF(lang) < 0)
        return 0;
    return 1;
}

THIS void *eng_getData(EngineList *el, const void *lang)
{
    if (!eng_rangeCheck(el, lang))
        return 0;
    return el->data[eng_slot(el, lang)];
}

/* What the ini says about this language's callbacks goes into the slot's own
   data on the way in. A language that says nothing gets all of them. */
THIS int32_t eng_setCallbackFnFlag(EngineList *el, const void *lang,
                                  void *data)
{
    int32_t flag = -1;
    char   *said = ini_getString(&el->ini, LANG_TEXT(lang), "CallbackFlag");

    if (said) {
        sscanf(said, "%10x", &flag);
        cpp_delete(said);
    } else {
        flag = CALLBACK_DEFAULT;
    }

    ((ListData *)data)->callbacks = flag;
    return 1;
}

THIS int32_t eng_setData(EngineList *el, const void *lang, void *data)
{
    if (!eng_rangeCheck(el, lang))
        return 0;

    eng_setCallbackFnFlag(el, lang, data);
    el->data[eng_slot(el, lang)] = data;
    return 1;
}

/* Told to delete itself, then forgotten. */
THIS void eng_removeData(EngineList *el, const void *lang)
{
    void *one;

    if (!eng_rangeCheck(el, lang))
        return;
    if (!el->data[eng_slot(el, lang)])
        return;

    one = el->data[eng_slot(el, lang)];
    if (one)
        DELETE_ITSELF(one);

    el->data[eng_slot(el, lang)] = 0;
}

ALIAS("??0EngineList@@QAE@XZ", "eng_ctor");
ALIAS("??1EngineList@@QAE@XZ", "eng_dtor");
ALIAS("?getData@EngineList@@QAEPAVEngineListData@@QBVLangIdentifier@@@Z",
      "eng_getData");
ALIAS("?setData@EngineList@@QAEHQBVLangIdentifier@@PAVEngineListData@@@Z",
      "eng_setData");
ALIAS("?removeData@EngineList@@QAEXQBVLangIdentifier@@@Z", "eng_removeData");
ALIAS("?rangeCheck@EngineList@@AAEHQBVLangIdentifier@@@Z", "eng_rangeCheck");
ALIAS("?getFirstLanguage@EngineList@@QAEHPAVLangIdentifier@@@Z",
      "eng_getFirstLanguage");
ALIAS("?getNextLanguage@EngineList@@QAEHPAVLangIdentifier@@@Z",
      "eng_getNextLanguage");
ALIAS("?setMaxLangAndDialect@EngineList@@AAEXXZ", "eng_setMaxLangAndDialect");
ALIAS("?setCallbackFnFlag@EngineList@@AAEHQBVLangIdentifier@@PAVEngineListData@@@Z",
      "eng_setCallbackFnFlag");
