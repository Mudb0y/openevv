/* Every phoneme the languages declare, read out of the built-in settings.
 *
 * One table per language, each a growing array of forty-eight byte records
 * read from lines named Phoneme0, Phoneme1 and so on until one is missing.
 * The first four bytes of a record are its name packed into a word, which is
 * also what it is searched by, so the array is sorted on that word once the
 * reading is done and looked up by halving afterwards.
 *
 * The record's line is fifteen numbers: four that go in as single bytes --
 * the name -- then three short ones and eight long ones. The language itself
 * is written into the record as well, though nothing here reads it back.
 *
 * The array doubles when it fills, plus one, so an array that started at
 * nought still grows.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "evv_arena.h"
#include "eci_engine.h"

/* One phoneme, and how many of the numbers on its line go where. */
#define RECORD_BYTES 0x30
#define NAME_BYTES   4

/* How the line is written. The last eight are four-byte fields of the record,
   so they are read as int and not as long: the two are the same width only
   where a long is four bytes. */
#define PHONEME_LINE \
    "%d %d %d %d %hd %hd %hd %d %d %d %d %d %d %d %d"
#define PHONEME_KEY "Phoneme%u"

/* The reader is built on the stack, so its size has to be right. */

typedef struct PhonemeData {
    const void *vt;        /* +0x00 */
    int32_t     callbacks; /* +0x04, the list fills this in */
    uint8_t    *records;   /* +0x08 */
    int16_t     room;      /* +0x0c */
    int16_t     count;     /* +0x0e */
} PhonemeData;

#define LANG_TEXT(l)    ((const char *)(l) + 4)
#define LANG_PACKED(l)  (*(const int32_t *)(l))

/* Where a record's name and its language sit. */
#define REC_AT(p, i)    ((p)->records + (i) * RECORD_BYTES)
#define REC_NAME(r)     (*(const int32_t *)(r))
#define REC_LANG(r)     (*(int32_t *)((r) + 0x0c))

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");
/* One slot each: how anything the list holds is told to delete itself. */
extern const void *vtbl_enginelistdata[1];

/* One slot: how the list tells a table to delete itself. */
extern const void *vtbl_phonemedata[1];

extern THIS IniFileReader *ini_ctor(IniFileReader *r)
    MANGLED("??0IniFileReader@@QAE@XZ");
extern THIS void ini_dtor(IniFileReader *r) MANGLED("??1IniFileReader@@QAE@XZ");
extern THIS char *ini_getString(IniFileReader *r, const char *section,
                                const char *key)
    MANGLED("?getString@IniFileReader@@QAEPBDPBD0@Z");

extern THIS void *eng_ctor(void *el) MANGLED("??0EngineList@@QAE@XZ");
extern THIS int32_t eng_getFirstLanguage(void *el, void *lang)
    MANGLED("?getFirstLanguage@EngineList@@QAEHPAVLangIdentifier@@@Z");
extern THIS int32_t eng_getNextLanguage(void *el, void *lang)
    MANGLED("?getNextLanguage@EngineList@@QAEHPAVLangIdentifier@@@Z");
extern THIS int32_t eng_setData(void *el, const void *lang, void *data)
    MANGLED("?setData@EngineList@@QAEHQBVLangIdentifier@@PAVEngineListData@@@Z");
extern THIS void *eng_getData(void *el, const void *lang)
    MANGLED("?getData@EngineList@@QAEPAVEngineListData@@QBVLangIdentifier@@@Z");
extern THIS void *sti_langCtor(void *l) MANGLED("??0LangIdentifier@@QAE@XZ");

typedef THIS void *(*DeleteFn)(void *self, int32_t freeIt);
#define DELETE_ITSELF(p) ((*(DeleteFn *)(*(void ***)(p)))((p), 1))

THIS int32_t ph_ensureArraySize(PhonemeData *p);

/* Sorted on the packed name, which is the first word of a record. */
int32_t ph_compare(const void *a, const void *b)
{
    if (*(const int32_t *)a > *(const int32_t *)b)
        return 1;
    if (*(const int32_t *)a < *(const int32_t *)b)
        return -1;
    return 0;
}

/* Twice what there is, plus one, so nought still grows. */
THIS int32_t ph_ensureArraySize(PhonemeData *p)
{
    int16_t  bigger;
    uint8_t *fresh;

    if (p->count < p->room)
        return 1;

    bigger = (int16_t)(p->room * 2 + 1);
    fresh = (uint8_t *)cpp_new((uint32_t)(bigger * RECORD_BYTES));
    if (!fresh)
        return 0;

    memset(fresh, 0, (size_t)(bigger * RECORD_BYTES));
    if (p->records) {
        memcpy(fresh, p->records, (size_t)(p->room * RECORD_BYTES));
        cpp_delete(p->records);
    }

    p->records = fresh;
    p->room = bigger;
    return 1;
}

/* Halve the array. Answers where the record is, not which one it is. */
THIS int32_t ph_search(PhonemeData *p, int32_t want, int16_t lo, int16_t hi)
{
    int16_t mid = (int16_t)((lo + hi) / 2);

    if (!p->records || hi < lo)
        return 0;

    if (REC_NAME(REC_AT(p, mid)) == want)
        return EVV_REF(REC_AT(p, mid));

    if (REC_NAME(REC_AT(p, mid)) > want)
        return ph_search(p, want, lo, (int16_t)(mid - 1));

    return ph_search(p, want, (int16_t)(mid + 1), hi);
}

THIS int32_t ph_findPhoneme(PhonemeData *p, int32_t want)
{
    if (p->count == 0)
        return 0;
    return ph_search(p, want, 0, p->count);
}

/* Read one language's phonemes, in the order the settings list them, and
   sort them at the end so the search can halve. */
THIS PhonemeData *ph_dataCtor(PhonemeData *p, const void *lang)
{
    IniFileReader ini;
    char          key[0x14];
    char         *line;

    p->vt = &vtbl_enginelistdata;
    p->vt = &vtbl_phonemedata;
    p->records = 0;
    p->room = 0;
    p->count = 0;

    ini_ctor(&ini);

    sprintf(key, PHONEME_KEY, (unsigned)p->count);
    while ((line = ini_getString(&ini, LANG_TEXT(lang), key)) != 0) {
        int32_t  name[NAME_BYTES];
        uint8_t *rec;
        int32_t  i;

        if (!ph_ensureArraySize(p)) {
            ini_dtor(&ini);
            return p;
        }

        rec = REC_AT(p, p->count);
        REC_LANG(rec) = LANG_PACKED(lang);

        sscanf(line, PHONEME_LINE,
               &name[0], &name[1], &name[2], &name[3],
               (short *)(rec + 0x04), (short *)(rec + 0x06),
               (short *)(rec + 0x08),
               (int32_t *)(rec + 0x10), (int32_t *)(rec + 0x14),
               (int32_t *)(rec + 0x18), (int32_t *)(rec + 0x1c),
               (int32_t *)(rec + 0x20), (int32_t *)(rec + 0x24),
               (int32_t *)(rec + 0x28), (int32_t *)(rec + 0x2c));

        /* The name goes in as four single bytes, whatever was read. */
        for (i = 0; i < NAME_BYTES; i++)
            rec[i] = (uint8_t)name[i];

        p->count++;
        sprintf(key, PHONEME_KEY, (unsigned)p->count);
        cpp_delete(line);
    }

    if (p->records)
        qsort(p->records, (size_t)p->count, RECORD_BYTES, ph_compare);

    ini_dtor(&ini);
    return p;
}

THIS void ph_dataDtor(PhonemeData *p)
{
    p->vt = &vtbl_phonemedata;

    if (p->records) {
        cpp_delete(p->records);
        p->records = 0;
    }

    p->vt = &vtbl_enginelistdata;
}

THIS void *ph_dataDestroy(PhonemeData *p, int32_t freeIt)
{
    ph_dataDtor(p);
    if (freeIt & 1)
        cpp_delete(p);
    return p;
}

/* The base's own deleting destructor, which has nothing of its own to undo. */
THIS void *ph_listDataDestroy(void *p, int32_t freeIt)
{
    *(const void **)p = &vtbl_enginelistdata;
    if (freeIt & 1)
        cpp_delete(p);
    return p;
}

/* One table per language the settings declare. A language whose table
   cannot be built, or cannot be stored, is dropped rather than kept
   half-made. */
/* A Phonemes is an EngineList with a table hung off each language, so
   what has to be allocated for one is what an EngineList is. */
const uint32_t ph_bytes = sizeof(EngineList);

THIS void *ph_ctor(void *self)
{
    LangIdentifier lang;

    eng_ctor(self);
    sti_langCtor(&lang);

    if (!eng_getFirstLanguage(self, &lang))
        return self;

    for (;;) {
        void *room = cpp_new(sizeof(PhonemeData));
        void *one = room ? ph_dataCtor((PhonemeData *)room, &lang) : 0;

        if (!one)
            return self;

        if (!eng_setData(self, &lang, one)) {
            DELETE_ITSELF(one);
            return self;
        }

        if (!eng_getNextLanguage(self, &lang))
            return self;
    }
}

THIS int32_t ph_getPhoneme(void *self, void *lang, int32_t want)
{
    PhonemeData *p = (PhonemeData *)eng_getData(self, lang);

    if (!p)
        return 0;
    return ph_findPhoneme(p, want);
}

const void *vtbl_phonemedata[1] = { (void *)ph_dataDestroy };
const void *vtbl_enginelistdata[1] = { (void *)ph_listDataDestroy };

ALIAS("??_7PhonemeData@@6B@", "vtbl_phonemedata");
ALIAS("??_7EngineListData@@6B@", "vtbl_enginelistdata");
ALIAS("??_EPhonemeData@@UAEPAXI@Z", "ph_dataDestroy");
ALIAS("??_EEngineListData@@UAEPAXI@Z", "ph_listDataDestroy");
ALIAS("??0Phonemes@@QAE@XZ", "ph_ctor");
ALIAS("?getPhoneme@Phonemes@@QAEJPAVLangIdentifier@@J@Z", "ph_getPhoneme");
ALIAS("??0PhonemeData@@QAE@QBVLangIdentifier@@@Z", "ph_dataCtor");
ALIAS("??1PhonemeData@@UAE@XZ", "ph_dataDtor");
ALIAS("??_GPhonemeData@@UAEPAXI@Z", "ph_dataDestroy");
ALIAS("??_GEngineListData@@UAEPAXI@Z", "ph_listDataDestroy");
ALIAS("?ensureArraySize@PhonemeData@@AAEHXZ", "ph_ensureArraySize");
ALIAS("?findPhoneme@PhonemeData@@QAEJJ@Z", "ph_findPhoneme");
ALIAS("?search@PhonemeData@@AAEJJFF@Z", "ph_search");
ALIAS("?compare@PhonemeData@@CAHPBX0@Z", "ph_compare");
