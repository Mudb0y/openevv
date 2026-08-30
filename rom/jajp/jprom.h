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

/* Lifted out of IBM's objects by tools/lift-romtables.py, which writes both
   lang/jajp/rom_tables_jajp.c and the header declaring what is in it. Each
   object's tables are one block there with a pointer per table, because that
   is how the original had them and its own code does not always stay inside
   the table it started in. The ones read as sixteen-bit values are cast where
   they are used; the block is aligned so that the cast is sound. */
#include "rom_tables_jajp.h"

/* The static dictionary itself, which tools/lift-rom.py writes: the words, the
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
int32_t rz_GetParameter(void *rz, char *p);

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
   methods return; src/eci_dict.c is what turns them into the older
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
