/* The Japanese romanizer, which is what jpnrom.dll is in stock Eloquence.
 *
 * A language written in another script cannot be handed to the engine as it
 * stands: the engine reads a phoneme notation, and Japanese arrives as kana
 * and kanji. The romanizer is what stands between -- it analyses the text,
 * looks its words up, works out their readings and their accents, and hands
 * back a string in the notation the engine already speaks. Everything below
 * that seam is the same engine that speaks the other eight languages, and
 * test/romcan.c is what proved it before any of this existed.
 *
 * How it is reached. src/eci_romanizer.c holds one romanizer per language
 * family and calls it through the table in src/eci_rom.h; this is the one that
 * answers for family 8. The manager finds it the way IBM's does, by asking
 * what is linked in rather than by loading anything.
 *
 * Names here are the names in IBM's own objects, and each file is named for
 * the object it came from, so the two can be read against each other. The
 * classes become structs with a prefix for the methods: RomInstParam is rp_,
 * Romanizer is jr_, and so on.
 */

#ifndef JPROM_H
#define JPROM_H

#include <stdint.h>
#include "eci_rom.h"

/* Operator new and delete, which is what IBM's romanizer allocates with, so
   ours does too: on a sixty-four bit host these come out of the low arena,
   and a string handed back to the engine has to be somewhere the engine can
   name. */
extern void *cpp_new(uint32_t n);
extern void  cpp_delete(void *p);

/* ---- how IBM's classes fit together --------------------------------- */

/* Read once and worth having written down, because nothing else says it.
 *
 * A RomInstance is 0x18 bytes and holds two things it makes itself: a
 * RomInstParam at +0x08, made with the directory the program was loaded from,
 * and a Romanizer at +0x10, made with that RomInstParam. Every one of its
 * thirty-one methods forwards to one or the other -- the parameter calls to
 * the first and everything about text to the second.
 *
 * Romanizer derives from ConverterInterface, so one object answers both, and
 * the ConverterInterface half is what the parameter block points back at.
 * `ConverterInterface::initBase' is where that happens: it keeps the block at
 * its own +0x08 and writes itself into the block's +0x00, then makes the
 * InputManager that holds text on its way in.
 *
 * The seven slots of that shared vtable, since a caller reaches four of them
 * by number:
 *
 *     0x00  the destructor
 *     0x04  processSentence
 *     0x08  getOffset
 *     0x0c  ResetBuffer
 *     0x10  isValidUserDictEntry
 *     0x14  mbcs2Rom
 *     0x18  rom2Mbcs
 *
 * Romanizer overrides the first four and inherits the rest; in
 * ConverterInterface itself the middle three are pure. Nothing of ours needs a
 * vtable at those numbers -- the manager reaches a romanizer through the named
 * table in src/eci_rom.h -- but a transcription that calls slot two has to
 * know it means getOffset. */
typedef struct Converter Converter;

/* ---- the tables ------------------------------------------------------ */

/* Lifted out of IBM's objects by tools/lift-romtables.py, which writes
   lang/jajp/rom_tables_jajp.c. Each object's tables are one block there with
   a pointer per table, because that is how the original had them and its own
   code does not always stay inside the table it started in. The ones read as
   sixteen-bit values are cast where they are used; the block is aligned so
   that the cast is sound. */
extern const uint8_t *const jajp_m_pAITable;
extern const uint8_t *const jajp_m_pRTable;
extern const uint8_t *const jajp_m_pKanaTable;
extern const uint8_t *const jajp_m_pLeadByteTable1;
extern const uint8_t *const jajp_m_pLeadByteTable2;

/* dictman.obj's tables, and how long each is. The lengths are what the
   lifter measured, which is the distance to the next table, so an index
   within them is one the original could have answered for. */
extern const uint8_t *const jajp_s_aFuncWordDict;
extern const int32_t jajp_s_aFuncWordDict_n;
extern const uint8_t *const jajp_s_aFuncWordDictEx;
extern const int32_t jajp_s_aFuncWordDictEx_n;
extern const uint8_t *const jajp_s_aKakariTable;
extern const int32_t jajp_s_aKakariTable_n;
extern const uint8_t *const jajp_s_aPenaltyTable;
extern const int32_t jajp_s_aPenaltyTable_n;
extern const uint8_t *const jajp_s_aPhrVectorTable;
extern const int32_t jajp_s_aPhrVectorTable_n;
extern const uint8_t *const jajp_s_aAccentTable;
extern const int32_t jajp_s_aAccentTable_n;
extern const uint8_t *const jajp_s_aNumMDTable;
extern const int32_t jajp_s_aNumMDTable_n;
extern const uint8_t *const jajp_s_aNumYomiTable;
extern const int32_t jajp_s_aNumYomiTable_n;
extern const uint8_t *const jajp_s_aNumJMDTable;
extern const int32_t jajp_s_aNumJMDTable_n;
extern const uint8_t *const jajp_s_aNumJCCTable;
extern const int32_t jajp_s_aNumJCCTable_n;
extern const uint8_t *const jajp_s_aHash4NDict;
extern const int32_t jajp_s_aHash4NDict_n;
extern const uint8_t *const jajp_s_aHash4TDict;
extern const int32_t jajp_s_aHash4TDict_n;
extern const uint8_t *const jajp_s_aHash4KDict;
extern const int32_t jajp_s_aHash4KDict_n;
extern const uint8_t *const jajp_s_aHash4KNDict;
extern const int32_t jajp_s_aHash4KNDict_n;
extern const uint8_t *const jajp_s_aHash4KTDict;
extern const int32_t jajp_s_aHash4KTDict_n;
extern const uint8_t *const jajp_s_aHash4EDict;
extern const int32_t jajp_s_aHash4EDict_n;
extern const uint8_t *const jajp_s_aItaijiHashTable;
extern const int32_t jajp_s_aItaijiHashTable_n;
extern const uint8_t *const jajp_s_aItaijiTable;
extern const int32_t jajp_s_aItaijiTable_n;
extern const uint8_t *const jajp_s_aTGTable;
extern const int32_t jajp_s_aTGTable_n;
extern const uint8_t *const jajp_s_aYomiDataTable;
extern const int32_t jajp_s_aYomiDataTable_n;
extern const uint8_t *const jajp_s_aPhraseDataTable;
extern const int32_t jajp_s_aPhraseDataTable_n;
extern const uint8_t *const jajp_s_aNumberDataTable;
extern const int32_t jajp_s_aNumberDataTable_n;
extern const uint8_t *const jajp_s_szFromStringOfRoman2Kana;
extern const int32_t jajp_s_szFromStringOfRoman2Kana_n;
extern const uint8_t *const jajp_s_szToStringOfRoman2Kana;
extern const int32_t jajp_s_szToStringOfRoman2Kana_n;
extern const uint8_t *const jajp_s_szRemainStringOfRoman2Kana;
extern const int32_t jajp_s_szRemainStringOfRoman2Kana_n;
extern const uint8_t *const jajp_s_anFromPositionOfRoman2Kana;
extern const int32_t jajp_s_anFromPositionOfRoman2Kana_n;
extern const uint8_t *const jajp_s_anToPositionOfRoman2Kana;
extern const int32_t jajp_s_anToPositionOfRoman2Kana_n;
extern const uint8_t *const jajp_s_anRemainPositionOfRoman2Kana;
extern const int32_t jajp_s_anRemainPositionOfRoman2Kana_n;
extern const uint8_t *const jajp_s_anAccentValueOfRoman2Kana;
extern const int32_t jajp_s_anAccentValueOfRoman2Kana_n;
extern const uint8_t *const jajp_s_anAccentPositionOfRoman2Kana;
extern const int32_t jajp_s_anAccentPositionOfRoman2Kana_n;
extern const uint8_t *const jajp_s_szFromStringOfEng2Roman;
extern const int32_t jajp_s_szFromStringOfEng2Roman_n;
extern const uint8_t *const jajp_s_szToStringOfEng2Roman;
extern const int32_t jajp_s_szToStringOfEng2Roman_n;
extern const uint8_t *const jajp_s_szRemainStringOfEng2Roman;
extern const int32_t jajp_s_szRemainStringOfEng2Roman_n;
extern const uint8_t *const jajp_s_anFromPositionOfEng2Roman;
extern const int32_t jajp_s_anFromPositionOfEng2Roman_n;
extern const uint8_t *const jajp_s_anToPositionOfEng2Roman;
extern const int32_t jajp_s_anToPositionOfEng2Roman_n;
extern const uint8_t *const jajp_s_anRemainPositionOfEng2Roman;
extern const int32_t jajp_s_anRemainPositionOfEng2Roman_n;
extern const uint8_t *const jajp_s_anAccentValueOfEng2Roman;
extern const int32_t jajp_s_anAccentValueOfEng2Roman_n;
extern const uint8_t *const jajp_s_anAccentPositionOfEng2Roman;
extern const int32_t jajp_s_anAccentPositionOfEng2Roman_n;

/* The static dictionary's two supplement blobs, which DictMan reaches for.
   tools/lift-rom.py writes those. */
extern const uint8_t *const jajp_s_apszSuppD[];
extern const uint8_t *const jajp_s_apszSuppI[];

/* ---- RomInstParam ---------------------------------------------------- */

/* What an instance was told to be. Every parameter the engine sets on a
   romanizer lands here, and the errors it reports are collected here too.
   Kept as named fields rather than at IBM's offsets: nothing outside this
   directory reads it, so there is nothing for a layout to agree with. */
typedef struct RomInstParam {
    /* The converter this parameter block belongs to, which is how anything
       holding the block reaches the romanizer itself. Nothing in the block's
       own object writes it -- its constructor only zeroes it -- and
       ConverterInterface::initBase fills it in with itself. InputManager's
       insertIndex is what reads it: it asks the converter where in the output
       the mark belongs. */
    struct Converter *owner;    /* +0x00 */
    char    *path;          /* +0x04, the directory it was made with */
    int32_t  codeset;       /* +0x08, language and codeset together */
    int32_t  wantWordIndex; /* +0x0c */
    int32_t  inputType;     /* +0x10 */
    int32_t  dictOn;        /* +0x14 */
    int32_t  textMode;      /* +0x18 */
    int32_t  numberMode;    /* +0x1c */
    int32_t  retroflex;     /* +0x20 */
    int32_t  lastError;     /* +0x24 */
    uint32_t errors;        /* +0x28, every error since they were cleared */
    int32_t  concatenative; /* +0x2c, parameter 0x3e8 */
    int32_t  wordMarks;     /* +0x30, parameter 0x3e9 */
    int32_t  voice;         /* +0x34, parameter 0x3ea */
    int32_t  rate;          /* +0x38, parameter 0x3eb */
} RomInstParam;

/* Which error is which. These are the bits getErrors collects. */
#define ROM_ERR_NONE         0x00
#define ROM_ERR_MEMORY       0x01
#define ROM_ERR_NO_DICT      0x02
#define ROM_ERR_DICT_READ    0x04
#define ROM_ERR_DICT_BAD     0x08
#define ROM_ERR_UNICODE      0x20
#define ROM_ERR_UNKNOWN_CHAR 0x40

RomInstParam *rp_ctor(RomInstParam *p, const char *path);
void          rp_dtor(RomInstParam *p);
int32_t       rp_getParam(RomInstParam *p, int32_t which);
int32_t       rp_setParam(RomInstParam *p, int32_t which, int32_t value);
void          rp_setError(RomInstParam *p, int32_t error);
void          rp_clearErrors(RomInstParam *p);
void          rp_clearOneError(RomInstParam *p, int32_t error);
uint32_t      rp_getErrors(RomInstParam *p);
int32_t       rp_getLastError(RomInstParam *p);
void          rp_getErrorMessage(RomInstParam *p, char *out);
int32_t       rp_getCodeSet(RomInstParam *p);
int32_t       rp_isDictOn(RomInstParam *p);
int32_t       rp_isSetWantWordIndex(RomInstParam *p);
int32_t       rp_isAnnotationsInText(RomInstParam *p);

/* The annotation an index mark becomes, which is a string of this object's
   own in the original. */
extern const char USERINDEXSTR[];

/* ---- UnicodeConverter ------------------------------------------------ */

/* Shift-JIS to UCS-2 and back, over the tables above. It keeps one buffer of
   each kind and grows it when a longer string arrives, which is why the
   answers are pointers into the converter rather than the caller's. */
typedef struct UnicodeConverter {
    char         *mbcs;      /* +0x00 */
    uint32_t      mbcsRoom;  /* +0x04, bytes */
    uint16_t     *ucs;       /* +0x08 */
    uint32_t      ucsRoom;   /* +0x0c, characters, not bytes */
    RomInstParam *param;     /* +0x10 */
} UnicodeConverter;

UnicodeConverter *uc_ctor(UnicodeConverter *c, RomInstParam *param);
void              uc_dtor(UnicodeConverter *c);
int32_t           uc_initTable(UnicodeConverter *c, const char *path,
                               int32_t lang);
int32_t           uc_MBCSToUCS2(UnicodeConverter *c, const char *in,
                                uint16_t **out);
int32_t           uc_UCS2ToMBCS(UnicodeConverter *c, const uint16_t *in,
                                char **out, int32_t yenFlag);
uint32_t          ucs2len(const uint16_t *s);

/* ---- DictMan --------------------------------------------------------- */

/* One substitution rule as DictMan lays it out: eight tables and a count.
   The positions and the accent tables are read as sixteen-bit values; what a
   row of each means is the business of whichever class reads them. */
typedef struct DictManRules {
    const uint8_t *from;         /* +0x00 */
    const uint8_t *to;           /* +0x04 */
    const uint8_t *remain;       /* +0x08 */
    const uint8_t *fromPos;      /* +0x0c */
    const uint8_t *toPos;        /* +0x10 */
    const uint8_t *remainPos;    /* +0x14 */
    const uint8_t *accentValue;  /* +0x18 */
    const uint8_t *accentPos;    /* +0x1c */
    uint16_t       count;        /* +0x20 */
} DictManRules;

extern DictManRules dm_EngToRomanRule;
extern DictManRules dm_RomanToKanaRule;
extern const uint8_t *dm_paUserDict;
extern const uint8_t *dm_paUserDictIdx;

void dm_EngRulesInit(void);
void dm_InitSupplementDictionary(void);

const uint8_t *dm_GetFuncDict(void);
const uint8_t *dm_GetFuncDictEx(void);
const uint8_t *dm_GetAccentAt(uint16_t i);
uint8_t        dm_GetKakariAt(uint16_t i);
uint8_t        dm_GetPhrVectorAt(uint16_t i);
uint8_t        dm_GetPenaltyAt(uint16_t i);
uint8_t        dm_GetTGAt2(uint8_t row, uint8_t col);
const uint8_t *dm_GetTGAt(uint8_t row);
const uint8_t *dm_GetYomiDataPtr(void);
const uint8_t *dm_GetPhraseDataPtr(void);
const uint8_t *dm_GetNumberDataPtr(void);
uint8_t        dm_GetNumMDAt(uint16_t i);
const uint8_t *dm_GetNumYomiPtrAt(uint8_t i);
uint8_t        dm_GetNumJMDAt(uint16_t i);
const uint8_t *dm_GetNumJMDPtr(void);
uint8_t        dm_GetNumJCCAt(uint16_t i);
const uint8_t *dm_GetNDictHashAt(uint16_t i, uint8_t j);
const uint8_t *dm_GetTDictHashAt(uint16_t i);
const uint8_t *dm_GetKDictHashAt(uint16_t i);
uint8_t        dm_GetKNDictHashAt(uint16_t i, uint8_t j);
uint8_t        dm_GetKTDictHashAt(uint16_t i, uint8_t j);
const uint8_t *dm_GetEDictHashAt(uint16_t i);
uint8_t        dm_GetItaijiHashAt(uint16_t i, uint8_t j);
const uint8_t *dm_GetItaijiAt(uint8_t row, uint16_t i);

/* ---- how much of this is written ------------------------------------ */

/* While this is defined the romanizer cannot convert anything yet, and says
   so by refusing to be made at all: src/eci_romanizer.c then behaves exactly
   as it did before there was a romanizer to find, and the instance is refused
   rather than speaking something wrong. Take it out when Romanizer is
   finished, and not before -- a half-written romanizer that quietly answers
   nothing is the one failure this whole exercise is arranged to prevent.
   test/romcan.sh is unaffected either way: it registers its own romanizer
   over whatever is linked. */
#define JPROM_INCOMPLETE 1

#endif
