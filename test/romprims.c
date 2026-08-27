/* The romanizer's converters, ours against IBM's, one call at a time.
 *
 * test/romcan.sh cannot reach these. It proves the engine below the romanizer
 * by replaying what IBM's romanizer answered, and a class the romanizer reaches
 * for itself is never called on that path at all -- the codeset conversion is
 * exactly that. So this is the same arrangement test/prims.c uses for the
 * machine's primitives: one file compiled twice, once against our romanizer and
 * once against IBM's own objects, both printing the same lines for the same
 * sweep, and test/romprims.sh diffing them.
 *
 * What is swept is every input there is. Shift-JIS to UCS-2 takes every single
 * byte and every two-byte pair the converter accepts, which is about eleven and
 * a half thousand; UCS-2 to Shift-JIS takes all sixty-five thousand code
 * points, twice, because the backslash is treated differently depending on the
 * third argument. Nothing is sampled and nothing is left to a case somebody
 * thought of.
 *
 * DictMan is swept the same way: each of its twenty-six accessors over the
 * whole range of the table it reads. An accessor that answers a pointer cannot
 * be compared across two processes, so what is printed is how far that pointer
 * moved from index nought -- which is the stride -- and the bytes it points at,
 * which is the data. That means the sweep holds the lifted tables to IBM's own
 * as well as the arithmetic that indexes them.
 *
 * Four bytes are left out of the first sweep and the reason is not squeamish.
 * 0x80, 0xfe and 0xff reach the end of IBM's chain of tests without its walk
 * advancing over them, so its converter loops on the same byte for ever;
 * sweeping them would hang IBM's side rather than say anything. And a pair
 * beginning 0xfd is one IBM's converter accepts and its table does not hold,
 * so it answers with whatever the linker put after that table -- which is what
 * this sweep found, and it is not an answer that means anything. Both are
 * deliberate differences and both are written down in
 * rom/jajp/unicodeconvt.c.
 *
 * usage: romprims
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "evv_abi.h"

#ifdef EVV_ROMPRIMS_OURS
#include "jprom.h"
#endif

void evvRunStaticInitialisers(void);
#ifdef EVV_ROMPRIMS_OURS
void evv_port_start(void);
void evv_port_finish(void);
#endif

/* How the two sides are reached. Ours are C functions over structs of our own;
   IBM's are C++ methods over objects of a size only its own code knows, so
   the room is stated here and the constructors are called by their mangled
   names. */
#ifdef EVV_ROMPRIMS_OURS

typedef RomInstParam Param;
typedef UnicodeConverter Conv;

static Param param_room;
static Conv  conv_room;

static Param *makeParam(const char *path)
{
    return rp_ctor(&param_room, path);
}

static Conv *makeConv(Param *p)
{
    return uc_ctor(&conv_room, p);
}

#define TO_UCS2(c, in, out)      uc_MBCSToUCS2((c), (in), (out))
#define TO_MBCS(c, in, out, f)   uc_UCS2ToMBCS((c), (in), (out), (f))

#include "eci_key.h"

#define IBM_KEY_ROOM   sizeof(Key)
#define IBM_TRANS_ROOM sizeof(Translation)
#define IBM_LIST_ROOM  sizeof(SkipList)

#define ibm_keyCtor(self, b, n)     key_ctor((Key *)(self), (b), (n))
#define ibm_keyDtor(self)           key_dtor((Key *)(self))
#define ibm_keySet(self, b, n)      key_set((Key *)(self), (b), (n))
#define ibm_keyDump(self)           key_dump((Key *)(self))
#define ibm_transCtor(self, v, n, w, e, p) \
    tr_ctor((Translation *)(self), (v), (n), (w), (e), (p))
#define ibm_transDtor(self)         tr_dtor((Translation *)(self))
#define ibm_transDump(self)         tr_dump((Translation *)(self))
#define ibm_slCtor(self)            sl_ctor((SkipList *)(self))
#define ibm_slDtor(self)            sl_dtor((SkipList *)(self))
#define ibm_slInsert(self, k, t) \
    sl_insert((SkipList *)(self), (Key *)(k), (Translation *)(t))
#define ibm_slSearch(self, k)       sl_search((SkipList *)(self), (Key *)(k))
#define ibm_slMultiSearch(self, k) \
    sl_multiSearch((SkipList *)(self), (Key *)(k))
#define ibm_slRemove(self, k)       sl_remove((SkipList *)(self), (Key *)(k))
#define ibm_slGetFirst(self, k, t) \
    sl_getFirst((SkipList *)(self), (Key **)(k), (Translation **)(t))
#define ibm_slGetNext(self, k, t) \
    sl_getNext((SkipList *)(self), (Key **)(k), (Translation **)(t))
#define ibm_slSave(self, p)         sl_save((SkipList *)(self), (p))
#define ibm_slLoad(self, p)         sl_load((SkipList *)(self), (p))

#define DM(name) dm_##name
#define JU(name) ju_##name
#define STATIC_DICT_INIT() ((void)0)

#else

typedef struct Param Param;
typedef struct Conv Conv;

/* What IBM's two objects take up: 0x3c bytes for the parameter block and
   0x14 for the converter, which is what their own constructors are handed. */
static char param_room[0x3c];
static char conv_room[0x14];

extern THIS Param *ibm_paramCtor(void *self, const char *path)
    MANGLED("??0RomInstParam@@QAE@PBD@Z");
extern THIS Conv *ibm_convCtor(void *self, Param *p)
    MANGLED("??0UnicodeConverter@@QAE@PAVRomInstParam@@@Z");
extern THIS int32_t ibm_toUcs2(Conv *c, const char *in, uint16_t **out)
    MANGLED("?MBCSToUCS2@UnicodeConverter@@QAE?AW4RomError@@PBDPAPAG@Z");
extern THIS int32_t ibm_toMbcs(Conv *c, const uint16_t *in, char **out,
                               int32_t flag)
    MANGLED("?UCS2ToMBCS@UnicodeConverter@@QAE?AW4RomError@@PBGPAPADH@Z");

static Param *makeParam(const char *path)
{
    return ibm_paramCtor(param_room, path);
}

static Conv *makeConv(Param *p)
{
    return ibm_convCtor(conv_room, p);
}

#define TO_UCS2(c, in, out)      ibm_toUcs2((c), (in), (out))
#define TO_MBCS(c, in, out, f)   ibm_toMbcs((c), (in), (out), (f))

/* DictMan's accessors are static members, so they are ordinary functions with
   no this pointer, and cdecl rather than thiscall. The two that fill in the
   static rules are instance methods that touch nothing of the instance, and
   are handed a pointer to nothing. */
extern const uint8_t *ibm_GetFuncDict(void)
    MANGLED("?GetFuncDict@DictMan@@SAPAEXZ");
extern const uint8_t *ibm_GetFuncDictEx(void)
    MANGLED("?GetFuncDictEx@DictMan@@SAPAEXZ");
extern const uint8_t *ibm_GetAccentAt(uint16_t i)
    MANGLED("?GetAccentAt@DictMan@@SAPAEG@Z");
extern uint8_t ibm_GetKakariAt(uint16_t i)
    MANGLED("?GetKakariAt@DictMan@@SA?BEG@Z");
extern uint8_t ibm_GetPhrVectorAt(uint16_t i)
    MANGLED("?GetPhrVectorAt@DictMan@@SA?BEG@Z");
extern uint8_t ibm_GetPenaltyAt(uint16_t i)
    MANGLED("?GetPenaltyAt@DictMan@@SA?BEG@Z");
extern uint8_t ibm_GetTGAt2(uint8_t row, uint8_t col)
    MANGLED("?GetTGAt@DictMan@@SA?BEEE@Z");
extern const uint8_t *ibm_GetTGAt(uint8_t row)
    MANGLED("?GetTGAt@DictMan@@SAPAEE@Z");
extern const uint8_t *ibm_GetYomiDataPtr(void)
    MANGLED("?GetYomiDataPtr@DictMan@@SAPAU_yomi_data_t@@XZ");
extern const uint8_t *ibm_GetPhraseDataPtr(void)
    MANGLED("?GetPhraseDataPtr@DictMan@@SAPAU_phrase_data_t@@XZ");
extern const uint8_t *ibm_GetNumberDataPtr(void)
    MANGLED("?GetNumberDataPtr@DictMan@@SAPAU_suushi_data_t@@XZ");
extern uint8_t ibm_GetNumMDAt(uint16_t i)
    MANGLED("?GetNumMDAt@DictMan@@SAEG@Z");
extern const uint8_t *ibm_GetNumYomiPtrAt(uint8_t i)
    MANGLED("?GetNumYomiPtrAt@DictMan@@SAPAU_ym_tbl_t@@E@Z");
extern uint8_t ibm_GetNumJMDAt(uint16_t i)
    MANGLED("?GetNumJMDAt@DictMan@@SAEG@Z");
extern const uint8_t *ibm_GetNumJMDPtr(void)
    MANGLED("?GetNumJMDPtr@DictMan@@SAPAEXZ");
extern uint8_t ibm_GetNumJCCAt(uint16_t i)
    MANGLED("?GetNumJCCAt@DictMan@@SAEG@Z");
extern const uint8_t *ibm_GetNDictHashAt(uint16_t i, uint8_t j)
    MANGLED("?GetNDictHashAt@DictMan@@SAPAEGE@Z");
extern const uint8_t *ibm_GetTDictHashAt(uint16_t i)
    MANGLED("?GetTDictHashAt@DictMan@@SAPAEG@Z");
extern const uint8_t *ibm_GetKDictHashAt(uint16_t i)
    MANGLED("?GetKDictHashAt@DictMan@@SAPAEG@Z");
extern uint8_t ibm_GetKNDictHashAt(uint16_t i, uint8_t j)
    MANGLED("?GetKNDictHashAt@DictMan@@SAEGE@Z");
extern uint8_t ibm_GetKTDictHashAt(uint16_t i, uint8_t j)
    MANGLED("?GetKTDictHashAt@DictMan@@SAEGE@Z");
extern const uint8_t *ibm_GetEDictHashAt(uint16_t i)
    MANGLED("?GetEDictHashAt@DictMan@@SAPAEG@Z");
extern uint8_t ibm_GetItaijiHashAt(uint16_t i, uint8_t j)
    MANGLED("?GetItaijiHashAt@DictMan@@SAEGE@Z");
extern const uint8_t *ibm_GetItaijiAt(uint8_t row, uint16_t i)
    MANGLED("?GetItaijiAt@DictMan@@SAPAEEG@Z");
extern THIS void ibm_EngRulesInit(void *self)
    MANGLED("?EngRulesInit@DictMan@@QAEXXZ");
extern THIS void ibm_InitSupplementDictionary(void *self)
    MANGLED("?InitSupplementDictionary@DictMan@@QAEXXZ");

/* The two rule blocks and the two supplement pointers are private statics of
   DictMan, so they are asked for by their mangled names too. */
extern const uint8_t *ibm_EngToRomanRule[]
    __asm__("\"?s_EngToRomanRule@DictMan@@0U_rules@@A\"");
extern const uint8_t *ibm_RomanToKanaRule[]
    __asm__("\"?s_RomanToKanaRule@DictMan@@0U_rules@@A\"");
extern const uint8_t *ibm_paUserDict
    __asm__("\"?s_paUserDict@DictMan@@0PAEA\"");
extern const uint8_t *ibm_paUserDictIdx
    __asm__("\"?s_paUserDictIdx@DictMan@@0PAEA\"");

/* The static dictionary's pointer arrays are filled in at run time by
   StaticDict::Initialize, six and a half thousand unrolled stores. Ours are
   not: tools/lift-rom.py reads those stores and writes the arrays out with
   their contents already in them, so there is nothing to call. IBM's side has
   to call it or the supplement pointers come back empty -- which is what this
   sweep said before it did. */
extern void ibm_StaticDictInitialize(void)
    MANGLED("?Initialize@StaticDict@@SAXXZ");

/* JpnUtil is static members too, so the same again. */
extern int32_t ibm_IsSBCSKana(char c)
    MANGLED("?IsSBCSKana@JpnUtil@@SAHD@Z");
extern int32_t ibm_IsAlphaNumSym(char c)
    MANGLED("?IsAlphaNumSym@JpnUtil@@SAHD@Z");
extern int32_t ibm_IsNum(char c)
    MANGLED("?IsNum@JpnUtil@@SAHD@Z");
extern int32_t ibm_IsAlpha(char c)
    MANGLED("?IsAlpha@JpnUtil@@SAHD@Z");
extern int32_t ibm_IsDBCSLeadByte(char c)
    MANGLED("?IsDBCSLeadByte@JpnUtil@@SAHD@Z");
extern int32_t ibm_IsDBCSTrailByte(uint8_t c)
    MANGLED("?IsDBCSTrailByte@JpnUtil@@SAHE@Z");
extern int32_t ibm_IsValidDBCS(const char *p)
    MANGLED("?IsValidDBCS@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsKatakana(const char *p)
    MANGLED("?IsKatakana@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsHiragana(const char *p)
    MANGLED("?IsHiragana@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsLongVowel(const char *p)
    MANGLED("?IsLongVowel@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsSNLKDelim(const char *p)
    MANGLED("?IsSNLKDelim@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsDBCSNum(const char *p)
    MANGLED("?IsDBCSNum@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsKanjiNum(const char *p)
    MANGLED("?IsKanjiNum@JpnUtil@@SAHPBD@Z");
extern uint16_t ibm_MakeUshort(char *p)
    MANGLED("?MakeUshort@JpnUtil@@SAGPAD@Z");
extern int32_t ibm_DbCmp(const char *a, const char *b)
    MANGLED("?DbCmp@JpnUtil@@SAHPBD0@Z");
extern int32_t ibm_DbCmp2(const char *a, char b0, char b1)
    MANGLED("?DbCmp2@JpnUtil@@SAHPBDDD@Z");
extern void ibm_DbCpy(char *to, const char *from)
    MANGLED("?DbCpy@JpnUtil@@SAXPAD0@Z");
extern void ibm_DbSet(char *to, char b0, char b1)
    MANGLED("?DbSet@JpnUtil@@SAXPADDD@Z");
extern int32_t ibm_TwoChCmp(char b0, char b1, char *p)
    MANGLED("?TwoChCmp@JpnUtil@@SAHDDPAD@Z");
extern void ibm_TwoChCpy(char *from, char *to0, char *to1)
    MANGLED("?TwoChCpy@JpnUtil@@SAXPAD00@Z");
extern int32_t ibm_WriteRomajiStrBuf(uint8_t code, uint8_t *out)
    MANGLED("?WriteRomajiStrBuf@JpnUtil@@SAHEPAE@Z");
extern void ibm_ConvertDakuten(char *out, uint8_t kana, uint8_t mark)
    MANGLED("?ConvertDakuten@JpnUtil@@SAXPADEE@Z");
extern void ibm_Hiragana2Katakana(const uint8_t *in, uint8_t *out)
    MANGLED("?Hiragana2Katakana@JpnUtil@@SAXPBEPAE@Z");
extern int32_t ibm_YomiCmp(uint8_t *a, uint8_t lenA, uint8_t *b, uint8_t lenB)
    MANGLED("?YomiCmp@JpnUtil@@SAHPAEE0E@Z");
extern void ibm_TableFree(uint16_t *used, uint16_t *tail, uint16_t *freeHead,
                          void *table, uint16_t nil, uint16_t which)
    MANGLED("?TableFree@JpnUtil@@SAXPAG00PAU_LINK_TBL_T@@GG@Z");

/* The stored dictionary's five classes. These are C++ objects with vtables and
   thiscall methods, so they are declared the way the port declares IBM's
   engine classes: the room they take up is stated here and the constructors
   are called by their mangled names. */
#define IBM_KEY_ROOM   0x0c
#define IBM_TRANS_ROOM 0x20
#define IBM_LIST_ROOM  0x20

extern THIS void *ibm_keyCtor(void *self, char *bytes, int32_t len)
    MANGLED("??0Key@@QAE@PADH@Z");
extern THIS void ibm_keyDtor(void *self)
    MANGLED("??1Key@@UAE@XZ");
extern THIS void ibm_keySet(void *self, char *bytes, int32_t len)
    MANGLED("?set@Key@@QAEXPADH@Z");
extern THIS void ibm_keyDump(void *self)
    MANGLED("?dump@Key@@QAEXXZ");
extern THIS void *ibm_transCtor(void *self, const char *value, int32_t len,
                                const char *word, const char *extra,
                                int32_t pos)
    MANGLED("??0Translation@@QAE@PBDH00W4ECIPartOfSpeech@@@Z");
extern THIS void ibm_transDtor(void *self)
    MANGLED("??1Translation@@QAE@XZ");
extern THIS void ibm_transDump(void *self)
    MANGLED("?dump@Translation@@QAEXXZ");
extern THIS void *ibm_slCtor(void *self)
    MANGLED("??0SkipList@@QAE@XZ");
extern THIS void ibm_slDtor(void *self)
    MANGLED("??1SkipList@@UAE@XZ");
extern THIS int32_t ibm_slInsert(void *self, void *key, void *trans)
    MANGLED("?insert@SkipList@@QAEHPAVKey@@PAVTranslation@@@Z");
extern THIS void *ibm_slSearch(void *self, void *key)
    MANGLED("?search@SkipList@@QAEPAVTranslation@@PAVKey@@@Z");
extern THIS void *ibm_slMultiSearch(void *self, void *key)
    MANGLED("?multiSearch@SkipList@@QAEPAVTranslation@@PAVKey@@@Z");
extern THIS int32_t ibm_slRemove(void *self, void *key)
    MANGLED("?remove@SkipList@@QAEHPAVKey@@@Z");
extern THIS int32_t ibm_slGetFirst(void *self, void **key, void **trans)
    MANGLED("?getFirst@SkipList@@QAEHPAPAVKey@@PAPAVTranslation@@@Z");
extern THIS int32_t ibm_slGetNext(void *self, void **key, void **trans)
    MANGLED("?getNext@SkipList@@QAEHPAPAVKey@@PAPAVTranslation@@@Z");
extern THIS int32_t ibm_slSave(void *self, const char *path)
    MANGLED("?save@SkipList@@QAEHPBD@Z");
extern THIS int32_t ibm_slLoad(void *self, const char *path)
    MANGLED("?load@SkipList@@QAEHPBD@Z");

#define DM(name) ibm_##name
#define JU(name) ibm_##name
#define STATIC_DICT_INIT() ibm_StaticDictInitialize()

#endif

/* ---- printing ------------------------------------------------------- */

static void putBytes(const char *s)
{
    if (s == NULL) {
        fputs("-", stdout);
        return;
    }
    if (*s == 0) {
        fputs(".", stdout);
        return;
    }
    for (; *s; s++)
        printf("%02x", (unsigned char)*s);
}

static void putWide(const uint16_t *s)
{
    if (s == NULL) {
        fputs("-", stdout);
        return;
    }
    if (*s == 0) {
        fputs(".", stdout);
        return;
    }
    for (; *s; s++)
        printf("%04x", (unsigned)*s);
}

/* ---- the sweeps ----------------------------------------------------- */

/* Which lead bytes take a second byte, and which single bytes stand alone.
   The trailing byte range is the whole of what Shift-JIS uses, and the
   converter is offered all of it whether the pair means anything or not:
   what a table holds for a pair nobody writes still has to be the same on
   both sides. */
static void sweepToUcs2(Conv *c)
{
    static char in[3];
    uint16_t *out;
    int lead, trail, one;

    for (one = 0x01; one <= 0x7f; one++) {
        in[0] = (char)one;
        in[1] = 0;
        out = NULL;
        printf("MBCS %02x -> %d ", one, (int)TO_UCS2(c, in, &out));
        putWide(out);
        putchar('\n');
    }

    for (one = 0xa0; one <= 0xdf; one++) {
        in[0] = (char)one;
        in[1] = 0;
        out = NULL;
        printf("MBCS %02x -> %d ", one, (int)TO_UCS2(c, in, &out));
        putWide(out);
        putchar('\n');
    }

    for (lead = 0x81; lead <= 0xfc; lead++) {
        if (lead > 0x9f && lead < 0xe0)
            continue;
        for (trail = 0x40; trail <= 0xfc; trail++) {
            in[0] = (char)lead;
            in[1] = (char)trail;
            in[2] = 0;
            out = NULL;
            printf("MBCS %02x%02x -> %d ", lead, trail,
                   (int)TO_UCS2(c, in, &out));
            putWide(out);
            putchar('\n');
        }
    }
}

/* Every code point, with the flag off and on. Nought is the terminator and
   cannot be offered. */
static void sweepToMbcs(Conv *c)
{
    static uint16_t in[2];
    char *out;
    long u;
    int flag;

    for (flag = 0; flag <= 1; flag++)
        for (u = 1; u <= 0xffff; u++) {
            in[0] = (uint16_t)u;
            in[1] = 0;
            out = NULL;
            printf("UCS2 %04lx %d -> %d ", u, flag,
                   (int)TO_MBCS(c, in, &out, flag));
            putBytes(out);
            putchar('\n');
        }
}

/* A string rather than one character at a time, so that the walk and the
   growing of the buffers are exercised as well as the tables. */
static void sweepStrings(Conv *c)
{
    static const char *const mbcs[] = {
        "", "a", "abc", "\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd",
        "\x93\xfa\x96\x7b\x8c\xea", "A\x82\xa0" "1\x83\x41",
        "\xb1\xb2\xb3", "\x82\xa0\xb1" "z",
        NULL
    };
    static const uint16_t wide1[] = { 0x3053, 0x3093, 0 };
    static const uint16_t wide2[] = { 'a', 0x5c, 'b', 0xa000, 0xe000, 0 };
    static const uint16_t wide3[] = { 0xff21, 0x30a2, 0x4e00, 0 };
    const uint16_t *const wides[] = { wide1, wide2, wide3, NULL };
    int i;

    for (i = 0; mbcs[i] != NULL; i++) {
        uint16_t *out = NULL;

        printf("STR ");
        putBytes(mbcs[i]);
        printf(" -> %d ", (int)TO_UCS2(c, mbcs[i], &out));
        putWide(out);
        putchar('\n');
    }

    for (i = 0; wides[i] != NULL; i++) {
        char *out = NULL;

        printf("WSTR ");
        putWide(wides[i]);
        printf(" -> %d ", (int)TO_MBCS(c, wides[i], &out, 1));
        putBytes(out);
        putchar('\n');
    }

    /* And a long one, to make the byte buffer grow past its first kilobyte
       and be freed and made again. */
    {
        static uint16_t big[2000];
        char *out = NULL;
        int k;

        for (k = 0; k < 1999; k++)
            big[k] = (uint16_t)(0x3041 + (k % 80));
        big[1999] = 0;
        printf("LONG -> %d ", (int)TO_MBCS(c, big, &out, 1));
        printf("%d bytes\n", out ? (int)strlen(out) : -1);
    }
}

/* ---- DictMan ---------------------------------------------------------- */

/* How far each table goes, worked out from its length and the stride the
   accessor uses. The lengths are what tools/lift-romtables.py measured, which
   is the distance to the next table, so these are the indices the original
   could have answered for.
 *
 * A pointer cannot be compared between two processes, so what is printed for
 * an accessor that answers one is how far it moved from index nought -- which
 * is the stride, exactly -- and the bytes it points at, which is the data. */
enum {
    N_ACCENT     = 264 / 3,
    N_KAKARI     = 280,
    N_PHRVECTOR  = 2624,
    N_PENALTY    = 536,
    N_TG         = 720 / 4,
    N_NUMMD      = 120,
    N_NUMYOMI    = 360 / 6,
    N_NUMJMD     = 5808,
    N_NUMJCC     = 2080,
    N_NDICT      = 4096 / 4,
    N_TDICT      = 1024 / 2,
    N_KDICT      = 1024 / 2,
    N_KNDICT     = 1536 / 3,
    N_KTDICT     = 1024 / 2,
    N_EDICT      = 240 / 2,
    N_ITAIJIHASH = 80 / 2,
    N_ITAIJI_ROWS = 2,
    N_ITAIJI     = 0x7a6 / 2 - 1
};

static void putAt(const uint8_t *p, int n)
{
    int i;

    if (p == NULL) {
        fputs("-", stdout);
        return;
    }
    for (i = 0; i < n; i++)
        printf("%02x", p[i]);
}

static void sweepDictMan(void)
{
    const uint8_t *base;
    long i;
    int j;

    printf("DM funcdict ");
    putAt(DM(GetFuncDict)(), 16);
    printf(" ex ");
    putAt(DM(GetFuncDictEx)(), 16);
    putchar('\n');

    printf("DM yomi ");
    putAt(DM(GetYomiDataPtr)(), 16);
    printf(" phrase ");
    putAt(DM(GetPhraseDataPtr)(), 16);
    printf(" number ");
    putAt(DM(GetNumberDataPtr)(), 16);
    printf(" jmd ");
    putAt(DM(GetNumJMDPtr)(), 16);
    putchar('\n');

    base = DM(GetAccentAt)(0);
    for (i = 0; i < N_ACCENT; i++) {
        const uint8_t *at = DM(GetAccentAt)((uint16_t)i);

        printf("DM accent %ld %ld ", i, (long)(at - base));
        putAt(at, 3);
        putchar('\n');
    }

    for (i = 0; i < N_KAKARI; i++)
        printf("DM kakari %ld %02x\n", i, DM(GetKakariAt)((uint16_t)i));
    for (i = 0; i < N_PHRVECTOR; i++)
        printf("DM phrvector %ld %02x\n", i,
               DM(GetPhrVectorAt)((uint16_t)i));
    for (i = 0; i < N_PENALTY; i++)
        printf("DM penalty %ld %02x\n", i, DM(GetPenaltyAt)((uint16_t)i));

    base = DM(GetTGAt)(0);
    for (i = 0; i < N_TG; i++) {
        const uint8_t *at = DM(GetTGAt)((uint8_t)i);

        printf("DM tg %ld %ld ", i, (long)(at - base));
        putAt(at, 4);
        for (j = 0; j < 4; j++)
            printf(" %02x", DM(GetTGAt2)((uint8_t)i, (uint8_t)j));
        putchar('\n');
    }

    for (i = 0; i < N_NUMMD; i++)
        printf("DM nummd %ld %02x\n", i, DM(GetNumMDAt)((uint16_t)i));
    for (i = 0; i < N_NUMJMD; i++)
        printf("DM numjmd %ld %02x\n", i, DM(GetNumJMDAt)((uint16_t)i));
    for (i = 0; i < N_NUMJCC; i++)
        printf("DM numjcc %ld %02x\n", i, DM(GetNumJCCAt)((uint16_t)i));

    base = DM(GetNumYomiPtrAt)(0);
    for (i = 0; i < N_NUMYOMI; i++) {
        const uint8_t *at = DM(GetNumYomiPtrAt)((uint8_t)i);

        printf("DM numyomi %ld %ld ", i, (long)(at - base));
        putAt(at, 6);
        putchar('\n');
    }

    base = DM(GetNDictHashAt)(0, 0);
    for (i = 0; i < N_NDICT; i++)
        for (j = 0; j < 4; j++) {
            const uint8_t *at = DM(GetNDictHashAt)((uint16_t)i, (uint8_t)j);

            printf("DM ndict %ld %d %ld %02x\n", i, j, (long)(at - base),
                   at[0]);
        }

    base = DM(GetTDictHashAt)(0);
    for (i = 0; i < N_TDICT; i++) {
        const uint8_t *at = DM(GetTDictHashAt)((uint16_t)i);

        printf("DM tdict %ld %ld ", i, (long)(at - base));
        putAt(at, 2);
        putchar('\n');
    }

    base = DM(GetKDictHashAt)(0);
    for (i = 0; i < N_KDICT; i++) {
        const uint8_t *at = DM(GetKDictHashAt)((uint16_t)i);

        printf("DM kdict %ld %ld ", i, (long)(at - base));
        putAt(at, 2);
        putchar('\n');
    }

    base = DM(GetEDictHashAt)(0);
    for (i = 0; i < N_EDICT; i++) {
        const uint8_t *at = DM(GetEDictHashAt)((uint16_t)i);

        printf("DM edict %ld %ld ", i, (long)(at - base));
        putAt(at, 2);
        putchar('\n');
    }

    for (i = 0; i < N_KNDICT; i++)
        for (j = 0; j < 3; j++)
            printf("DM kndict %ld %d %02x\n", i, j,
                   DM(GetKNDictHashAt)((uint16_t)i, (uint8_t)j));
    for (i = 0; i < N_KTDICT; i++)
        for (j = 0; j < 2; j++)
            printf("DM ktdict %ld %d %02x\n", i, j,
                   DM(GetKTDictHashAt)((uint16_t)i, (uint8_t)j));
    for (i = 0; i < N_ITAIJIHASH; i++)
        for (j = 0; j < 2; j++)
            printf("DM itaijihash %ld %d %02x\n", i, j,
                   DM(GetItaijiHashAt)((uint16_t)i, (uint8_t)j));

    base = DM(GetItaijiAt)(0, 0);
    for (j = 0; j < N_ITAIJI_ROWS; j++)
        for (i = 0; i < N_ITAIJI; i++) {
            const uint8_t *at = DM(GetItaijiAt)((uint8_t)j, (uint16_t)i);

            printf("DM itaiji %d %ld %ld ", j, i, (long)(at - base));
            putAt(at, 2);
            putchar('\n');
        }
}

/* The two rule blocks, after the call that fills them in, and the two
   supplement pointers after theirs. What is printed is the count and the
   first bytes of each of the eight tables, which says both that the right
   table went into the right slot and that it holds what IBM's holds. */
static void sweepRules(void)
{
#ifdef EVV_ROMPRIMS_OURS
    const DictManRules *r[2];

    dm_EngRulesInit();
    dm_InitSupplementDictionary();
    r[0] = &dm_EngToRomanRule;
    r[1] = &dm_RomanToKanaRule;
#else
    const uint8_t **r[2];
    char room[0x40];

    ibm_EngRulesInit(room);
    ibm_InitSupplementDictionary(room);
    r[0] = ibm_EngToRomanRule;
    r[1] = ibm_RomanToKanaRule;
#endif

    {
        int k;

        for (k = 0; k < 2; k++) {
#ifdef EVV_ROMPRIMS_OURS
            const uint8_t *t[8];
            unsigned count = r[k]->count;

            t[0] = r[k]->from;    t[1] = r[k]->to;
            t[2] = r[k]->remain;  t[3] = r[k]->fromPos;
            t[4] = r[k]->toPos;   t[5] = r[k]->remainPos;
            t[6] = r[k]->accentValue; t[7] = r[k]->accentPos;
#else
            const uint8_t *t[8];
            unsigned count = *(const unsigned short *)((const char *)r[k]
                                                       + 0x20);
            int q;

            for (q = 0; q < 8; q++)
                t[q] = r[k][q];
#endif
            printf("DM rule %d count %u", k, count);
            {
                int q;

                for (q = 0; q < 8; q++) {
                    printf(" ");
                    putAt(t[q], 12);
                }
            }
            putchar('\n');
        }
    }

    printf("DM supp ");
#ifdef EVV_ROMPRIMS_OURS
    putAt(dm_paUserDict, 24);
    printf(" idx ");
    putAt(dm_paUserDictIdx, 24);
#else
    putAt(ibm_paUserDict, 24);
    printf(" idx ");
    putAt(ibm_paUserDictIdx, 24);
#endif
    putchar('\n');
}

/* ---- JpnUtil ---------------------------------------------------------- */

/* Every byte through the six predicates that take one, as one line each. */
static void sweepBytes(void)
{
    int b;

    for (b = 0; b <= 0xff; b++)
        printf("JU byte %02x %d%d%d%d%d%d\n", b,
               JU(IsSBCSKana)((char)b) ? 1 : 0,
               JU(IsAlphaNumSym)((char)b) ? 1 : 0,
               JU(IsNum)((char)b) ? 1 : 0,
               JU(IsAlpha)((char)b) ? 1 : 0,
               JU(IsDBCSLeadByte)((char)b) ? 1 : 0,
               JU(IsDBCSTrailByte)((uint8_t)b) ? 1 : 0);
}

/* Every two-byte character through the seven predicates that take a pointer,
   and through MakeUshort, as one line each. Sixty-five thousand lines is the
   whole space and there is no reason to sample it. */
static void sweepPairs(void)
{
    static char pair[3];
    long hi, lo;

    for (hi = 0; hi <= 0xff; hi++)
        for (lo = 1; lo <= 0xff; lo++) {
            pair[0] = (char)hi;
            pair[1] = (char)lo;
            pair[2] = 0;
            printf("JU pair %02lx%02lx %04x %d%d%d%d%d%d%d\n", hi, lo,
                   (unsigned)JU(MakeUshort)(pair),
                   JU(IsKatakana)(pair) ? 1 : 0,
                   JU(IsHiragana)(pair) ? 1 : 0,
                   JU(IsLongVowel)(pair) ? 1 : 0,
                   JU(IsSNLKDelim)(pair) ? 1 : 0,
                   JU(IsDBCSNum)(pair) ? 1 : 0,
                   JU(IsKanjiNum)(pair) ? 1 : 0,
                   JU(IsValidDBCS)(pair) ? 1 : 0);
        }
}

/* The two-byte copies and comparisons, over a handful of pairs each way. */
static void sweepTwoByte(void)
{
    static const char *const some[] = {
        "\x82\xa0", "\x82\xa1", "\x83\x41", "\x81\x42", "ab", "a\x01",
        NULL
    };
    int i, j;

    for (i = 0; some[i] != NULL; i++)
        for (j = 0; some[j] != NULL; j++) {
            char to[3];
            char c0 = 0, c1 = 0;

            printf("JU db %d %d cmp %d cmp2 %d twoch %d", i, j,
                   JU(DbCmp)(some[i], some[j]) ? 1 : 0,
                   JU(DbCmp2)(some[i], some[j][0], some[j][1]) ? 1 : 0,
                   JU(TwoChCmp)(some[i][0], some[i][1], (char *)some[j]) ? 1
                                                                        : 0);
            to[0] = 0; to[1] = 0; to[2] = 0;
            JU(DbCpy)(to, some[j]);
            printf(" cpy %02x%02x", (unsigned char)to[0],
                   (unsigned char)to[1]);
            JU(DbSet)(to, some[i][0], some[j][1]);
            printf(" set %02x%02x", (unsigned char)to[0],
                   (unsigned char)to[1]);
            JU(TwoChCpy)((char *)some[i], &c0, &c1);
            printf(" twocpy %02x%02x\n", (unsigned char)c0,
                   (unsigned char)c1);
        }
}

/* Every kana code into letters, four times over: into an empty buffer, into
   one already ending in the geminate marker, into one ending in that marker
   and an apostrophe, and into one ending in an ordinary syllable, which is
   what the capitalising row acts on. */
static void sweepRomaji(void)
{
    static const char *const before[] = { "", "Q", "Q'", "ka", "kaN", NULL };
    int b;
    long code;

    for (b = 0; before[b] != NULL; b++)
        for (code = 0; code <= 0xff; code++) {
            static uint8_t out[128];
            int32_t rc;

            strcpy((char *)out, before[b]);
            rc = JU(WriteRomajiStrBuf)((uint8_t)code, out);
            printf("JU romaji %d %02lx %d ", b, code, (int)rc);
            putBytes((const char *)out);
            putchar('\n');
        }
}

/* Every half-width kana with each of the two voicing marks. */
static void sweepDakuten(void)
{
    long kana;
    int  mark;

    for (mark = 0xde; mark <= 0xdf; mark++)
        for (kana = 0xa1; kana <= 0xdf; kana++) {
            char out[4];

            out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;
            JU(ConvertDakuten)(out, (uint8_t)kana, (uint8_t)mark);
            printf("JU dakuten %02lx %02x %02x%02x\n", kana, mark,
                   (unsigned char)out[0], (unsigned char)out[1]);
        }
}

/* Every hiragana into katakana, one at a time and then in strings. 0x82ec is
   left out: IsHiragana accepts it and the table has not got it, so IBM's walks
   it for ever. rom/jajp/jpnutil.c says what ours does instead. */
static void sweepKatakana(void)
{
    static const uint8_t *const strings[] = {
        (const uint8_t *)"\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd",
        (const uint8_t *)"a\x82\xa0" "1\x83\x41\xb1",
        (const uint8_t *)"\x93\xfa\x96\x7b\x8c\xea",
        (const uint8_t *)"",
        NULL
    };
    long lo;
    int  i;

    for (lo = 0x9f; lo <= 0xf1; lo++) {
        uint8_t in[3];
        uint8_t out[16];

        if (lo == 0xec)
            continue;
        in[0] = 0x82;
        in[1] = (uint8_t)lo;
        in[2] = 0;
        memset(out, 0, sizeof out);
        JU(Hiragana2Katakana)(in, out);
        printf("JU h2k 82%02lx ", lo);
        putBytes((const char *)out);
        putchar('\n');
    }

    for (i = 0; strings[i] != NULL; i++) {
        uint8_t out[64];

        memset(out, 0, sizeof out);
        JU(Hiragana2Katakana)(strings[i], out);
        printf("JU h2kstr %d ", i);
        putBytes((const char *)out);
        putchar('\n');
    }
}

static void sweepYomi(void)
{
    static uint8_t a[] = { 1, 2, 3, 4 };
    static uint8_t b[] = { 1, 2, 3, 5 };
    int i, j;

    for (i = 0; i <= 4; i++)
        for (j = 0; j <= 4; j++)
            printf("JU yomi %d %d %d %d\n", i, j,
                   (int)JU(YomiCmp)(a, (uint8_t)i, b, (uint8_t)j),
                   (int)JU(YomiCmp)(a, (uint8_t)i, a, (uint8_t)j));
}

/* A chain of eight, unlinked one at a time, printed after each. The nil is
   0xffff and the first two indices stand for the head and the tail. */
static void sweepTableFree(void)
{
    enum { N = 8, NIL = 0xffff };
    uint16_t table[N * 2];
    uint16_t used = 0, tail = NIL, freeHead = NIL;
    int i, k;

    for (i = 0; i < N; i++) {
        table[i * 2] = (uint16_t)(i == 0 ? NIL : i - 1);
        table[i * 2 + 1] = (uint16_t)(i == N - 1 ? NIL : i + 1);
    }
    used = N - 1;

    for (k = 0; k < N; k++) {
        JU(TableFree)(&used, &tail, &freeHead, (void *)table, NIL,
                      (uint16_t)k);
        printf("JU tablefree %d used %04x tail %04x free %04x", k, used, tail,
               freeHead);
        for (i = 0; i < N; i++)
            printf(" %04x/%04x", table[i * 2], table[i * 2 + 1]);
        putchar('\n');
    }
}

/* ---- the stored dictionary -------------------------------------------- */

/* What is compared and what cannot be. A skip list draws each entry's tower
   height at random, and IBM's constructor seeds that from the clock, so the
   towers differ between two runs of the same program and a saved file is not
   the same file twice. What does not depend on the draw is every answer the
   list gives: order decides those, and the towers only decide how quickly they
   are reached. So this prints answers -- what a search found, what a walk
   yields, what a remove said -- and never a level or a file byte. A save and a
   load are checked by walking the loaded list and finding the same entries in
   the same order. */

/* The keys, chosen to exercise what the comparisons do: the same first bytes
   with different lengths, so a prefix search has something to find; keys that
   sort before and after each other; and one inserted twice, so the replace
   path is walked. */
static const char *const SL_KEYS[] = {
    "\x82\xa0", "\x82\xa0\x82\xa2", "\x82\xa0\x82\xa2\x82\xa4",
    "\x82\xa2", "\x82\xa4", "\x83\x41", "\x83\x41\x83\x43",
    "\x93\xfa\x96\x7b", "\x93\xfa\x96\x7b\x8c\xea",
    "ab", "abcd", "abcdef", "b", "zz", "\x82\xa0", "\x81\x40",
    "\xfc\xfc", "m", "\x82\xf1", "\x83\x93",
    NULL
};

static void slWalk(void *list, const char *what)
{
    void *key = 0;
    void *trans = 0;
    int   n = 0;
    int32_t more;

    printf("SL walk %s\n", what);
    for (more = ibm_slGetFirst(list, &key, &trans); more != 0;
         more = ibm_slGetNext(list, &key, &trans)) {
        printf("SL  %d ", n++);
        ibm_keyDump(key);
        ibm_transDump(trans);
        if (n > 200)
            break;
    }
    printf("SL walked %d\n", n);
}

static void sweepSkipList(void)
{
    static char list[IBM_LIST_ROOM];
    static char list2[IBM_LIST_ROOM];
    static char key[IBM_KEY_ROOM];
    static char trans[IBM_TRANS_ROOM];
    const char *path = "romprims-skiplist.tmp";
    int i;

    ibm_slCtor(list);

    for (i = 0; SL_KEYS[i] != NULL; i++) {
        char what[64];
        int32_t rc;

        sprintf(what, "value-%d", i);
        ibm_keyCtor(key, (char *)SL_KEYS[i], (int32_t)strlen(SL_KEYS[i]));
        ibm_transCtor(trans, what, (int32_t)strlen(what), "word", "extra",
                      i % 5);
        rc = ibm_slInsert(list, key, trans);
        printf("SL insert %d rc %d\n", i, (int)rc);
        ibm_transDtor(trans);
        ibm_keyDtor(key);
    }

    slWalk(list, "after inserting");

    /* Every key that went in, and a few that did not. */
    for (i = 0; SL_KEYS[i] != NULL; i++) {
        void *found;

        ibm_keyCtor(key, (char *)SL_KEYS[i], (int32_t)strlen(SL_KEYS[i]));
        found = ibm_slSearch(list, key);
        printf("SL search %d %s", i, found ? "found " : "none\n");
        if (found)
            ibm_transDump(found);
        ibm_keyDtor(key);
    }
    {
        static const char *const absent[] = { "q", "\x82\xa1", "zzz", NULL };

        for (i = 0; absent[i] != NULL; i++) {
            void *found;

            ibm_keyCtor(key, (char *)absent[i],
                        (int32_t)strlen(absent[i]));
            found = ibm_slSearch(list, key);
            printf("SL absent %d %s\n", i, found ? "found" : "none");
            ibm_keyDtor(key);
        }
    }

    /* Every prefix of a long key at once, which is what a Japanese lookup
       wants: the count comes back in the four bytes before the answer. */
    {
        static const char *const multi[] = {
            "\x82\xa0\x82\xa2\x82\xa4", "\x93\xfa\x96\x7b\x8c\xea",
            "abcdef", NULL
        };

        for (i = 0; multi[i] != NULL; i++) {
            void *out;

            ibm_keyCtor(key, (char *)multi[i], (int32_t)strlen(multi[i]));
            out = ibm_slMultiSearch(list, key);
            if (out == 0) {
                printf("SL multi %d none\n", i);
            } else {
                int32_t n = *(const int32_t *)((const char *)out - 4);
                int32_t j;

                printf("SL multi %d count %d\n", i, (int)n);
                for (j = 0; j < n; j++) {
                    printf("SL  multi %d %d ", i, (int)j);
                    ibm_transDump((char *)out + j * IBM_TRANS_ROOM);
                }
            }
            ibm_keyDtor(key);
        }
    }

    /* Out again, every other one. */
    for (i = 0; SL_KEYS[i] != NULL; i += 2) {
        int32_t rc;

        ibm_keyCtor(key, (char *)SL_KEYS[i], (int32_t)strlen(SL_KEYS[i]));
        rc = ibm_slRemove(list, key);
        printf("SL remove %d rc %d\n", i, (int)rc);
        ibm_keyDtor(key);
    }

    slWalk(list, "after removing");

    /* And round through a file. The file's own bytes cannot be compared --
       the towers in it are random -- but what comes back has to be what went
       in. */
    printf("SL save %d\n", (int)ibm_slSave(list, path));
    ibm_slCtor(list2);
    printf("SL load %d\n", (int)ibm_slLoad(list2, path));
    slWalk(list2, "after loading");

    ibm_slDtor(list2);
    ibm_slDtor(list);
    remove(path);
}

int main(void)
{
    Param *p;
    Conv  *c;

    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
#ifdef EVV_ROMPRIMS_OURS
    evv_port_start();
#endif
    evvRunStaticInitialisers();

    STATIC_DICT_INIT();

    p = makeParam("");
    if (p == NULL) {
        printf("romprims: no parameter block\n");
        return 1;
    }
    c = makeConv(p);
    if (c == NULL) {
        printf("romprims: no converter\n");
        return 1;
    }

    sweepToUcs2(c);
    sweepToMbcs(c);
    sweepStrings(c);
    sweepDictMan();
    sweepRules();
    sweepBytes();
    sweepPairs();
    sweepTwoByte();
    sweepRomaji();
    sweepDakuten();
    sweepKatakana();
    sweepYomi();
    sweepTableFree();
    sweepSkipList();

    fflush(stdout);
#ifdef EVV_ROMPRIMS_OURS
    evv_port_finish();
#endif
    return 0;
}
