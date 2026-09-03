/* User dictionaries.

   A caller can give the engine a list of its own pronunciations. This is the
   whole of how that is asked for: making a dictionary, filling it, saving and
   loading it, looking things up in it and walking through it.

   None of it does the work. A dictionary is really two dictionaries -- one
   the engine keeps and one the romanizer keeps for the same language -- and
   everything here is a matter of holding the pair together, converting the
   caller's text into whatever each side wants, and handing the question on.
   The record made by newDict is that pair plus what it takes to reach them.

   Which of the two answers depends on the volume asked for. Volumes nought
   to two are the engine's; volume three is the romanizer's. The extended
   calls, the ones that also carry a part of speech, are the romanizer's
   whatever volume they name.

   The thread keeps three scratch pointers for converted results, one for a
   lookup and two for a walk, so that what a caller is handed stays where it
   is until the next call of the same kind. Each is freed as the next call
   starts rather than at the end of the last, which is why they are fields
   rather than locals.

   Not verified. Nothing in the harness opens a dictionary; driving one
   through the published interface was tried and the call that makes a
   dictionary active refuses, so the audio never changes and there is nothing
   to compare. This is transcription read carefully off the original and
   nothing more than that. */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"

#define OK               0
#define ERR_BAD_ARG    (-3)
#define ERR_NO_MEMORY  (-2)
#define ERR_BAD_TEXT   (-7)
#define ERR_NO_LANG    (-14)
#define ERR_ENGINE     (-15)
/* Not an error in the same family: a walk that has run out says five. */
#define DICT_NO_ENTRY    5

/* The volumes the engine keeps, and the one the romanizer keeps. */
#define VOLUME_ENGINE_LAST 2
#define VOLUME_ROMANIZER   3

/* The one text mode that means anything other than plain bytes. */
#define TEXT_MODE_ROM     10

/* Slots of the engine's own table. All stdcall with the engine pushed like
   any other argument. */
#define ENG_NEW_DICT      0x6c
#define ENG_SET_DICT      0x74
#define ENG_DELETE_DICT   0x78
#define ENG_LOAD_VOLUME   0x7c
#define ENG_SAVE_VOLUME   0x80
#define ENG_UPDATE        0x84
#define ENG_FIND_FIRST    0x88
#define ENG_FIND_NEXT     0x8c
#define ENG_LOOKUP        0x90

/* And of the romanizer instance's, which are thiscall. */
#define ROM_NEW_DICT      0x48
#define ROM_DELETE_DICT   0x4c
#define ROM_SET_DICT      0x50
#define ROM_LOAD_VOLUME   0x54
#define ROM_SAVE_VOLUME   0x58
#define ROM_LOOKUP_EXT    0x5c
#define ROM_UPDATE_EXT    0x60
#define ROM_FIND_FIRST_EXT 0x64
#define ROM_FIND_NEXT_EXT 0x68
#define ROM_TO_CHAR       0x74
#define ROM_TO_ECI        0x78

#define ENGCALL __attribute__((stdcall))
/* A slot of an object's table of virtual functions, by the byte it sat at
   in the original. A slot is a pointer, not four bytes. */
#define VT_AT(o, off) (((void **)*(void ***)(o))[(off) / 4])

typedef ENGCALL void *(*EngNewDict)(void *engine);
typedef ENGCALL void (*EngSetDict)(void *engine, void *dict);
typedef ENGCALL void (*EngDeleteDict)(void *engine, void *dict);
typedef ENGCALL int32_t (*EngVolume)(void *engine, void *dict, int32_t vol,
                                     const char *file);
typedef ENGCALL int32_t (*EngUpdate)(void *engine, void *dict, int32_t vol,
                                     char *key, char *value);
typedef ENGCALL void (*EngFind)(void *engine, void *dict, int32_t vol,
                                char **key, char **value);
typedef ENGCALL char *(*EngLookup)(void *engine, void *dict, int32_t vol,
                                   char *key);

typedef THIS void *(*RomNewDict)(void *rom);
typedef THIS void (*RomSetDict)(void *rom, void *dict);
typedef THIS int32_t (*RomVolume)(void *rom, void *dict, int32_t vol,
                                  const char *file);
typedef THIS int32_t (*RomExt)(void *rom, void *dict, int32_t vol, void *a,
                               void *b, void *c, void *d, void *e);
typedef THIS int32_t (*RomToChar)(void *rom, void *in, char **out);
typedef THIS int32_t (*RomToECI)(void *rom, const char *in, char **out);

/* What newDict hands back: the language it is for, the engine and its
   dictionary, and the romanizer and its dictionary. Either romanizer half
   may be missing. */
typedef struct {
    int32_t  language;   /* +0x00 */
    void    *engine;     /* +0x04 */
    void    *engDict;    /* +0x08 */
    void    *rom;        /* +0x0c, may be null */
    void    *romDict;    /* +0x10 */
} Dict;

/* Where the engine array keeps what it knows about a language. Only the
   field naming the dictionary in play is reached from here. */
#define ED_ACTIVE(d) (*(void **)((char *)(d) + 0x14))

extern THIS void lang_setString(LangIdentifier *l)
    MANGLED("?setString@LangIdentifier@@AAEXXZ");
extern THIS void *ea_getEngine(void *a, const LangIdentifier *l)
    MANGLED("?getEngine@EngineArray@@QAEPAVEngineWrapper@@QBVLangIdentifier@@@Z");
extern THIS void *ea_getEngineData(void *a, const LangIdentifier *l)
    MANGLED("?getEngineData@EngineArray@@QAEPAVEngineData@@QBVLangIdentifier@@@Z");
extern THIS void *rz_getRom(void *r, uint32_t language)
    MANGLED("?getRom@RomanizerManager@@QAEPAVRomInstance@@K@Z");

/* Name a language by its number, the way every one of these begins. */
static void std_name(LangIdentifier *l, int32_t language)
{
    l->packed = (uint32_t)language;
    l->packed = (uint32_t)language;
    lang_setString(l);
}

/* ---- turning the caller's text into what each side wants ---- */

/* From whatever the caller handed down into plain bytes.

   Mode nought means it already is bytes and only wants copying. Mode ten
   means it is the romanizer's own form, and if the flag says so the
   romanizer is asked to make bytes of it first. Anything else is refused. */
THIS int32_t std_convertToChar(SynthThread *t, void *in, int32_t len,
                               int32_t mode, char **out, void *rom,
                               int32_t translate)
{
    int32_t rc = ERR_BAD_ARG;
    char *made = 0;

    (void)t;
    if (!in || len <= 0 || !out)
        return rc;
    rc = ERR_BAD_TEXT;

    if (mode == 0) {
        made = (char *)cpp_new((uint32_t)len + 1);
        if (made) {
            memcpy(made, in, (size_t)len);
            made[len] = 0;
            *out = made;
            rc = OK;
        } else {
            rc = ERR_NO_MEMORY;
        }
        return rc;
    }
    if (mode != TEXT_MODE_ROM)
        return rc;

    {
        char *text = 0;
        int32_t n;

        if (translate == 1) {
            void *inst = *(void **)((char *)rom + 0x0c);
            RomToChar convert = (RomToChar)VT_AT(inst, ROM_TO_CHAR);

            if (!convert(inst, in, &text))
                return ERR_BAD_ARG;
        } else {
            text = (char *)in;
        }

        n = (int32_t)strlen(text);
        made = (char *)cpp_new((uint32_t)n + 1);
        if (!made)
            return ERR_NO_MEMORY;
        memcpy(made, text, (size_t)n);
        made[n] = 0;
        *out = made;
        rc = OK;
    }
    return rc;
}

/* And back the other way, with the length as well, because the caller is
   given a buffer rather than a string. */
THIS int32_t std_convertToECIinputText(SynthThread *t, const char *in,
                                       int32_t mode, void **out,
                                       int32_t *outLen, void *rom,
                                       int32_t translate)
{
    int32_t rc = ERR_BAD_ARG;
    char *made = 0;

    (void)t;
    if (!in || !out || !outLen)
        return rc;
    rc = ERR_BAD_TEXT;
    *out = 0;
    *outLen = 0;

    if (mode == 0) {
        made = (char *)cpp_new((uint32_t)strlen(in) + 1);
        if (!made)
            return ERR_NO_MEMORY;
        strcpy(made, in);
        *out = made;
        *outLen = (int32_t)strlen(in) + 1;
        return OK;
    }
    if (mode != TEXT_MODE_ROM)
        return rc;

    {
        char *text = 0;
        int32_t n;

        if (translate == 1) {
            void *inst = *(void **)((char *)rom + 0x0c);
            RomToECI convert = (RomToECI)VT_AT(inst, ROM_TO_ECI);

            if (!convert(inst, in, &text))
                return ERR_BAD_ARG;
        } else {
            text = (char *)in;
        }

        n = (int32_t)strlen(text);
        made = (char *)cpp_new((uint32_t)n + 1);
        if (!made)
            return ERR_NO_MEMORY;
        strcpy(made, text);
        *out = made;
        *outLen = n + 1;
        rc = OK;
    }
    return rc;
}

/* ---- making and unmaking ---- */

/* Ask both sides for a dictionary of their own and hold the pair together.
   The romanizer half is allowed to be missing; the engine half is not. */
THIS int32_t std_newDict(SynthThread *t, int32_t language, void **out)
{
    LangIdentifier want;
    int32_t rc = ERR_BAD_ARG;
    void *lock, *engine, *engDict, *rom = 0, *romDict = 0;
    Dict *rec;

    std_name(&want, language);
    if (!out)
        return rc;
    *out = 0;
    rc = ERR_NO_LANG;

    lock = ST_LOCK(t);
    sy_mutexWait(lock, -1);
    engine = ea_getEngine(ST_ENGINES(t), &want);
    sy_mutexRelease(lock);
    if (!engine)
        return rc;

    rc = ERR_NO_MEMORY;
    engDict = ((EngNewDict)VT_AT(engine, ENG_NEW_DICT))(engine);
    if (!engDict)
        return rc;

    rom = rz_getRom(ST_ROMAN(t), (uint32_t)language);
    if (rom)
        romDict = ((RomNewDict)VT_AT(rom, ROM_NEW_DICT))(rom);

    rec = (Dict *)cpp_new(sizeof(Dict));
    if (rec) {
        rec->language = language;
        rec->engine = engine;
        rec->engDict = engDict;
        rec->rom = rom;
        rec->romDict = romDict;
    }
    if (!rec)
        return rc;
    *out = rec;
    return OK;
}

/* Give both halves back. If this was the dictionary in play, the engine
   array is told it no longer is. */
THIS int32_t std_deleteDict(SynthThread *t, Dict *dict)
{
    LangIdentifier want;
    int32_t rc = ERR_BAD_ARG;
    void *data;

    if (!dict)
        return rc;
    std_name(&want, dict->language);
    rc = OK;

    data = ea_getEngineData(ST_ENGINES(t), &want);
    if (ED_ACTIVE(data) == dict)
        ED_ACTIVE(data) = 0;

    ((EngDeleteDict)VT_AT(dict->engine, ENG_DELETE_DICT))(dict->engine,
                                                          dict->engDict);
    rc = OK;
    if (dict->rom)
        ((RomSetDict)VT_AT(dict->rom, ROM_DELETE_DICT))(dict->rom,
                                                        dict->romDict);
    cpp_delete(dict);
    return rc;
}

/* ---- which one is in play ---- */

THIS int32_t std_activateDict(SynthThread *t, Dict *dict)
{
    LangIdentifier want;
    int32_t rc = ERR_BAD_ARG;
    void *data;

    if (!dict)
        return rc;
    std_name(&want, dict->language);
    data = ea_getEngineData(ST_ENGINES(t), &want);
    ED_ACTIVE(data) = dict;

    ((EngSetDict)VT_AT(dict->engine, ENG_SET_DICT))(dict->engine,
                                                    dict->engDict);
    rc = OK;
    if (dict->rom)
        ((RomSetDict)VT_AT(dict->rom, ROM_SET_DICT))(dict->rom,
                                                     dict->romDict);
    rc = OK;
    return rc;
}

THIS int32_t std_deactivateDict(SynthThread *t, Dict *dict)
{
    LangIdentifier want;
    int32_t rc = ERR_BAD_ARG;
    void *data;

    if (!dict)
        return rc;
    std_name(&want, dict->language);
    data = ea_getEngineData(ST_ENGINES(t), &want);
    ED_ACTIVE(data) = 0;

    ((EngSetDict)VT_AT(dict->engine, ENG_SET_DICT))(dict->engine, 0);
    rc = OK;
    if (dict->rom)
        ((RomSetDict)VT_AT(dict->rom, ROM_SET_DICT))(dict->rom, 0);
    rc = OK;
    return rc;
}

THIS int32_t std_getActiveDict(SynthThread *t, int32_t language, void **out)
{
    LangIdentifier want;
    int32_t rc = ERR_BAD_ARG;
    void *data;

    std_name(&want, language);
    if (!out)
        return rc;
    rc = ERR_NO_LANG;
    data = ea_getEngineData(ST_ENGINES(t), &want);
    if (!data)
        return rc;
    rc = OK;
    *out = ED_ACTIVE(data);
    return rc;
}

/* Which language a dictionary was made for. It carries that itself. */
THIS int32_t std_getDictLanguage(SynthThread *t, Dict *dict, int32_t *out)
{
    int32_t rc = ERR_BAD_ARG;

    (void)t;
    if (dict) {
        rc = OK;
        *out = dict->language;
    }
    return rc;
}

/* ---- files ---- */

/* Read a volume in from a file, from whichever side owns that volume. */
THIS int32_t std_loadDictVolume(SynthThread *t, Dict *dict, int32_t volume,
                                const char *file)
{
    int32_t rc = ERR_BAD_ARG;

    (void)t;
    if (!dict || !file)
        return rc;

    if (volume >= 0 && volume <= VOLUME_ENGINE_LAST) {
        EngVolume load = (EngVolume)VT_AT(dict->engine, ENG_LOAD_VOLUME);

        rc = OK;
        if (load(dict->engine, dict->engDict, volume, file))
            rc = ERR_ENGINE;
        return rc;
    }
    if (volume != VOLUME_ROMANIZER || !dict->rom)
        return rc;
    {
        RomVolume load = (RomVolume)VT_AT(dict->rom, ROM_LOAD_VOLUME);

        rc = OK;
        if (load(dict->rom, dict->romDict, volume, file))
            rc = ERR_ENGINE;
    }
    return rc;
}

THIS int32_t std_saveDictVolume(SynthThread *t, Dict *dict, int32_t volume,
                                const char *file)
{
    int32_t rc = ERR_BAD_ARG;

    (void)t;
    if (!dict || !file)
        return rc;

    if (volume >= 0 && volume <= VOLUME_ENGINE_LAST) {
        EngVolume save = (EngVolume)VT_AT(dict->engine, ENG_SAVE_VOLUME);

        rc = OK;
        if (save(dict->engine, dict->engDict, volume, file))
            rc = ERR_ENGINE;
        return rc;
    }
    if (volume != VOLUME_ROMANIZER || !dict->rom)
        return rc;
    {
        RomVolume save = (RomVolume)VT_AT(dict->rom, ROM_SAVE_VOLUME);

        rc = OK;
        if (save(dict->rom, dict->romDict, volume, file))
            rc = ERR_ENGINE;
    }
    return rc;
}

/* ---- entries ---- */

/* Put one entry in. Both halves of it are converted to bytes first and both
   copies given back afterwards, whether or not the engine took them. */
THIS int32_t std_updateDict(SynthThread *t, Dict *dict, int32_t volume,
                            void *key, int32_t keyLen, void *value,
                            int32_t valueLen, int32_t mode)
{
    int32_t rc = ERR_BAD_ARG;
    char *k = 0, *v = 0;

    if (!dict)
        return rc;
    if (volume < 0 || volume > VOLUME_ENGINE_LAST)
        return rc;
    if (!key || keyLen <= 0 || valueLen < 0)
        return rc;

    rc = std_convertToChar(t, key, keyLen, mode, &k, dict, 1);
    if (rc == OK && value)
        rc = std_convertToChar(t, value, valueLen, mode, &v, dict, 1);

    if (rc == OK) {
        EngUpdate update = (EngUpdate)VT_AT(dict->engine, ENG_UPDATE);

        if (update(dict->engine, dict->engDict, volume, k, v))
            rc = ERR_ENGINE;
    }

    if (v) {
        cpp_delete(v);
        v = 0;
    }
    if (k) {
        cpp_delete(k);
        k = 0;
    }
    return rc;
}

/* Look one up. What comes back is converted into the caller's own form and
   kept in the thread until the next lookup, so the caller can read it
   without copying it first. */
THIS int32_t std_lookupDict(SynthThread *t, Dict *dict, int32_t volume,
                            void *key, int32_t keyLen, void **out,
                            int32_t *outLen, int32_t mode)
{
    int32_t rc = ERR_BAD_ARG;
    char *k = 0, *found;

    if (!dict)
        return rc;
    if (volume < 0 || volume > VOLUME_ENGINE_LAST)
        return rc;
    if (!key || keyLen <= 0 || !out || !outLen)
        return rc;

    *outLen = 0;
    if (ST_LASTLOOKUP(t))
        cpp_delete(ST_LASTLOOKUP(t));
    *out = 0;
    ST_LASTLOOKUP(t) = 0;

    rc = std_convertToChar(t, key, keyLen, mode, &k, dict, 1);
    if (rc != OK)
        return rc;

    found = ((EngLookup)VT_AT(dict->engine, ENG_LOOKUP))(dict->engine,
                                                         dict->engDict,
                                                         volume, k);
    if (found) {
        rc = std_convertToECIinputText(t, found, mode,
                                       (void **)&ST_LASTLOOKUP(t), outLen,
                                       dict, 1);
        if (rc == OK)
            *out = ST_LASTLOOKUP(t);
    } else {
        rc = DICT_NO_ENTRY;
    }

    if (k) {
        cpp_delete(k);
        k = 0;
    }
    return rc;
}

/* Walk through a volume. The first and the next are the same routine with a
   flag, and both keep their two answers in the thread the same way a lookup
   keeps its one. */
THIS int32_t std_findDictEntry(SynthThread *t, Dict *dict, int32_t volume,
                               void **key, int32_t *keyLen, void **value,
                               int32_t *valueLen, int32_t mode, int32_t first)
{
    int32_t rc = ERR_BAD_ARG;
    char *k = 0, *v = 0;

    if (!dict)
        return rc;
    if (volume < 0 || volume > VOLUME_ENGINE_LAST)
        return rc;
    if (!key || !keyLen || !value || !valueLen)
        return rc;

    *keyLen = 0;
    *valueLen = 0;

    if (ST_LASTKEY(t))
        cpp_delete(ST_LASTKEY(t));
    *key = 0;
    ST_LASTKEY(t) = 0;
    if (ST_LASTVALUE(t))
        cpp_delete(ST_LASTVALUE(t));
    *value = 0;
    ST_LASTVALUE(t) = 0;

    {
        EngFind walk = (EngFind)VT_AT(dict->engine,
                                      first ? ENG_FIND_FIRST : ENG_FIND_NEXT);

        walk(dict->engine, dict->engDict, volume, &k, &v);
    }

    if (!k || !v)
        return DICT_NO_ENTRY;

    rc = std_convertToECIinputText(t, k, mode, (void **)&ST_LASTKEY(t),
                                   keyLen, dict, 1);
    if (rc != OK)
        return rc;
    rc = std_convertToECIinputText(t, v, mode, (void **)&ST_LASTVALUE(t),
                                   valueLen, dict, 1);
    if (rc != OK)
        return rc;
    *key = ST_LASTKEY(t);
    *value = ST_LASTVALUE(t);
    return rc;
}

THIS int32_t std_findFirstDictEntry(SynthThread *t, Dict *dict,
                                    int32_t volume, void **key,
                                    int32_t *keyLen, void **value,
                                    int32_t *valueLen, int32_t mode)
{
    return std_findDictEntry(t, dict, volume, key, keyLen, value, valueLen,
                             mode, 1);
}

THIS int32_t std_findNextDictEntry(SynthThread *t, Dict *dict, int32_t volume,
                                   void **key, int32_t *keyLen, void **value,
                                   int32_t *valueLen, int32_t mode)
{
    return std_findDictEntry(t, dict, volume, key, keyLen, value, valueLen,
                             mode, 0);
}

/* ---- the extended four ---- */

/* These carry a part of speech and a language as well, and they are the
   romanizer's whatever volume they name: the engine's own dictionaries have
   no room for either. All four are the same shape. */
static int32_t std_ext(SynthThread *t, Dict *dict, int32_t volume,
                       uint32_t slot, void *a, void *b, void *c, void *d,
                       void *e)
{
    int32_t rc = ERR_BAD_ARG;

    (void)t;
    if (!dict)
        return rc;
    if (volume != VOLUME_ROMANIZER)
        return rc;
    if (!a)
        return rc;
    if (!dict->rom)
        return rc;

    if (((RomExt)VT_AT(dict->rom, slot))(dict->rom, dict->romDict, volume,
                                         a, b, c, d, e))
        rc = DICT_NO_ENTRY;
    else
        rc = OK;
    return rc;
}

THIS int32_t std_lookupDictExt(SynthThread *t, Dict *dict, int32_t volume,
                               void *a, void *b, void *c, void *d, void *e)
{
    return std_ext(t, dict, volume, ROM_LOOKUP_EXT, a, b, c, d, e);
}

THIS int32_t std_updateDictExt(SynthThread *t, Dict *dict, int32_t volume,
                               void *a, void *b, void *c, void *d, void *e)
{
    return std_ext(t, dict, volume, ROM_UPDATE_EXT, a, b, c, d, e);
}

THIS int32_t std_findFirstDictEntryExt(SynthThread *t, Dict *dict,
                                       int32_t volume, void *a, void *b,
                                       void *c, void *d, void *e)
{
    return std_ext(t, dict, volume, ROM_FIND_FIRST_EXT, a, b, c, d, e);
}

THIS int32_t std_findNextDictEntryExt(SynthThread *t, Dict *dict,
                                      int32_t volume, void *a, void *b,
                                      void *c, void *d, void *e)
{
    return std_ext(t, dict, volume, ROM_FIND_NEXT_EXT, a, b, c, d, e);
}

ALIAS("?convertToChar@SynthThread@@QAEJPAXJJPAPAD0J@Z", "std_convertToChar");
ALIAS("?convertToECIinputText@SynthThread@@QAEJPBDJPAPAXPAJPAXJ@Z",
      "std_convertToECIinputText");
ALIAS("?newDict@SynthThread@@QAEJJPAPAX@Z", "std_newDict");
ALIAS("?deleteDict@SynthThread@@QAEJPAX@Z", "std_deleteDict");
ALIAS("?activateDict@SynthThread@@QAEJPAX@Z", "std_activateDict");
ALIAS("?deactivateDict@SynthThread@@QAEJPAX@Z", "std_deactivateDict");
ALIAS("?getActiveDict@SynthThread@@QAEJJPAPAX@Z", "std_getActiveDict");
ALIAS("?getDictLanguage@SynthThread@@QAEJPAXPAJ@Z", "std_getDictLanguage");
ALIAS("?loadDictVolume@SynthThread@@QAEJPAXJPBD@Z", "std_loadDictVolume");
ALIAS("?saveDictVolume@SynthThread@@QAEJPAXJPBD@Z", "std_saveDictVolume");
ALIAS("?updateDict@SynthThread@@QAEJPAXJ0J0JJ@Z", "std_updateDict");
ALIAS("?lookupDict@SynthThread@@QAEJPAXJ0JPAPAXPAJJ@Z", "std_lookupDict");
ALIAS("?findDictEntry@SynthThread@@AAEJPAXJPAPAXPAJ12JH@Z",
      "std_findDictEntry");
ALIAS("?findFirstDictEntry@SynthThread@@QAEJPAXJPAPAXPAJ12J@Z",
      "std_findFirstDictEntry");
ALIAS("?findNextDictEntry@SynthThread@@QAEJPAXJPAPAXPAJ12J@Z",
      "std_findNextDictEntry");
ALIAS("?lookupDictExt@SynthThread@@QAEJPAXJ0JPAPAXPAJPAW4ECIPartOfSpeech@@"
      "W4ECILanguageDialect@@@Z", "std_lookupDictExt");
ALIAS("?updateDictExt@SynthThread@@QAEJPAXJ0J0JW4ECIPartOfSpeech@@"
      "W4ECILanguageDialect@@@Z", "std_updateDictExt");
ALIAS("?findFirstDictEntryExt@SynthThread@@QAEJPAXJPAPAXPAJ12"
      "PAW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z",
      "std_findFirstDictEntryExt");
ALIAS("?findNextDictEntryExt@SynthThread@@QAEJPAXJPAPAXPAJ12"
      "PAW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z",
      "std_findNextDictEntryExt");
