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

#define DM(name) dm_##name
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

#define DM(name) ibm_##name
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

    fflush(stdout);
#ifdef EVV_ROMPRIMS_OURS
    evv_port_finish();
#endif
    return 0;
}
