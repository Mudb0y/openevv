/* The Japanese romanizer, which is what jpnrom.dll is in stock Eloquence.
 *
 * A language written in another script cannot be handed to the engine as it
 * stands: the engine reads a phoneme notation, and Japanese arrives as kana
 * and kanji. The romanizer is what stands between -- it analyses the text,
 * looks its words up, works out their readings and their accents, and hands
 * back a string in the notation the engine already speaks. Everything below
 * that seam is the same engine that speaks the other eight languages, and
 * test/harness/romcan.c is what proved it before any of this existed.
 *
 * How it is reached. src/eci/lang/eci_romanizer.c holds one romanizer per language
 * family and calls it through the table in src/eci/lang/eci_rom.h; this is the one that
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
#include "evv_abi.h"
#include "eci_objects.h"
#include "romanizer.h"

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
 * table in src/eci/lang/eci_rom.h -- but a transcription that calls slot two has to
 * know it means getOffset. */
typedef struct Converter Converter;

/* Those seven slots as a table. A converter block is not a struct here -- it
   is a run of bytes at IBM's offsets, because Romanizer's record is shared
   with classes that read it -- so the vtable pointer is parked past the
   record with the rest of its pointers and CI_VT is how it is reached.
   All seven are read now and Romanizer fills the table in. */
typedef struct ConverterVtbl {
    void   *(*destroy)(Converter *c, int32_t freeIt);
    int32_t (*processSentence)(Converter *c, char **out, int32_t more);
    int32_t (*getOffset)(Converter *c);
    void    (*ResetBuffer)(Converter *c);
    int32_t (*isValidUserDictEntry)(Converter *c, const char *s, int32_t a,
                                    int32_t b);
    int32_t (*mbcs2Rom)(Converter *c, const char *s, char **out);
    int32_t (*rom2Mbcs)(Converter *c, const char *s, char **out);
} ConverterVtbl;

extern const ConverterVtbl JRZ_VTBL;

#define CI_VT(c) (*(const ConverterVtbl **)((uint8_t *)(c) + RZ_VTABLE_AT))

/* ---- the tables ------------------------------------------------------ */

/* Lifted out of IBM's objects by tools/rom/tables.py, which writes both
   lang/jajp/rom_tables_jajp.c and the header declaring what is in it. Each
   object's tables are one block there with a pointer per table, because that
   is how the original had them and its own code does not always stay inside
   the table it started in. The ones read as sixteen-bit values are cast where
   they are used; the block is aligned so that the cast is sound. */
#include "rom_tables_jajp.h"

/* The static dictionary itself, which tools/rom/dictionary.py writes: the words, the
   single kanji, the two tries that index them by reading, and the two
   supplement blobs DictMan reaches for. Each array's own count comes with it,
   because a lookup that has walked off the end of the hash has to be able to
   tell. */
extern const uint8_t *const jajp_s_apszNormal[];
extern const int32_t        jajp_s_apszNormal_n;
extern const uint8_t *const jajp_s_apszTankan[];
extern const int32_t        jajp_s_apszTankan_n;
extern const uint8_t *const jajp_s_apszEng[];
extern const int32_t        jajp_s_apszEng_n;
extern const uint8_t *const jajp_s_apszKana[];
extern const int32_t        jajp_s_apszKana_n;
extern const uint8_t *const jajp_s_apszTankanKana[];
extern const int32_t        jajp_s_apszTankanKana_n;
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
int32_t       rp_setInputType(RomInstParam *p, int32_t type);

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

/* ---- JpnUtil --------------------------------------------------------- */

/* One entry of the chains the phrase tables keep their entries on: two
   sixteen-bit indices, with one value standing for the end. */
typedef struct LinkTable {
    uint16_t prev;
    uint16_t next;
} LinkTable;

/* The file calls, which are the C library's under IBM's own names. */
#include <stdio.h>

FILE     *ju_ttsOpen(char *name, const char *mode);
void      ju_ttsClose(FILE *f);
long      ju_ttsLseek(FILE *f, long to, long whence);
long      ju_ttsRead(FILE *f, char *buf, uint32_t n);
long      ju_ttsReadAll(FILE *f, char **out, long from, uint32_t most);
long      ju_ttsWrite(FILE *f, const char *buf, uint32_t n);

uint16_t  ju_MakeUshort(const char *p);
int32_t   ju_DbCmp(const char *a, const char *b);
int32_t   ju_DbCmp2(const char *a, char b0, char b1);
void      ju_DbCpy(char *to, const char *from);
void      ju_DbSet(char *to, char b0, char b1);
int32_t   ju_TwoChCmp(char b0, char b1, char *p);
void      ju_TwoChCpy(char *from, char *to0, char *to1);

int32_t   ju_IsSBCSKana(char c);
int32_t   ju_IsAlphaNumSym(char c);
int32_t   ju_IsNum(char c);
int32_t   ju_IsAlpha(char c);
int32_t   ju_IsDBCSLeadByte(char c);
int32_t   ju_IsDBCSTrailByte(uint8_t c);
int32_t   ju_IsValidDBCS(const char *p);
int32_t   ju_IsKatakana(const char *p);
int32_t   ju_IsHiragana(const char *p);
int32_t   ju_IsLongVowel(const char *p);
int32_t   ju_IsSNLKDelim(const char *p);
int32_t   ju_IsDBCSNum(const char *p);
int32_t   ju_IsKanjiNum(const char *p);

void      ju_GetRomaji(uint8_t *out, const uint8_t *table, int16_t col);
int32_t   ju_WriteRomajiStrBuf(uint8_t code, uint8_t *out);
void      ju_ConvertDakuten(char *out, uint8_t kana, uint8_t mark);
void      ju_Hiragana2Katakana(const uint8_t *in, uint8_t *out);
int32_t   ju_YomiCmp(uint8_t *a, uint8_t lenA, uint8_t *b, uint8_t lenB);
void      ju_TableFree(uint16_t *used, uint16_t *tail, uint16_t *freeHead,
                       void *table, uint16_t nil, uint16_t which);

/* ---- DictSearch ------------------------------------------------------ */

/* The class the rest of the analyser leans on. Forty-six of its sixty-two
   methods are written; rom/jajp/dictsearch.h maps the record and says which
   parts of it are understood, and rom/jajp/dictsearch.c says why the layout
   here is IBM's rather than ours. The block is passed as bytes because the
   fields it holds are still being worked out. */
void   *dsr_ctor(void *d, void *analysis);
void   *dsr_destroy(void *d, int32_t freeIt);
int32_t ds_IsOnin(uint8_t code);
int16_t ds_GetYoonIndex(void *d, char *s);
void    ds_SetLongWord(void *d, int16_t n, void *e, uint8_t *word);
int16_t ds_IsMember(void *d, uint8_t *p, const uint8_t *table, int16_t n);
int16_t ds_IsZKNum(void *d, uint8_t *p);
int16_t ds_IsZSNum(void *d, uint8_t *p);
int16_t ds_IsZKeta(void *d, uint8_t *p);
int16_t ds_IsZSymb(void *d, uint8_t *p);
int32_t ds_IsCommaPosition(void *d, char *p, int32_t n);
int32_t ds_IsEndOfQuote(void *d, int16_t at);
int16_t ds_CheckKetaOrder(void *d, int16_t *n, int16_t *chars,
                          int16_t *keepN, int16_t *keepChars,
                          int16_t keta, uint8_t *buf);
int16_t ds_SetSuushiWord(void *d, int16_t slot, int16_t at);
int16_t ds_SetDummyWord(void *d, int16_t slot, int16_t at);
struct RomUserDict *ds_getPtrOfUserDict(void *d);
int16_t ds_Do(void *d);
int16_t ds_HitFuncWordReverse(void *d, const uint8_t *head, int16_t slot,
                              uint16_t at, int16_t count, uint8_t chars,
                              uint8_t hiragana, uint8_t *vec,
                              const uint8_t *base);
int16_t ds_FzkSearchUnknown(void *d, uint8_t *vec, uint16_t at, int16_t slot,
                            const uint8_t *dict, int16_t unused);
int16_t ds_FzkParsing(void *d, uint8_t *vec, int16_t at);
int16_t ds_FzkParsingReverse(void *d);

/* ---- Romanizer ------------------------------------------------------- */

/* Only the one method, which DictSearch::Do calls when an annotation stands
   in front of the character it is about to look up. The record it works on is
   rom/jajp/romanizer.h. */
int32_t jrz_GetParameter(void *rz, char *p);

/* Two statics of DictMan naming a user dictionary loaded from a file. Nothing
   in this port sets them, so they stay null and the walk that reads them is
   never taken; they are here because Do tests for them. */
extern const uint8_t *dm_s_paUserDict;
extern const uint8_t *dm_s_paUserDictIdx;
int32_t ds_CountHrgn(void *d, int32_t n);
const uint8_t *ds_ReadGWDict(void *d, int16_t page, int16_t at,
                             int16_t which);
int32_t ds_CheckCaseMarker(void *d, int16_t at);
void    ds_CheckCnvChoon(void *d, uint8_t code, uint8_t *next);
int32_t ds_GetTextBuf(void *d, int16_t from);
int16_t ds_ConvertYoonDict(void *d, int16_t base, int16_t yoon, uint8_t flag);
void    ds_ProcessHiragana(void *d, int16_t at, void *e);
void    ds_ProcessKatakana(void *d, int16_t at, void *e);
int16_t ds_WriteKanaData(void *d, const uint8_t *head, int16_t chars,
                         int16_t unused, int16_t base);
int16_t ds_LookupKanaDict(void *d, int16_t at);
int16_t ds_GenerateKanaString(void *d);
int16_t ds_CompareKanji(void *d, const uint8_t *ent, int16_t which);
int16_t ds_WriteGWDict(void *d, const uint8_t *word, int16_t which,
                       int16_t base, int16_t at, int16_t mode);
int16_t ds_WriteDictTableData(void *d, const uint8_t *head, int16_t which,
                              int16_t mode, int16_t at, int16_t base);
int16_t ds_GetDictEntry(void *d, int16_t which, int16_t at, int16_t base,
                        const uint8_t *head, int16_t mode);
int16_t ds_SearchTankanTable(void *d, int16_t which, int16_t at,
                             int16_t base);
int16_t ds_GenerateWord(void *d, int16_t at, int16_t base);
int32_t ds_IsItaiji(void *d, uint16_t code);
uint16_t ds_SwapKanji(void *d, uint16_t code);
int16_t ds_ErrorDummy(void *d, int16_t slot, int16_t at);
int16_t ds_WriteData(void *d, const uint8_t *head, int16_t chars,
                     int16_t hiragana, int16_t base, int16_t last,
                     int16_t at);
int16_t ds_WriteTankanData(void *d, const uint8_t *head, int16_t chars,
                           int16_t base, int16_t at);
int16_t ds_WriteUserData(void *d, const uint8_t *head, int16_t slot,
                         int16_t at);
int16_t ds_LookupUserDict(void *d, const uint8_t *dict, char *text,
                          int16_t slot, const uint8_t *index, int16_t at,
                          int16_t unused);
int16_t ds_LookupEngWordDict(void *d, uint8_t *roman, int16_t slot,
                             int16_t at, int16_t want, int32_t mark);
int32_t ds_LookupEngWordDictFromText(void *d, int16_t slot, int16_t at);
int16_t ds_LookupTankanDict(void *d, int16_t base, int16_t at);
int16_t ds_LookupNormalWordDict(void *d, int16_t base, int16_t at,
                                int32_t swap);
int16_t ds_HitFuncWordDict(void *d, const uint8_t *head, int16_t slot,
                           int16_t at, int16_t count, int16_t run,
                           int16_t hiragana, const uint8_t *vec,
                           const uint8_t *dict, int16_t flag);
int16_t ds_SearchFuncWordDict(void *d, const uint8_t *vec, int16_t at,
                              int16_t slot, const uint8_t *dict,
                              int16_t flag);
int16_t ds_LookupFuncWordDict(void *d, int16_t base, int16_t at);
int16_t ds_EngRulesUppercase(void *d, const uint8_t *in, uint8_t *out);
int16_t ds_EngRulesNormalize(void *d, const uint8_t *in, uint8_t *out);
int16_t ds_EngRulesApplyRule(void *d, const uint8_t *in, uint8_t *out,
                             DictManRules *r, int16_t *accent);
int16_t ds_EngRulesConvert(void *d, const uint8_t *in, uint8_t *out,
                           DictManRules *eng, DictManRules *kana,
                           int16_t *outLen, int16_t *count);
void    ds_SetDummySymbol(void *d, int16_t at, void *e);
void    ds_SetDummyRomanAlphabet(void *d, int16_t at, void *e);
void    ds_ProcessRomanAlphabet(void *d, int16_t at, void *e);
int32_t ds_NeedKatakanaAnalysis(void *d, int16_t base, int16_t n);
int16_t ds_CheckJrtTable(void *d, int16_t base, int16_t n);
int16_t ds_CompareJMD(void *d, uint8_t *p, int16_t at, int16_t n);
void    ds_SetJCC(void *d, const uint8_t *m, int16_t slot);
int16_t ds_JoSuusiSearch(void *d, int16_t at);
int16_t ds_HandleError(void *d, int16_t at, int16_t written, int16_t base,
                       char *out);

/* TextAnalysis's, and here because it is in IBM's dictsearch object. */
void    ta_AddLongWord(void *t, uint8_t *word, int16_t n);

/* ---- InputChar ------------------------------------------------------- */

/* The record is IBM's, so these all take a block of IC_ROOM bytes rather than
   a struct; rom/jajp/inputchar.h is what says where anything in it sits, and
   why it is that way round. ic_GetSnlkTableAt answers a node of the same
   kind, which is why it is a void pointer as well. */
void   *ic_ctor(void *in, void *analysis);
void    ic_Init(void *in);
void    ic_SetText(void *in, const char *text);
void    ic_SetTextAt(void *in, const char *text, uint32_t at);
uint8_t ic_GetNextChar(void *in);
int32_t ic_IsAnnotationsInText(void *in);
int32_t ic_AddSnlkTable(void *in, int16_t at, const char *written,
                        const char *reading, int32_t flag);
void   *ic_GetSnlkTableAt(void *in, int16_t at);
void    ic_DeleteSnlkTable(void *in);
int16_t ic_GetUnknownKanji(void *in, int16_t at, int32_t from, int32_t to);
int32_t ic_IsKanjiNum(void *in, int32_t at);
int32_t ic_GetCharType(void *in, int16_t at);
int32_t ic_CheckNextAnnotation(void *in);
int16_t ic_CheckCyuTen(void *in, int16_t *at);
int16_t ic_CheckContextForNum(void *in, int16_t *at);
int16_t ic_CheckContext(void *in, int16_t *at, int32_t peek);
void    ic_ConvertDakuten(void *in, int16_t at, uint8_t c, uint8_t mark);
int16_t ic_ProcessASCII(void *in, int16_t at, uint8_t *c0, uint8_t *c1);
int8_t  ic_ProcessAnnotation(void *in, int16_t at);
void    ic_RecoverOverflow(void *in, int16_t at);
int16_t ic_ReadSentence(void *in);

/* ---- Annotation ------------------------------------------------------ */

/* The marks a caller put in the text, lifted out of it and kept until the
   output side passes the place they belonged to. A ring of 128, which is
   IBM's number and its only bound. Named fields rather than IBM's offsets:
   nothing outside rom/jajp/annotation.c reads them. */
#define ANNO_N          128

#include "eci_io.h"

typedef struct Annotation {
    void    *analysis;        /* TextAnalysis * */
    int16_t  at[ANNO_N];      /* where in the text each belonged */
    char    *text[ANNO_N];
    int32_t  type[ANNO_N];
    uint8_t  head;            /* the oldest still kept */
    uint8_t  count;
} Annotation;

Annotation *an_ctor(Annotation *a, void *analysis);
int32_t     an_GetRomHandAnnoType(Annotation *a, const char *s);
int32_t     an_Save(Annotation *a, char *text, int16_t len, int16_t at);
const char *an_GetLastAnno(Annotation *a, int16_t before, int32_t type);
void        an_dtor(Annotation *a);
void       *an_destroy(Annotation *a, int32_t freeIt);
void        an_Remove(Annotation *a);
void        an_RemoveAfter(Annotation *a, int16_t after);
int32_t     an_Flush(Annotation *a, int32_t escape, DynaBuf *out,
                     int32_t dropPause);

/* ---- RomUserDict ----------------------------------------------------- */

/* The stored dictionary's own classes, which this shares with the engine's
   English one: the same skip list, keyed the same way. */
#include "eci_key.h"

/* What a caller taught the romanizer, as it is stored. Thirty-one bytes, and
   the layout is IBM's because Translation keeps it as opaque bytes and
   writeData reads it back field by field.
 *
 * The reading may overrun into the two bytes after it -- see
 * rud_transKatakana2Yomi -- so nothing may be put between `kana' and `attr'. */
typedef struct UserDictData {
    uint8_t chars;      /* +0x00, characters of written form */
    uint8_t kanaLen;    /* +0x01, yomi codes in the reading */
    uint8_t kana[25];   /* +0x02, the reading itself */
    uint8_t accent;     /* +0x1b, which mora the caret marked */
    uint8_t attr;       /* +0x1c */
    uint8_t attr2;      /* +0x1d */
    uint8_t pos;        /* +0x1e, the part of speech */
} UserDictData;

/* And the class itself. Named fields rather than IBM's offsets: it is written
   whole, and nothing outside this directory reads it. */
typedef struct RomUserDict {
    SkipList *dict;      /* what is in force, which the engine sets */
    void     *analysis;  /* TextAnalysis * */
    void     *input;     /* InputChar *, its own */
    void     *search;    /* DictSearch *, its own */
} RomUserDict;

/* What the dictionary calls answer. These are the raw numbers IBM's own
   methods return; src/eci/dict/eci_dict.c is what turns them into the older
   interface's names, and it only distinguishes the second. */
#define ECI_DICT_NO_ENTRY       5    /* the search found nothing */
#define ECI_DICT_NO_MEMORY     (-2)  /* an allocation or an insert failed */
#define ECI_DICT_ERROR         (-3)  /* the word is longer than a key may be */
#define ECI_DICT_INVALID_ENTRY (-15) /* nothing that could be stored */

/* Its one table, three bytes for each of the four parts of speech. */
extern const uint8_t *const jajp_s_anUserDictData;

RomUserDict *rud_ctor(RomUserDict *u, void *analysis);
int32_t rud_makeKey(RomUserDict *u, uint8_t *in, int32_t n, char *out,
                   int32_t *outLen);
int32_t rud_makeTransValue(RomUserDict *u, const char *in, uint8_t *accent,
                          char *out, int16_t room);
uint8_t rud_transKatakana2Yomi(RomUserDict *u, char *kana, uint8_t *out);
uint8_t rud_transKana2Yomi(RomUserDict *u, char *kana, uint8_t *out);
int32_t rud_makeUserDictData(RomUserDict *u, UserDictData *d,
                            uint8_t keyLen, char *kana, int32_t pos);
int32_t rud_writeData(RomUserDict *u, UserDictData *d, int16_t slot,
                     int16_t at);
int16_t rud_lookup(RomUserDict *u, uint8_t *text, int16_t at, int16_t slot);
int32_t rud_updateDictExt(RomUserDict *u, SkipList *list, int32_t which,
                         uint8_t *word, int32_t wordLen, char *kana,
                         int32_t kanaLen, int32_t pos);
int32_t rud_lookupDictExt(RomUserDict *u, SkipList *list, int32_t which,
                         uint8_t *word, int32_t wordLen, void **value,
                         int32_t *valueLen, int32_t *pos);

/* ---- JpnUtil's codeset conversions ----------------------------------- */

/* IBM keeps these five in a separate object from the rest of JpnUtil, so they
   are in rom/jajp/codeconv.c rather than rom/jajp/jpnutil.c. Only the last
   two are called from outside it; the other three are here because the sweep
   holds them to IBM's answer one at a time. */
int32_t ju_SkipESCSeq(const char *text, long *at, int32_t *twoByte);
void    ju_jis2sjis(uint8_t *lead, uint8_t *trail);
void    ju_han2zen(const char *text, long *at, uint8_t *lead, uint8_t *trail,
                   int32_t kind);
long    ju_euc2shift(const char *in, long len, char *out, int32_t wantZen);
long    ju_seven2shift(const char *in, long len, char *out);

/* ---- InputManager ---------------------------------------------------- */

/* One thing waiting on the queue: where in the output it belonged, and what
   it reads back as. A mark carries nothing of its own and a parameter carries
   the text the caller wrote. The records are ours -- nothing outside
   rom/jajp/inputmngr.c holds one -- and the shape is IBM's all the same. The
   offsets in the comments below are IBM's, which is where the reading came
   from; ours are wider wherever a pointer is. */
typedef struct RomQueueElement RomQueueElement;

typedef struct RomQueueElementVtbl {
    void   *(*destroy)(RomQueueElement *e, int32_t freeIt);
    int32_t (*getData)(RomQueueElement *e, const char **out);
} RomQueueElementVtbl;

struct RomQueueElement {
    const RomQueueElementVtbl *vt;   /* +0x00 */
    int32_t at;                      /* +0x04, where it belonged */
    int32_t kind;                    /* +0x08, one for a parameter */
};

typedef struct QElementIndex {
    RomQueueElement base;
} QElementIndex;

typedef struct QElementParam {
    RomQueueElement base;
    char   *text;                    /* +0x0c, its own copy */
    int32_t len;                     /* +0x10 */
} QElementParam;

QElementParam *qp_ctor(QElementParam *p, const char *text, int32_t len,
                       int32_t at);

/* Text on its way in, and the marks and parameters that go with it. The
   offsets are IBM's, as above. */
typedef struct InputManager {
    ETIqueue        *queue;    /* +0x00 */
    RomInstParam    *param;    /* +0x04 */
    RomQueueElement *element;  /* +0x08, the one last taken off the queue */
    int32_t          codeset;  /* +0x0c, what the waiting text arrived in */
    const char      *text;     /* +0x10, waiting, not copied */
    uint32_t         len;      /* +0x14 */
    char            *buf;      /* +0x18, where a join is put */
} InputManager;

InputManager    *im_ctor(InputManager *m, RomInstParam *param);
void             im_dtor(InputManager *m);
void             im_remove(InputManager *m);
int32_t          im_addText(InputManager *m, const char *text, uint32_t len,
                            int32_t codeset);
int32_t          im_getText(InputManager *m, const char **outText,
                            uint32_t *outLen, const char *text,
                            uint32_t len);
int32_t          im_insertIndex(InputManager *m);
int32_t          im_addParam(InputManager *m, const char *text, int32_t len);
int32_t          im_hasMoreElement(InputManager *m);
RomQueueElement *im_getNextElement(InputManager *m);
int32_t          im_getNextOffset(InputManager *m);
int32_t          im_getNextData(InputManager *m, const char **out);
void             im_removeElement(InputManager *m);

/* The queue itself is the engine's, which src/eci/queue/eci_etiqueue.c has. These are
   IBM's own methods and carry IBM's convention: on a thirty-two bit build the
   object goes in a register rather than on the stack, so a declaration that
   leaves THIS off links and then passes the arguments one slot out. */
extern const uint32_t eq_bytes;
THIS ETIqueue *eq_ctor(ETIqueue *q, uint32_t capacity);
THIS void      eq_dtor(ETIqueue *q);
THIS void      eq_reset(ETIqueue *q);
THIS int32_t   eq_isEmpty(ETIqueue *q);
THIS int32_t   eq_push(ETIqueue *q, void *p);
THIS int32_t   eq_pop(ETIqueue *q, void **out);
THIS int32_t   eq_peekHead(ETIqueue *q, void **out);

/* ---- ConverterInterface ---------------------------------------------- */

/* The base half of a Romanizer, which is where everything the engine asks
   arrives. The block is passed as bytes because the record is IBM's;
   rom/jajp/romanizer.h says where each field sits. */
void     ci_initBase(void *c, RomInstParam *param);
void     ci_closeBase(void *c);
int32_t  ci_UCS2ToMBCS(void *c, const uint16_t *in, char **out, int32_t yen);
int32_t  ci_MBCSToUCS2(void *c, const char *in, uint16_t **out);
int32_t  ci_insertIndex(void *c);
int32_t  ci_addParam(void *c, const char *text, int32_t len);
int32_t  ci_outputIndexOrParam(void *c, char *out, int32_t at);
int32_t  ci_addText(void *c, const char *text, int32_t len,
                    int32_t inputType);
uint32_t ci_trans2defaultCodeset(void *c, void *text, int32_t len,
                                 int32_t codeset, const char **out);
int32_t  ci_stop(void *c);
int32_t  ci_resume(void *c);
void    *ci_newDict(void *c);
void     ci_deleteDict(void *c, void *dict);
void     ci_setDict(void *c, void *dict);
long     ci_findDictFile(void *c, const char *name, char *out);
int32_t  ci_loadDict(void *c, void *dict, int32_t which, const char *name);
int32_t  ci_saveDict(void *c, void *dict, int32_t which, const char *name);
int32_t  ci_lookupDictExt(void *c, void *dict, int32_t which, uint8_t *word,
                          int32_t wordLen, void **value, int32_t *valueLen,
                          int32_t *pos, int32_t codeset);
int32_t  ci_findFirstDictEntryExt(void *c, void *dict, int32_t which,
                                  void **word, int32_t *wordLen,
                                  void **extra, int32_t *extraLen,
                                  int32_t *pos, int32_t codeset);
int32_t  ci_findNextDictEntryExt(void *c, void *dict, int32_t which,
                                 void **word, int32_t *wordLen,
                                 void **extra, int32_t *extraLen,
                                 int32_t *pos, int32_t codeset);
int32_t  ci_updateDictExt(void *c, void *dict, int32_t which, uint8_t *word,
                          int32_t wordLen, char *kana, int32_t kanaLen,
                          int32_t pos, int32_t codeset);

/* ---- PhraseBuf -------------------------------------------------------- */

/* Where the path search's answers become phrases. The block is passed as
   bytes because the record is IBM's; rom/jajp/phrasebuf.h is the map, and
   rom/jajp/jpath.h is the map of the two records it reads out of JPath. */
void   *pb_ctor(void *pb, void *analysis);
void   *pb_destroy(void *pb, int32_t freeIt);
void    pb_Copy(void *pb, int16_t which);
void    pb_ModifyPos(void *pb, uint8_t *out, uint8_t pos);
int32_t pb_IsBunsetsuEnd(void *pb, const uint8_t *sub);
int32_t pb_IsSokuonTankanVerb(void *pb, const uint8_t *sub);
int16_t pb_GetSpecialPhraseType(void *pb, const uint8_t *w);
int16_t pb_ChkTTELink(void *pb, int32_t sokuon, const uint8_t *f);
void    pb_SetJrt(void *pb, const uint8_t *path, uint8_t *w,
                  int16_t *outKana, int16_t *outAccent);
int16_t pb_SetPhrasePart(void *pb, const uint8_t *path, int16_t n,
                         int16_t fzk, int32_t sokuon, uint8_t *out);
int16_t pb_SetPhraseBuffer(void *pb, uint8_t *out);

/* ---- JPath ------------------------------------------------------------ */

/* The path search. The block is passed as bytes because the record is IBM's;
   rom/jajp/jpath.h is the map, and the paths and sub-words it leaves behind
   are what PhraseBuf reads. */
void   *jp_ctor(void *jp, void *analysis);
void   *jp_destroy(void *jp, int32_t freeIt);
void    jp_Make(void *jp, int16_t at);
int32_t jp_AddPath(void *jp, const uint8_t *path, const uint8_t *entry,
                   uint8_t *out, int16_t nPaths, int16_t entryIndex);

/* The nine below are private to JPath in IBM's own source, and nothing else
   here calls them. They are not static all the same, because IBM's compiler
   gave each of them an external symbol and test/harness/romprims.c can
   therefore hold each one to IBM's own answer -- which the two public entry
   points above could not do on their own. Two of the nine are where all the
   difficulty is: a wrong branch in either would show as a slightly different
   reading of one sentence in a thousand, and nowhere else. */
int16_t jp_CheckType(void *jp, const uint8_t *tg);
int16_t jp_CheckAdFlag(void *jp, const uint8_t *lt, const uint8_t *rt,
                       const uint8_t *la, const uint8_t *ra, int16_t cost);
int16_t jp_JrtJrtCheck(void *jp, const uint8_t *lt, const uint8_t *rt,
                       const uint8_t *la, const uint8_t *ra, int32_t adjust);
int32_t jp_IsHead(void *jp, const uint8_t *e);
int32_t jp_IsEnd(void *jp, const uint8_t *e);
int32_t jp_IsContinuable(void *jp, const uint8_t *e);
int16_t jp_GetMoraOnPath(void *jp, const uint8_t *path, int16_t extra);
void    jp_SetWordAttr(void *jp, uint8_t *sub, const uint8_t *e);
void    jp_MakeJrtSubTable(void *jp);

/* ---- NumRead --------------------------------------------------------- */

/* How a number becomes words. The block is passed as bytes because the
   record is IBM's; rom/jajp/numread.h is the map. `Do' is the whole of it and
   the other ten are its passes, all public in IBM's own source. */
void    nr_Init(void *nr);
int16_t nr_SegmentYomiBlock(void *nr, const uint8_t *w, int16_t word);
int16_t nr_SetYomiType(void *nr, int16_t howmany);
void    nr_GenerateStdForm(void *nr, int16_t howmany);
int16_t nr_ApplySRule(void *nr, int16_t howmany, int16_t *got);
int16_t nr_ApplySRuleToKetaYomi(void *nr, int16_t which, int16_t n,
                                int16_t at, int16_t *got, const uint8_t *ss);
int16_t nr_ApplySRuleToBouYomi(void *nr, int16_t n, int16_t at, int16_t *got,
                               const uint8_t *ss);
int16_t nr_ApplySRuleToShosu(void *nr, int16_t n, int16_t at, int16_t *got,
                             const uint8_t *prev, const uint8_t *ss);
int16_t nr_ApplySRuleToBunsu(void *nr, int16_t n, int16_t at, int16_t *got,
                             const uint8_t *ss);
int16_t nr_ApplyJRule(void *nr, const uint8_t *w, int16_t word, int16_t n,
                      int16_t howmany, int16_t *got);
int16_t nr_Do(void *nr, const uint8_t *w, int16_t *pWord, int16_t *pOut);

/* ---- IntonPhrase ----------------------------------------------------- */

/* Where a speaker breathes, and what pitch each phrase carries.
   `ThreePhraseParsing' is the entry point and the other sixteen are its
   passes. Eleven of them are private in IBM's own source and are not static
   here for the same reason JPath's are not: access control is a compile-time
   thing, so each one has an external symbol in IBM's object and each can be
   held to IBM's own answer rather than only through the entry point.
   rom/jajp/intonphrase.h is the map of the record and of the two records it
   is handed. */
void    *ip_ctor(void *ip, void *owner);
void    *ip_destroy(void *ip, int32_t freeIt);
int16_t  ip_TableAllocBG(void *ip, uint16_t *count, uint16_t *last,
                         uint16_t *at, uint8_t *link, uint16_t n);
void    *ip_BreathGroupAlloc(void *ip);
void     ip_InitPhraseTable(void *ip, int16_t n);
void     ip_CheckPhraseToPhrase(void *ip, uint8_t *st);
void     ip_ProsodyControl(void *ip);
void     ip_ThreePhraseParsing(void *ip, void *table);
int16_t  ip_ModifyPType(void *ip, uint8_t type);
uint8_t  ip_CheckBreathGroup(void *ip, uint8_t *st, uint8_t *right,
                             uint8_t group);
int16_t  ip_CheckChoon(void *ip, const uint8_t *t, int16_t at);
void     ip_SetPhraseState(void *ip);
uint8_t  ip_PhraseParsing(void *ip, uint8_t *l, uint8_t *r, uint8_t group,
                          int32_t odd);
void     ip_RegroupPhrases(void *ip);
int16_t  ip_PhraseSeparate(void *ip, void *start, void *end, int16_t moras,
                           int16_t limit, int16_t floor_, int16_t mark);
void     ip_SetPauseLength(void *ip);
void     ip_SetPitchValues(void *ip);
int16_t  ip_SetIntonationalPhrase(void *ip);
uint8_t  ip_SetAccentualPhrase(void *ip, void *ph, uint8_t at);

/* ---- ProsCtrl -------------------------------------------------------- */

/* The prosody chain: breath groups in, the ESPR string the synthesiser reads
   out. `GenerateESPR' is the entry point and the other sixteen are its
   parts. Fourteen of them are private in IBM's own source and are not static
   here for the reason JPath's and IntonPhrase's are not: each has an external
   symbol in the object, so each can be held to IBM's own answer.
   rom/jajp/prosctrl.h maps the four records it builds. */
void    *pc_ctor(void *pc);
void     pc_dtor(void *pc);
int32_t  pc_GenerateESPR(void *pc, const void *env, int32_t param,
                         const char *text, const void *bgt, char *out,
                         uint32_t cap);
int32_t  pc_BG_T2BreathGroups(void *pc, const void *bgt, void **outGroups,
                              int32_t *outCount);
void     pc_FreeBreathGroups(void *pc, void *groups, int32_t count);
int32_t  pc_WriteESPR2(void *pc, void *groups, int32_t count, int32_t ms,
                       char *out, uint32_t cap);
int32_t  pc_WriteGokiInfo(void *pc, const uint8_t *ap, int32_t which,
                          int32_t kind, int32_t *at, char *buf, char *out,
                          uint32_t cap, uint32_t *len);
int32_t  pc_GetGokiInfoToWrite(void *pc, const uint8_t *ap, int32_t *m,
                               int32_t *first, int32_t *upto, int32_t *pos,
                               const char **name, int32_t stress,
                               int32_t kind);
int32_t  pc_WriteBGInfo(void *pc, int32_t ms, int32_t kind, int32_t last,
                        char *out, uint32_t cap, uint32_t *len);
int32_t  pc_WriteStressLevel(void *pc, int32_t at, int32_t from, int32_t to,
                             char *out, uint32_t cap, uint32_t *len,
                             int32_t force);
int32_t  pc_WriteUserIndex(void *pc, int32_t n, char *out, uint32_t cap,
                           uint32_t *len);
int32_t  pc_WriteDummyF0Pair(void *pc, int32_t n, char *out, uint32_t cap,
                             uint32_t *len);
int32_t  pc_WriteToOutBuf(void *pc, const char *what, char *out,
                          uint32_t cap, uint32_t *len);
int32_t  pc_ModifyWordProminence(void *pc, int32_t *prom, int32_t pos,
                                 int32_t flag);
int32_t  pc_IsBurstCons(void *pc, uint8_t code);
int32_t  pc_IsValidConsForSokuOn(void *pc, uint8_t code);

/* ---- MakeReadableJP -------------------------------------------------- */

/* The front of the analyser: what is not words -- a date, a time, an amount
   of money, a telephone number -- rewritten into the words a reader would
   say, before TextAnalysis sees any of it. Every method is written. All but
   the eight `normalize' methods and `convertSPR' are private in IBM's source
   and are not static here for the same reason JPath's are not.
   rom/jajp/makereadable.h is the record. */
void       *mrl_ctor(void *mr);
void        mrl_dtor(void *mr);
int32_t     mrl_copyAndReturn(void *mr, const char *text, uint32_t n,
                              char **buf, uint32_t *cap);
void       *mr_ctor(void *mr);
void        mr_dtor(void *mr);
void       *mr_destroy(void *mr, int32_t freeIt);
int32_t     mr_reallocateBuf(void *mr, char **buf, uint32_t used,
                             uint32_t want);
int32_t     mr_appendText(void *mr, const char *text, char **buf,
                          uint32_t *cap, uint32_t *len);
int32_t     mr_appendTextN(void *mr, const char *text, uint32_t n,
                           char **buf, uint32_t *cap, uint32_t *len);
int32_t     mr_appendChar(void *mr, const char *c, char **buf, uint32_t *cap,
                          uint32_t *len);
int32_t     mr_appendMakeReadableNumber(void *mr, void *num, char **buf,
                                        uint32_t *cap, uint32_t *len,
                                        int32_t trimZeros);
int32_t     mr_separateNumberByDecimalPoint(void *mr, const void *whole,
                                            void *left, void *right);
const char *mr_suppressZero(void *mr, const char *p, const char *end);
int32_t     mr_isCurrencySymbol(void *mr, const char *t, uint32_t *howLong);
int32_t     mr_isBoolSymbol(void *mr, const char *t, uint32_t *howLong);
int32_t     mr_isCurrencyPunct(void *mr, const char *t);
int32_t     mr_isDecimalPoint(void *mr, const char *t);
int32_t     mr_isParenthesis(void *mr, const char *t);
int32_t     mr_isTimeDelimiter(void *mr, const char *t);
int32_t     mr_isPlusMinusSymbol(void *mr, const char *t);
int32_t     mr_isDayOfWeek(void *mr, const char *t);
int32_t     mr_isRangeSymbol(void *mr, const char *t);
int32_t     mr_isDateSeparator(void *mr, const char *t);
int32_t     mr_isTelSymbol(void *mr, const char *t);
int32_t     mr_isDBCSDigit(void *mr, const char *t);
int32_t     mr_isDigit(void *mr, const char *t);
int32_t     mr_normalizeDigits(void *mr, const char *text, uint32_t n,
                               char **buf, uint32_t *cap, int32_t flag);
int32_t     mr_normalizeLiteral(void *mr, const char *text, uint32_t n,
                                char **buf, uint32_t *cap, int32_t flag);
int32_t     mr_normalizeBool(void *mr, const char *text, uint32_t n,
                             char **buf, uint32_t *cap, int32_t flag);
int32_t     mr_normalizeNumber(void *mr, const char *text, uint32_t n,
                               char **buf, uint32_t *cap, int32_t flag);
int32_t     mr_normalizePhone(void *mr, const char *text, uint32_t n,
                              char **buf, uint32_t *cap, int32_t flag);
int32_t     mr_normalizeTime(void *mr, const char *text, uint32_t n,
                             char **buf, uint32_t *cap, int32_t flag);
int32_t     mr_normalizeCurrency(void *mr, const char *text, uint32_t n,
                                 char **buf, uint32_t *cap, int32_t flag);
int32_t     mr_normalizeDate(void *mr, const char *text, uint32_t n,
                             char **buf, uint32_t *cap, int32_t flag);
int32_t     mr_convertSPR(void *mr, const char *text, uint32_t n,
                          char **buf, uint32_t *cap);

/* ---- Romanizer ------------------------------------------------------- */

/* The class the engine asks for Japanese, and the last of the romanizer to be
   written. rom/jajp/romanizer.h is the record and rom/jajp/jpnrom.c the code. */
int32_t     jrz_getOffset(void *rz);
void        jrz_ResetBuffer(void *rz);
void        jrz_ChangeDefYomi(void *rz, void *row);
int16_t     jrz_CheckDefYomiCMD(void *rz, void *row);
int16_t     jrz_InsertWordSeparator(void *rz, char *out, void *ph, int16_t at,
                                   int16_t i);
int16_t     jrz_GetWordIndex(void *rz, void *bg);
uint16_t    jrz_CountUserIndex(void *rz, char *s);
int8_t      jrz_Init(void *rz);
int32_t     jrz_SendResult(void *rz, char **out);
int16_t     jrz_ChangeYomi(void *rz, void *dst, void *src);
int32_t     jrz_GetParameter(void *rz, char *s);
int32_t     jrz_GetParameters(void *rz, char *s);
int32_t     jrz_GenerateESPR(void *rz, void *bg, char *out);
int16_t     jrz_GenerateRomajiOutput(void *rz, void *bg, void *ph, char *out,
                                    void *next);
int32_t     jrz_GenerateResult(void *rz, int32_t flush);
int32_t     jrz_processSentence(void *rz, char **out, int32_t more);
void       *jrz_ctor(void *rz, RomInstParam *param);
void        jrz_dtor(void *rz);
void       *jrz_destroy(void *rz, int32_t freeIt);
int32_t     jrz_isValidUserDictEntry(void *rz, const char *s, int32_t a,
                                     int32_t b);
int32_t     jrz_mbcs2Rom(void *rz, const char *s, char **out);
int32_t     jrz_rom2Mbcs(void *rz, const char *s, char **out);

/* ---- TextAnalysis ---------------------------------------------------- */

/* Almost none of it is written. `CopyJrtPart' is, because PhraseTable's
   number reader wants it: what IBM calls a `_P_JRT_T' is the same eighteen
   bytes as a word of a phrase, so rom/jajp/phrasebuf.h is its map too.
   rom/jajp/kakutei.c holds it, named for the object it came out of. */
void        ta_ClearInputBuf(void *ta);
int32_t     ta_IsEndOfInput(void *ta);
void        ta_InitPhraseTable(void *ta, int16_t n);
void        ta_ClearPhraseTable(void *ta);
int16_t     ta_Kinsoku(void *ta, int16_t slot, int16_t kind);
int16_t     ta_SearchJrtSeparate(void *ta, void *wp, int16_t from, int16_t to,
                                 int16_t moras);
void        ta_HandleOverflow(void *ta, void *row, int32_t at);
int16_t     ta_CheckPhraseLinkEnd(void *ta, int16_t slot);
int16_t     ta_SetNextPhraseBuffer(void *ta, int16_t off);
int16_t     ta_SetPhraseMakeTable(void *ta, int16_t off);
int16_t     ta_CheckPhraseLink(void *ta, int16_t buf, int16_t slot,
                               int16_t at);
void        ta_CopyJrtPart(const void *src, void *dst);
void        ta_CopyFzkPart(const void *src, void *dst, int16_t si, int16_t di);
uint8_t     ta_CountMoraInPhrase(void *ta, void *wp, int16_t *out);
int16_t     ta_UpdatePhraseBuffer(void *ta, void *wp, const uint8_t *dict);
int16_t     ta_Kakutei(void *ta, void *wp);
int16_t     ta_CheckMaru(void *ta, int16_t off);
int16_t     ta_PhraseMatching(void *ta, int16_t *out);
int16_t     ta_TextParsing(void *ta);
int16_t     ta_ProcessRemaining(void *ta);
int16_t     ta_ProcessSentence(void *ta);
int32_t     ta_SetText(void *ta, const char *text, int32_t len);
int32_t     ta_AppendText(void *ta, const char *text, int32_t len);
int32_t     ta_FormatAddText(void *ta, char *out, const char *text,
                             int32_t len);
int32_t     ta_processSnlkAnno(void *ta, const char *text, char **word,
                               char **reading, const char **end);
void       *ta_ctor(void *ta, void *romanizer);
int32_t     ta_initialize(void *ta);
void        ta_dtor(void *ta);
void       *ta_destroy(void *ta, int32_t freeIt);
void       *ta_GetPhraseTableRoot(void *ta);

/* And the four of `unknown.obj', which read the rows the search could not. */
void        ta_UnknownWord(void *ta);
int16_t     ta_ReParsing(void *ta, void *a, void *b);
int16_t     ta_GenUnknownPhrase(void *ta, void *a, void *b, int16_t nFzk,
                                int16_t at);
void        ta_SetOneMoraWord(void *ta, void *row);

/* And the four of `comppenalty.obj', which answer for a link in the search
   what `PhraseTable::SetUkeTypePhrase' answers for a row: which phrases may
   sit in front of this one. rom/jajp/comppenalty.c says where the two differ. */
void        ta_ExtKkrForLink(void *ta, uint8_t *kkr, int16_t tg,
                             const uint8_t *other);
int16_t     ta_SetJWordUkeTypeForLink(void *ta, uint8_t *uke,
                                      const uint8_t *tg0, const uint8_t *tgL);
void        ta_SetFWordUkeTypeForLink(void *ta, uint8_t at, uint8_t fzk,
                                      uint8_t *uke, const uint8_t *tg0,
                                      const uint8_t *tgL);
int16_t     ta_SetUkeTypeForLink(void *ta, uint8_t *uke, void *wp);

/* ---- PhraseTable ----------------------------------------------------- */

/* Where a phrase becomes a row of the phrase table. Every method is written;
   the sixteenth is a second copy of `DictSearch::IsOnin'. rom/jajp/phrasetable.h is the record
   and rom/jajp/phrasebuf.h and rom/jajp/intonphrase.h are the two it works
   over. */
int32_t     ptb_initialize(void *pt);
void        ptb_dtor(void *pt);
void       *ptb_destroy(void *pt, int32_t freeIt);
uint8_t     ptb_GetPosFromTG(void *pt, uint8_t tg);
uint8_t     ptb_GetFzkPosFromTG(void *pt, uint8_t tg);
int16_t     ptb_GetAffixType(void *pt, uint8_t *tg);
int16_t     ptb_TableAllocPhrase(void *pt, uint16_t *first, uint16_t *last,
                                 uint16_t *free_, uint8_t *link,
                                 uint16_t count);
void       *ptb_PhraseAlloc(void *pt);
void       *ptb_GeneratePhraseTable(void *pt);
void        ptb_ExtKKRPhrase(void *pt, uint8_t *kkr, int16_t tg,
                             uint8_t *other);
void        ptb_SetSubUkeType(void *pt, uint8_t *uke, int16_t tg,
                              uint8_t *flag);
void        ptb_SetNoneFzkKKR(void *pt, uint8_t *kkr, void *wp);
int16_t     ptb_SetUkeTypePhrase(void *pt, uint8_t *uke, void *wp);
void        ptb_FzkAccent(void *pt, uint8_t *in, uint8_t *out);
int16_t     ptb_CompoundWord(void *pt, void *wp, void *row);
void       *ptb_SetSuushiPhraseTable(void *pt, void *wp, void *row,
                                     uint8_t *jrt, int16_t before, int16_t n);
void       *ptb_SetSuushiPhrase(void *pt, void *wp, void *row, int16_t *out);
int16_t     ptb_SetPhraseTable(void *pt, int16_t a, int16_t b, void *wp,
                               uint8_t *c, int16_t *d, int16_t *e);

/* ---- TextNormalizer -------------------------------------------------- */

/* What decides which of MakeReadableJP's readers a piece of annotated text
   wants, and puts what came back in place of the annotation. Every method is
   written. rom/jajp/textnormalizer.h is the record. */
void       *tn_ctor(void *tn);
void        tn_dtor(void *tn);
int32_t     tn_reallocateBuf(void *tn, char **buf, uint32_t used,
                             uint32_t want);
int32_t     tn_getAnnoType(void *tn, const char *text, const char **argStart,
                           const char **after);
int32_t     tn_makeReadable(void *tn, const char *text, int32_t n,
                            char **buf, uint32_t *cap, int32_t flag);
int32_t     tn_normalizeText(void *tn, const char *text, uint32_t n,
                             char **buf, uint32_t *len);

/* ---- how much of this is written ------------------------------------ */

/* While this is defined the romanizer cannot convert anything yet, and says
   so by refusing to be made at all: src/eci/lang/eci_romanizer.c then behaves exactly
   as it did before there was a romanizer to find, and the instance is refused
   rather than speaking something wrong. Take it out when Romanizer is
   finished, and not before -- a half-written romanizer that quietly answers
   nothing is the one failure this whole exercise is arranged to prevent.
   test/harness/romcan.sh is unaffected either way: it registers its own romanizer
   over whatever is linked. */

#endif
