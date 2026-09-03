/* The IPA converters, ours against IBM's, one code point at a time.
 *
 * The suite cannot reach these either, and for a sharper reason than the
 * machine's primitives: until the SSML reader is turned on nothing in the
 * engine asks what an IPA symbol means, and even with it on a sentence
 * reaches only the handful of symbols that sentence happens to spell. What
 * has to be right is the whole table -- six languages, every code point IBM
 * gave an answer to, and every following symbol that changes that answer --
 * so this asks for all of it.
 *
 * The same file is compiled twice: once against our engine, where the
 * converters keep their own names, and once against IBM's objects, where
 * they are mangled. Both print the same lines and `test/ipa.sh' diffs them.
 *
 * The tables in src/eci/ssml/eci_ipatospr.c were read out of IBM's object by asking
 * it these questions, so this is not an independent check of how they were
 * derived -- it is the check that they were transcribed without loss and
 * that the lookup in front of them behaves as the original's switch did.
 * The whole-string call is compared as well, which is what covers the walk,
 * the successor handling and the language table in front of the six.
 *
 * Every code point from nought to 0x2100 is offered, which is past the last
 * one any of the six answers to; the twenty-one following symbols are every
 * value the original's own code compares against. What is printed is the
 * answer, the flag that says the second symbol was swallowed, and the return
 * code, so that an unknown symbol is a compared line rather than a silence.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "evv_abi.h"

#if defined(EVV_IPA_OURS)
#define IPA(name) /* our own name */
#else
#define IPA(name) MANGLED(name)
#endif

extern int32_t UsEnIPAToSPRConverter(int32_t cur, int32_t next, char *out,
                                     int32_t *used)
    IPA("?UsEnIPAToSPRConverter@@YAHHHPADAAH@Z");
extern int32_t UkEnIPAToSPRConverter(int32_t cur, int32_t next, char *out,
                                     int32_t *used)
    IPA("?UkEnIPAToSPRConverter@@YAHHHPADAAH@Z");
extern int32_t GrGrIPAToSPRConverter(int32_t cur, int32_t next, char *out,
                                     int32_t *used)
    IPA("?GrGrIPAToSPRConverter@@YAHHHPADAAH@Z");
extern int32_t FrFrIPAToSPRConverter(int32_t cur, int32_t next, char *out,
                                     int32_t *used)
    IPA("?FrFrIPAToSPRConverter@@YAHHHPADAAH@Z");
extern int32_t JpJpIPAToSPRConverter(int32_t cur, int32_t next, char *out,
                                     int32_t *used)
    IPA("?JpJpIPAToSPRConverter@@YAHHHPADAAH@Z");
extern int32_t KoKoIPAToSPRConverter(int32_t cur, int32_t next, char *out,
                                     int32_t *used)
    IPA("?KoKoIPAToSPRConverter@@YAHHHPADAAH@Z");
extern int32_t IPAToSPR(uint8_t *utf8, uint32_t bytes, char *spr,
                        uint32_t *room, int32_t lang)
    IPA("?IPAToSPR@@YAHPAEK0AAKW4ECILanguageDialect@@@Z");
extern int8_t IsValidUTF8(const uint8_t *s, int32_t length)
    IPA("?IsValidUTF8@@YA_NPBEH@Z");
extern int32_t ConvertUCS32toUTF8(const uint32_t *src, uint32_t count,
                                  uint8_t *dst, uint32_t *length)
    IPA("?ConvertUCS32toUTF8@@YAHPBKKPAEAAK@Z");
extern int32_t ConvertUTF8toUCS32(const uint8_t *src, uint32_t length,
                                  uint32_t *dst, uint32_t *count)
    IPA("?ConvertUTF8toUCS32@@YAHPBEKPAKAAK@Z");

typedef int32_t (*Converter)(int32_t, int32_t, char *, int32_t *);

/* Every value the original's own switches compare a following symbol
   against. Nought stands for nothing following. */
static const int32_t NEXTS[] = {
    0x0000, 0x0065, 0x0066, 0x0069, 0x0073, 0x0075, 0x0079, 0x026a,
    0x0283, 0x028a, 0x028f, 0x0292, 0x02b0, 0x02b7, 0x02d0, 0x02de,
    0x02ed, 0x0303, 0x0325, 0x0329
};

/* Past the last code point any of the six answers to. */
#define LAST_POINT 0x2100

/* The languages IPAToSPR knows how to route, plus two it does not, so that
   the fall-through is compared as well. */
static const int32_t LANGS[] = {
    0x10000, 0x10001, 0x20000, 0x30000, 0x30001, 0x40000, 0x50000,
    0x60000, 0x70000, 0x80000, 0x80800, 0x90000, 0xa0000, 0xb0000,
    0x110000
};

/* ---- one symbol at a time -------------------------------------------- */

static void oneConverter(const char *name, Converter convert)
{
    int32_t cur;
    size_t  i;

    for (cur = 0; cur <= LAST_POINT; cur++) {
        for (i = 0; i < sizeof NEXTS / sizeof NEXTS[0]; i++) {
            char    out[256];
            int32_t used = 0;
            int32_t rc;

            memset(out, 0, sizeof out);
            rc = convert(cur, NEXTS[i], out, &used);
            if (rc == 0 || used != 0 || out[0] != 0)
                printf("%s %04x %04x %d %d [%s]\n", name, (unsigned)cur,
                       (unsigned)NEXTS[i], (int)used, (int)rc, out);
        }
    }
}

/* ---- and whole strings ----------------------------------------------- */

/* A handful of real pronunciations, given as the UTF-8 a document would
   carry, so that the walk over a string is compared and not only the table
   under it. The last two are deliberately awkward: one ends on a symbol
   that wants a successor, and one is a single length mark with nothing in
   front of it. */
static const char *WORDS[] = {
    "h\xc9\x99\xcb\x88lo\xca\x8a",              /* h schwa stress l o upsilon */
    "\xcb\x88w\xc9\x9c\xcb\x90ld",              /* stress w rev-open-e length l d */
    "b\xc9\x9b\xcb\x90t\xc9\x99",               /* b open-e length t schwa */
    "\xca\x83\xc9\x94n",                        /* esh open-o n */
    "t\xca\x83i\xcb\x90z",                      /* t esh i length z */
    "d\xca\x92\xc9\x91\xcb\x90n",               /* d ezh script-a length n */
    "\xc3\xa6\x6b\x73",                         /* ae k s */
    "a\xc9\xaa",                                /* a small-cap-i, a diphthong */
    "a",                                        /* a, nothing after it */
    "\xcb\x90",                                 /* a length mark on its own */
    "",                                         /* nothing at all */
    "abcdefghijklmnopqrstuvwxyz"                /* every plain letter */
};

static void wholeStrings(void)
{
    size_t w, l;

    for (l = 0; l < sizeof LANGS / sizeof LANGS[0]; l++) {
        for (w = 0; w < sizeof WORDS / sizeof WORDS[0]; w++) {
            char     spr[512];
            uint32_t room = (uint32_t)(sizeof spr - 1);
            int32_t  rc;

            memset(spr, 0, sizeof spr);
            rc = IPAToSPR((uint8_t *)WORDS[w], (uint32_t)strlen(WORDS[w]),
                          spr, &room, LANGS[l]);
            printf("spr %05x %u %d %u [%s]\n", (unsigned)LANGS[l],
                   (unsigned)w, (int)rc, (unsigned)room, spr);
        }
    }

    /* And the same string into a buffer too small for it, at every size
       from nothing up to more than it needs, which is where the running
       out is decided. */
    for (l = 0; l < 20; l++) {
        char     spr[64];
        uint32_t room = (uint32_t)l;
        int32_t  rc;

        memset(spr, 0, sizeof spr);
        rc = IPAToSPR((uint8_t *)WORDS[0], (uint32_t)strlen(WORDS[0]),
                      spr, &room, 0x10000);
        printf("room %u %d %u [%s]\n", (unsigned)l, (int)rc,
               (unsigned)room, spr);
    }
}

/* ---- the code-set conversions beneath them --------------------------- */

static void codeSets(void)
{
    static const uint32_t POINTS[] = {
        0x00, 0x41, 0x7f, 0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xfffd,
        0xffff, 0x10000, 0x10ffff, 0x1fffff, 0x200000, 0x7fffffff
    };
    size_t i;

    for (i = 0; i < sizeof POINTS / sizeof POINTS[0]; i++) {
        uint8_t  bytes[16];
        uint32_t length = (uint32_t)sizeof bytes;
        int32_t  rc;
        size_t   j;

        memset(bytes, 0, sizeof bytes);
        rc = ConvertUCS32toUTF8(&POINTS[i], 1, bytes, &length);
        printf("u32 %08x %d %u", (unsigned)POINTS[i], (int)rc,
               (unsigned)length);
        for (j = 0; j < length; j++)
            printf(" %02x", bytes[j]);
        printf("\n");
    }

    /* Every one of those written out and read back, which is what puts the
       two halves against each other. */
    for (i = 0; i < sizeof POINTS / sizeof POINTS[0]; i++) {
        uint8_t  bytes[16];
        uint32_t length = (uint32_t)sizeof bytes;
        uint32_t back[8];
        uint32_t count = (uint32_t)(sizeof back / sizeof back[0]);
        int32_t  rc;
        size_t   j;

        memset(bytes, 0, sizeof bytes);
        if (ConvertUCS32toUTF8(&POINTS[i], 1, bytes, &length) != 0)
            continue;
        memset(back, 0, sizeof back);
        rc = ConvertUTF8toUCS32(bytes, length, back, &count);
        printf("u8 %08x %d %u", (unsigned)POINTS[i], (int)rc,
               (unsigned)count);
        for (j = 0; j < count; j++)
            printf(" %08x", (unsigned)back[j]);
        printf("\n");
    }

    /* And the validity test on every lead byte at every length, which is
       what the reader in front of it leans on. */
    for (i = 0; i < 256; i++) {
        uint8_t bytes[4];
        int32_t length;

        for (length = 1; length <= 4; length++) {
            bytes[0] = (uint8_t)i;
            bytes[1] = 0x80;
            bytes[2] = 0xbf;
            bytes[3] = 0xa0;
            printf("valid %02x %d %d\n", (unsigned)i, (int)length,
                   (int)IsValidUTF8(bytes, length));
        }
    }
}

int main(void)
{
    oneConverter("UsEn", UsEnIPAToSPRConverter);
    oneConverter("UkEn", UkEnIPAToSPRConverter);
    oneConverter("GrGr", GrGrIPAToSPRConverter);
    oneConverter("FrFr", FrFrIPAToSPRConverter);
    oneConverter("JpJp", JpJpIPAToSPRConverter);
    oneConverter("KoKo", KoKoIPAToSPRConverter);
    wholeStrings();
    codeSets();
    return 0;
}
