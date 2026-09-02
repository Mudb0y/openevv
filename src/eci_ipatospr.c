/* A pronunciation written in IPA, turned into one the engine can say.
 *
 * SSML lets a document give a word's pronunciation outright, in the
 * International Phonetic Alphabet: <phoneme alphabet="ipa" ph="həˈloʊ">.
 * The engine has never heard of IPA. What it takes is its own spelling,
 * SPR, in the annotation ` `[hxEHlOW] ', so something has to stand between
 * the two, and this is it.
 *
 * The mapping is per language, because the same IPA symbol is a different
 * sound in each and because each language's own phoneme set is different.
 * IBM wrote six converters and shares them out among fifteen languages,
 * which is the arrangement kept here: US and British English have their
 * own, German has one that French Canada also uses, France has one, and
 * the Japanese one serves Italian, Chinese, Cantonese, Portuguese and
 * Japanese while the Korean one serves Korean, Finnish and one Japanese
 * code set.
 *
 * Two of those are the original's own doing rather than anything sensible,
 * and both are kept. French Canada is sent to the German converter, which
 * is almost certainly a slip for the French one -- the two constants sit
 * next to each other in its switch. And the Japanese converter is what
 * Italian, Chinese and Portuguese get, so a document that gives an Italian
 * word an IPA pronunciation is answered in Japanese phonemes.
 *
 * Each converter takes one code point and the one after it, because IPA
 * writes a diphthong as two symbols and a length mark as a symbol of its
 * own: it answers what the pair means and says whether it swallowed the
 * second one. Where the pair means nothing special the first symbol's own
 * answer stands.
 *
 * The tables are read out of IBM's object rather than typed from a chart,
 * and `test/ipa.sh' is what says they are right: it asks IBM's own
 * converters and ours the same question for every code point up to 0x2100
 * with every following symbol that can matter, and passes only on identical
 * answers. That is where these numbers came from and it is what keeps them
 * true.
 *
 * The three code-set conversions at the head are in this object too, and
 * `ConvertUCS2toUTF8' among them is what the engine's own text path has
 * always used; it lived in a file of its own until the rest of the object
 * arrived.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "evv_abi.h"
#include "eci_ssml.h"

/* What the converters and the conversions answer. */
#define UTF8_BAD_LEAD   (-4)
#define UTF8_MALFORMED  (-5)
#define UTF8_NO_ROOM    (-3)
#define UTF8_SHORT      (-2)
#define IPA_NO_INPUT    (-6)
#define IPA_UNKNOWN     (-1)

/* How much room one symbol's answer gets, and the widest a converter's
   answer is; the original's own buffer is the first of these. */
#define SPR_ROOM        0x60

/* The top bits a UTF-8 lead byte carries for each length, and the mask that
   takes them off again. */
static const uint8_t UTF8FirstByteMark[] = {
    0x00, 0x00, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc
};

static const uint8_t UTF8FirstByteMask[] = {
    0xff, 0x1f, 0x0f, 0x07, 0x03, 0x01, 0x00, 0x00
};

/* ---- code sets ------------------------------------------------------- */

/* Whether a run of UTF-8 bytes is well formed. The trailing bytes are
   checked from the last back to the first, then the lead byte is held
   against the three ranges that bound what may follow it: 0xe0 wants a
   second byte of at least 0xa0, 0xf0 at least 0x90, and 0xf4 at most 0x8f.
   The lead byte itself must be outside 0x80 to 0xc1 and not above 0xf4. */
int8_t IsValidUTF8(const uint8_t *s, int32_t length)
{
    const uint8_t *p = s + length;
    uint8_t        b;

    switch (length) {
    case 4:
        b = *--p;
        if (b < 0x80 || b > 0xbf)
            return 0;
        /* fall through */
    case 3:
        b = *--p;
        if (b < 0x80 || b > 0xbf)
            return 0;
        /* fall through */
    case 2:
        b = *--p;
        if (b > 0xbf)
            return 0;
        switch (s[0]) {
        case 0xe0:
            if (b < 0xa0)
                return 0;
            break;
        case 0xf0:
            if (b < 0x90)
                return 0;
            break;
        case 0xf4:
            if (b > 0x8f)
                return 0;
            break;
        default:
            if (b < 0x80)
                return 0;
            break;
        }
        /* fall through */
    case 1:
        if (s[0] >= 0x80 && s[0] < 0xc2)
            return 0;
        if (s[0] > 0xf4)
            return 0;
        return 1;
    default:
        return 0;
    }
}

/* Thirty-two bit characters written out as bytes. The caller says how much
   room there is in the length and reads back how much was used; a character
   that would not fit stops the walk without being half written, and what
   comes back is minus three. Anything at or above 0x200000 is replaced by
   the replacement character rather than refused. */
int32_t ConvertUCS32toUTF8(const uint32_t *src, uint32_t count,
                           uint8_t *dst, uint32_t *length)
{
    const uint32_t *in    = src;
    const uint32_t *end   = src + count;
    uint8_t        *out   = dst;
    const uint8_t  *limit = dst + *length;
    int32_t         rc    = 0;

    while (in < end) {
        uint32_t ch = *in++;
        uint16_t bytes;

        if (ch < 0x80)
            bytes = 1;
        else if (ch < 0x800)
            bytes = 2;
        else if (ch < 0x10000)
            bytes = 3;
        else if (ch < 0x200000)
            bytes = 4;
        else {
            bytes = 2;
            ch = 0xfffd;
        }

        out += bytes;
        if (out > limit) {
            in--;
            out -= bytes;
            rc = UTF8_NO_ROOM;
            break;
        }

        switch (bytes) {
        case 4:
            *--out = (uint8_t)((ch | 0x80) & 0xbf);
            ch >>= 6;
            /* fall through */
        case 3:
            *--out = (uint8_t)((ch | 0x80) & 0xbf);
            ch >>= 6;
            /* fall through */
        case 2:
            *--out = (uint8_t)((ch | 0x80) & 0xbf);
            ch >>= 6;
            /* fall through */
        case 1:
            *--out = (uint8_t)(UTF8FirstByteMark[bytes] | ch);
            break;
        }
        out += bytes;
    }

    *length = (uint32_t)(out - dst);
    return rc;
}

/* Sixteen-bit characters written out as bytes: one, two or three each.
   Surrogate pairs are not treated as pairs; each half is encoded on its
   own, which is what the original does. */
int32_t u8_convertUCS2toUTF8(const uint16_t *src, int32_t count,
                             uint8_t *dst, uint32_t *length)
{
    const uint16_t *end   = src + count;
    uint8_t        *out   = dst;
    const uint8_t  *limit = dst + *length;
    int32_t         rc    = 0;

    while (src < end) {
        uint16_t ch = *src++;
        uint16_t bytes;

        if (ch < 0x80)
            bytes = 1;
        else if (ch < 0x800)
            bytes = 2;
        else
            bytes = 3;

        if (out + bytes > limit) {
            src--;
            rc = UTF8_NO_ROOM;
            break;
        }

        switch (bytes) {
        case 3:
            out[0] = (uint8_t)((ch >> 12) + 0xe0);
            out[1] = (uint8_t)(((ch & 0xfc0) >> 6) + 0x80);
            out[2] = (uint8_t)((ch & 0x3f) + 0x80);
            break;
        case 2:
            out[0] = (uint8_t)(((ch & 0x7c0) >> 6) + 0xc0);
            out[1] = (uint8_t)((ch & 0x3f) + 0x80);
            break;
        case 1:
            out[0] = (uint8_t)ch;
            break;
        }

        out += bytes;
    }

    *length = (uint32_t)(out - dst);
    return rc;
}

/* Bytes read back as thirty-two bit characters. A character above the
   sixteen-bit range is written as a surrogate pair, and the second half of
   that pair is where the original has a defect that is kept: it masks the
   remainder with ten rather than with the low ten bits, so every character
   above 0xffff comes back with a wrong low surrogate. Nothing in the SSML
   path reaches it -- IPA is all below 0x3000 -- and correcting it would be
   the first thing here that answered differently from IBM. */
int32_t ConvertUTF8toUCS32(const uint8_t *src, uint32_t length,
                           uint32_t *dst, uint32_t *count)
{
    const uint8_t *in    = src;
    const uint8_t *end   = src + length;
    uint32_t      *out   = dst;
    const uint32_t *limit = dst + *count;
    int32_t        rc    = 0;

    while (in < end) {
        uint32_t ch = 0;
        uint8_t  bytes[4];
        uint16_t extra;
        int32_t  i;

        if (in[0] <= 0x7f)
            extra = 0;
        else if (in[0] >= 0xc0 && in[0] <= 0xdf)
            extra = 1;
        else if (in[0] >= 0xe0 && in[0] <= 0xef)
            extra = 2;
        else if (in[0] >= 0xf0 && in[0] <= 0xf7)
            extra = 3;
        else {
            rc = UTF8_BAD_LEAD;
            break;
        }

        if (in + extra >= end) {
            rc = UTF8_SHORT;
            break;
        }

        if (!IsValidUTF8(in, extra + 1)) {
            rc = UTF8_MALFORMED;
            break;
        }

        /* The bytes are taken most significant last, the lead byte having
           its length bits masked off and every other its top two. */
        for (i = extra; i >= 0; i--) {
            bytes[i] = *in++;
            if (i == extra)
                bytes[i] = (uint8_t)(bytes[i] & UTF8FirstByteMask[extra]);
            else
                bytes[i] = (uint8_t)(bytes[i] & 0x3f);
        }

        switch (extra) {
        case 3:
            ch = (ch + bytes[3]) << 6;
            /* fall through */
        case 2:
            ch = (ch + bytes[2]) << 6;
            /* fall through */
        case 1:
            ch = (ch + bytes[1]) << 6;
            /* fall through */
        case 0:
            ch = ch + bytes[0];
            break;
        }

        if (out >= limit) {
            in -= extra + 1;
            rc = UTF8_NO_ROOM;
            break;
        }

        if (ch <= 0xffff) {
            if (ch >= 0xd800 && ch <= 0xdfff) {
                rc = UTF8_MALFORMED;
                break;
            }
            *out++ = ch;
        } else if (ch > 0x10ffff) {
            rc = UTF8_MALFORMED;
            break;
        } else {
            if (out + 1 >= limit) {
                rc = UTF8_NO_ROOM;
                break;
            }
            ch -= 0x10000;
            *out++ = (ch >> 10) + 0xd800;
            *out++ = (ch & 0xa) + 0xdc00;
        }
    }

    *count = (uint32_t)(out - dst);
    return rc;
}

/* ---- what each symbol means in each language ------------------------- */

/* One IPA symbol on its own, and one followed by a particular other. The
   `used' byte says the second symbol was swallowed. */
typedef struct {
    uint16_t    cur;
    uint8_t     used;
    const char *spr;
} IpaOne;

typedef struct {
    uint16_t    cur;
    uint16_t    next;
    uint8_t     used;
    const char *spr;
} IpaTwo;

/* UsEn: 86 single, 22 paired */
static const IpaOne usen_one[] = {
    { 0x002e, 0, "." },   /* FULL STOP */
    { 0x0061, 0, "a" },   /* LATIN SMALL LETTER A */
    { 0x0062, 0, "b" },   /* LATIN SMALL LETTER B */
    { 0x0063, 0, "k" },   /* LATIN SMALL LETTER C */
    { 0x0064, 0, "d" },   /* LATIN SMALL LETTER D */
    { 0x0065, 0, "e" },   /* LATIN SMALL LETTER E */
    { 0x0066, 0, "f" },   /* LATIN SMALL LETTER F */
    { 0x0067, 0, "g" },   /* LATIN SMALL LETTER G */
    { 0x0068, 0, "h" },   /* LATIN SMALL LETTER H */
    { 0x0069, 0, "i" },   /* LATIN SMALL LETTER I */
    { 0x006a, 0, "y" },   /* LATIN SMALL LETTER J */
    { 0x006b, 0, "k" },   /* LATIN SMALL LETTER K */
    { 0x006c, 0, "l" },   /* LATIN SMALL LETTER L */
    { 0x006d, 0, "m" },   /* LATIN SMALL LETTER M */
    { 0x006e, 0, "n" },   /* LATIN SMALL LETTER N */
    { 0x006f, 0, "o" },   /* LATIN SMALL LETTER O */
    { 0x0070, 0, "p" },   /* LATIN SMALL LETTER P */
    { 0x0072, 0, "r" },   /* LATIN SMALL LETTER R */
    { 0x0073, 0, "s" },   /* LATIN SMALL LETTER S */
    { 0x0074, 0, "t" },   /* LATIN SMALL LETTER T */
    { 0x0075, 0, "u" },   /* LATIN SMALL LETTER U */
    { 0x0076, 0, "v" },   /* LATIN SMALL LETTER V */
    { 0x0077, 0, "w" },   /* LATIN SMALL LETTER W */
    { 0x0078, 0, "h" },   /* LATIN SMALL LETTER X */
    { 0x0079, 0, "wi" },   /* LATIN SMALL LETTER Y */
    { 0x007a, 0, "z" },   /* LATIN SMALL LETTER Z */
    { 0x00e6, 0, "A" },   /* LATIN SMALL LETTER AE */
    { 0x00f0, 0, "D" },   /* LATIN SMALL LETTER ETH */
    { 0x00f8, 0, "we" },   /* LATIN SMALL LETTER O WITH STROKE */
    { 0x0131, 0, "X" },   /* LATIN SMALL LETTER DOTLESS I */
    { 0x014b, 0, "G" },   /* LATIN SMALL LETTER ENG */
    { 0x0153, 0, "wE" },   /* LATIN SMALL LIGATURE OE */
    { 0x0250, 0, "a" },   /* LATIN SMALL LETTER TURNED A */
    { 0x0251, 0, "a" },   /* LATIN SMALL LETTER ALPHA */
    { 0x0252, 0, "c" },   /* LATIN SMALL LETTER TURNED ALPHA */
    { 0x0254, 0, "c" },   /* LATIN SMALL LETTER OPEN O */
    { 0x0255, 0, "s" },   /* LATIN SMALL LETTER C WITH CURL */
    { 0x0258, 0, "x" },   /* LATIN SMALL LETTER REVERSED E */
    { 0x0259, 0, "x" },   /* LATIN SMALL LETTER SCHWA */
    { 0x025a, 0, "R" },   /* LATIN SMALL LETTER SCHWA WITH HOOK */
    { 0x025b, 0, "E" },   /* LATIN SMALL LETTER OPEN E */
    { 0x025c, 0, "R" },   /* LATIN SMALL LETTER REVERSED OPEN E */
    { 0x025d, 0, "R" },   /* LATIN SMALL LETTER REVERSED OPEN E WITH HOOK */
    { 0x025e, 0, "x" },   /* LATIN SMALL LETTER CLOSED REVERSED OPEN E */
    { 0x025f, 0, "g" },   /* LATIN SMALL LETTER DOTLESS J WITH STROKE */
    { 0x0261, 0, "g" },   /* LATIN SMALL LETTER SCRIPT G */
    { 0x0263, 0, "g" },   /* LATIN SMALL LETTER GAMMA */
    { 0x0264, 0, "o" },   /* LATIN SMALL LETTER RAMS HORN */
    { 0x0266, 0, "h" },   /* LATIN SMALL LETTER H WITH HOOK */
    { 0x0268, 0, "X" },   /* LATIN SMALL LETTER I WITH STROKE */
    { 0x0269, 0, "I" },   /* LATIN SMALL LETTER IOTA */
    { 0x026a, 0, "I" },   /* LATIN LETTER SMALL CAPITAL I */
    { 0x026f, 0, "U" },   /* LATIN SMALL LETTER TURNED M */
    { 0x0271, 0, "m" },   /* LATIN SMALL LETTER M WITH HOOK */
    { 0x0272, 0, "ny" },   /* LATIN SMALL LETTER N WITH LEFT HOOK */
    { 0x0275, 0, "x" },   /* LATIN SMALL LETTER BARRED O */
    { 0x0276, 0, "wA" },   /* LATIN LETTER SMALL CAPITAL OE */
    { 0x0277, 0, "U" },   /* LATIN SMALL LETTER CLOSED OMEGA */
    { 0x0279, 0, "r" },   /* LATIN SMALL LETTER TURNED R */
    { 0x027e, 0, "F" },   /* LATIN SMALL LETTER R WITH FISHHOOK */
    { 0x0280, 0, "r" },   /* LATIN LETTER SMALL CAPITAL R */
    { 0x0281, 0, "r" },   /* LATIN LETTER SMALL CAPITAL INVERTED R */
    { 0x0282, 0, "rs" },   /* LATIN SMALL LETTER S WITH HOOK */
    { 0x0283, 0, "S" },   /* LATIN SMALL LETTER ESH */
    { 0x0289, 0, "u" },   /* LATIN SMALL LETTER U BAR */
    { 0x028a, 0, "U" },   /* LATIN SMALL LETTER UPSILON */
    { 0x028b, 0, "w" },   /* LATIN SMALL LETTER V WITH HOOK */
    { 0x028c, 0, "H" },   /* LATIN SMALL LETTER TURNED V */
    { 0x028e, 0, "y" },   /* LATIN SMALL LETTER TURNED Y */
    { 0x028f, 0, "wI" },   /* LATIN LETTER SMALL CAPITAL Y */
    { 0x0290, 0, "rz" },   /* LATIN SMALL LETTER Z WITH RETROFLEX HOOK */
    { 0x0292, 0, "Z" },   /* LATIN SMALL LETTER EZH */
    { 0x0294, 0, "?" },   /* LATIN LETTER GLOTTAL STOP */
    { 0x029a, 0, "x" },   /* LATIN SMALL LETTER CLOSED OPEN E */
    { 0x029d, 0, "y" },   /* LATIN SMALL LETTER J WITH CROSSED-TAIL */
    { 0x02a4, 0, "J" },   /* LATIN SMALL LETTER DEZH DIGRAPH */
    { 0x02a6, 0, "ts" },   /* LATIN SMALL LETTER TS DIGRAPH */
    { 0x02a7, 0, "C" },   /* LATIN SMALL LETTER TESH DIGRAPH */
    { 0x02b2, 0, "y" },   /* MODIFIER LETTER SMALL J */
    { 0x02b7, 0, "w" },   /* MODIFIER LETTER SMALL W */
    { 0x02c8, 0, "1" },   /* MODIFIER LETTER VERTICAL LINE */
    { 0x02cc, 0, "2" },   /* MODIFIER LETTER LOW VERTICAL LINE */
    { 0x032b, 0, "b" },   /* COMBINING INVERTED DOUBLE ARCH BELOW */
    { 0x03b8, 0, "T" },   /* GREEK SMALL LETTER THETA */
    { 0x03c7, 0, "h" },   /* GREEK SMALL LETTER CHI */
    { 0x2016, 0, "`p150" },   /* DOUBLE VERTICAL LINE */
};
static const IpaTwo usen_two[] = {
    { 0x0061, 0x026a, 1, "Y" },   /* LATIN SMALL LETTER A + LATIN LETTER SMALL CAPITAL I */
    { 0x0061, 0x028a, 1, "W" },   /* LATIN SMALL LETTER A + LATIN SMALL LETTER UPSILON */
    { 0x0061, 0x02d0, 1, "a" },   /* LATIN SMALL LETTER A + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0064, 0x0292, 1, "J" },   /* LATIN SMALL LETTER D + LATIN SMALL LETTER EZH */
    { 0x0065, 0x0069, 1, "e" },   /* LATIN SMALL LETTER E + LATIN SMALL LETTER I */
    { 0x0065, 0x026a, 1, "e" },   /* LATIN SMALL LETTER E + LATIN LETTER SMALL CAPITAL I */
    { 0x0069, 0x0065, 1, "Y" },   /* LATIN SMALL LETTER I + LATIN SMALL LETTER E */
    { 0x0069, 0x02d0, 1, "i" },   /* LATIN SMALL LETTER I + MODIFIER LETTER TRIANGULAR COLON */
    { 0x006c, 0x0329, 1, "xl" },   /* LATIN SMALL LETTER L + COMBINING VERTICAL LINE BELOW */
    { 0x006d, 0x0329, 1, "M" },   /* LATIN SMALL LETTER M + COMBINING VERTICAL LINE BELOW */
    { 0x006e, 0x0329, 1, "N" },   /* LATIN SMALL LETTER N + COMBINING VERTICAL LINE BELOW */
    { 0x006f, 0x0069, 1, "O" },   /* LATIN SMALL LETTER O + LATIN SMALL LETTER I */
    { 0x006f, 0x0075, 1, "W" },   /* LATIN SMALL LETTER O + LATIN SMALL LETTER U */
    { 0x006f, 0x028a, 1, "o" },   /* LATIN SMALL LETTER O + LATIN SMALL LETTER UPSILON */
    { 0x0074, 0x0283, 1, "C" },   /* LATIN SMALL LETTER T + LATIN SMALL LETTER ESH */
    { 0x0075, 0x02d0, 1, "u" },   /* LATIN SMALL LETTER U + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0251, 0x02d0, 1, "a" },   /* LATIN SMALL LETTER ALPHA + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0254, 0x026a, 1, "O" },   /* LATIN SMALL LETTER OPEN O + LATIN LETTER SMALL CAPITAL I */
    { 0x0254, 0x02d0, 1, "c" },   /* LATIN SMALL LETTER OPEN O + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0259, 0x02de, 1, "R" },   /* LATIN SMALL LETTER SCHWA + MODIFIER LETTER RHOTIC HOOK */
    { 0x025c, 0x02d0, 1, "R" },   /* LATIN SMALL LETTER REVERSED OPEN E + MODIFIER LETTER TRIANGULAR COLON */
    { 0x025c, 0x02de, 1, "R" },   /* LATIN SMALL LETTER REVERSED OPEN E + MODIFIER LETTER RHOTIC HOOK */
};

/* UkEn: 91 single, 21 paired */
static const IpaOne uken_one[] = {
    { 0x002e, 0, "." },   /* FULL STOP */
    { 0x0061, 0, "a" },   /* LATIN SMALL LETTER A */
    { 0x0062, 0, "b" },   /* LATIN SMALL LETTER B */
    { 0x0063, 0, "k" },   /* LATIN SMALL LETTER C */
    { 0x0064, 0, "d" },   /* LATIN SMALL LETTER D */
    { 0x0065, 0, "E" },   /* LATIN SMALL LETTER E */
    { 0x0066, 0, "f" },   /* LATIN SMALL LETTER F */
    { 0x0067, 0, "g" },   /* LATIN SMALL LETTER G */
    { 0x0068, 0, "h" },   /* LATIN SMALL LETTER H */
    { 0x0069, 0, "X" },   /* LATIN SMALL LETTER I */
    { 0x006a, 0, "y" },   /* LATIN SMALL LETTER J */
    { 0x006b, 0, "k" },   /* LATIN SMALL LETTER K */
    { 0x006c, 0, "l" },   /* LATIN SMALL LETTER L */
    { 0x006d, 0, "m" },   /* LATIN SMALL LETTER M */
    { 0x006e, 0, "n" },   /* LATIN SMALL LETTER N */
    { 0x006f, 0, "o" },   /* LATIN SMALL LETTER O */
    { 0x0070, 0, "p" },   /* LATIN SMALL LETTER P */
    { 0x0072, 0, "r" },   /* LATIN SMALL LETTER R */
    { 0x0073, 0, "s" },   /* LATIN SMALL LETTER S */
    { 0x0074, 0, "t" },   /* LATIN SMALL LETTER T */
    { 0x0075, 0, "u" },   /* LATIN SMALL LETTER U */
    { 0x0076, 0, "v" },   /* LATIN SMALL LETTER V */
    { 0x0077, 0, "w" },   /* LATIN SMALL LETTER W */
    { 0x0078, 0, "h" },   /* LATIN SMALL LETTER X */
    { 0x0079, 0, "wi" },   /* LATIN SMALL LETTER Y */
    { 0x007a, 0, "z" },   /* LATIN SMALL LETTER Z */
    { 0x00e6, 0, "A" },   /* LATIN SMALL LETTER AE */
    { 0x00f0, 0, "D" },   /* LATIN SMALL LETTER ETH */
    { 0x00f8, 0, "we" },   /* LATIN SMALL LETTER O WITH STROKE */
    { 0x0131, 0, "X" },   /* LATIN SMALL LETTER DOTLESS I */
    { 0x014b, 0, "G" },   /* LATIN SMALL LETTER ENG */
    { 0x0153, 0, "wE" },   /* LATIN SMALL LIGATURE OE */
    { 0x01c0, 0, "t" },   /* LATIN LETTER DENTAL CLICK */
    { 0x01c1, 0, "t" },   /* LATIN LETTER LATERAL CLICK */
    { 0x01c3, 0, "t" },   /* LATIN LETTER RETROFLEX CLICK */
    { 0x0250, 0, "a" },   /* LATIN SMALL LETTER TURNED A */
    { 0x0251, 0, "a" },   /* LATIN SMALL LETTER ALPHA */
    { 0x0252, 0, "@" },   /* LATIN SMALL LETTER TURNED ALPHA */
    { 0x0254, 0, "c" },   /* LATIN SMALL LETTER OPEN O */
    { 0x0255, 0, "s" },   /* LATIN SMALL LETTER C WITH CURL */
    { 0x0258, 0, "x" },   /* LATIN SMALL LETTER REVERSED E */
    { 0x0259, 0, "x" },   /* LATIN SMALL LETTER SCHWA */
    { 0x025a, 0, "R" },   /* LATIN SMALL LETTER SCHWA WITH HOOK */
    { 0x025b, 0, "E" },   /* LATIN SMALL LETTER OPEN E */
    { 0x025c, 0, "R" },   /* LATIN SMALL LETTER REVERSED OPEN E */
    { 0x025d, 0, "R" },   /* LATIN SMALL LETTER REVERSED OPEN E WITH HOOK */
    { 0x025e, 0, "R" },   /* LATIN SMALL LETTER CLOSED REVERSED OPEN E */
    { 0x025f, 0, "g" },   /* LATIN SMALL LETTER DOTLESS J WITH STROKE */
    { 0x0261, 0, "g" },   /* LATIN SMALL LETTER SCRIPT G */
    { 0x0263, 0, "g" },   /* LATIN SMALL LETTER GAMMA */
    { 0x0264, 0, "o" },   /* LATIN SMALL LETTER RAMS HORN */
    { 0x0266, 0, "h" },   /* LATIN SMALL LETTER H WITH HOOK */
    { 0x0268, 0, "X" },   /* LATIN SMALL LETTER I WITH STROKE */
    { 0x0269, 0, "I" },   /* LATIN SMALL LETTER IOTA */
    { 0x026a, 0, "I" },   /* LATIN LETTER SMALL CAPITAL I */
    { 0x026c, 0, "sl" },   /* LATIN SMALL LETTER L WITH BELT */
    { 0x026e, 0, "zl" },   /* LATIN SMALL LETTER LEZH */
    { 0x026f, 0, "U" },   /* LATIN SMALL LETTER TURNED M */
    { 0x0271, 0, "m" },   /* LATIN SMALL LETTER M WITH HOOK */
    { 0x0272, 0, "ny" },   /* LATIN SMALL LETTER N WITH LEFT HOOK */
    { 0x0275, 0, "x" },   /* LATIN SMALL LETTER BARRED O */
    { 0x0276, 0, "wA" },   /* LATIN LETTER SMALL CAPITAL OE */
    { 0x0277, 0, "U" },   /* LATIN SMALL LETTER CLOSED OMEGA */
    { 0x0279, 0, "r" },   /* LATIN SMALL LETTER TURNED R */
    { 0x027e, 0, "r" },   /* LATIN SMALL LETTER R WITH FISHHOOK */
    { 0x0280, 0, "r" },   /* LATIN LETTER SMALL CAPITAL R */
    { 0x0281, 0, "r" },   /* LATIN LETTER SMALL CAPITAL INVERTED R */
    { 0x0282, 0, "rs" },   /* LATIN SMALL LETTER S WITH HOOK */
    { 0x0283, 0, "S" },   /* LATIN SMALL LETTER ESH */
    { 0x0289, 0, "u" },   /* LATIN SMALL LETTER U BAR */
    { 0x028a, 0, "U" },   /* LATIN SMALL LETTER UPSILON */
    { 0x028b, 0, "w" },   /* LATIN SMALL LETTER V WITH HOOK */
    { 0x028c, 0, "H" },   /* LATIN SMALL LETTER TURNED V */
    { 0x028e, 0, "y" },   /* LATIN SMALL LETTER TURNED Y */
    { 0x028f, 0, "wI" },   /* LATIN LETTER SMALL CAPITAL Y */
    { 0x0290, 0, "rz" },   /* LATIN SMALL LETTER Z WITH RETROFLEX HOOK */
    { 0x0292, 0, "Z" },   /* LATIN SMALL LETTER EZH */
    { 0x0294, 0, "?" },   /* LATIN LETTER GLOTTAL STOP */
    { 0x029a, 0, "R" },   /* LATIN SMALL LETTER CLOSED OPEN E */
    { 0x029d, 0, "y" },   /* LATIN SMALL LETTER J WITH CROSSED-TAIL */
    { 0x02a4, 0, "J" },   /* LATIN SMALL LETTER DEZH DIGRAPH */
    { 0x02a6, 0, "ts" },   /* LATIN SMALL LETTER TS DIGRAPH */
    { 0x02a7, 0, "C" },   /* LATIN SMALL LETTER TESH DIGRAPH */
    { 0x02b2, 0, "y" },   /* MODIFIER LETTER SMALL J */
    { 0x02b7, 0, "w" },   /* MODIFIER LETTER SMALL W */
    { 0x02c8, 0, "1" },   /* MODIFIER LETTER VERTICAL LINE */
    { 0x02cc, 0, "2" },   /* MODIFIER LETTER LOW VERTICAL LINE */
    { 0x032b, 0, "b" },   /* COMBINING INVERTED DOUBLE ARCH BELOW */
    { 0x03b8, 0, "T" },   /* GREEK SMALL LETTER THETA */
    { 0x03c7, 0, "h" },   /* GREEK SMALL LETTER CHI */
    { 0x2016, 0, "`p150" },   /* DOUBLE VERTICAL LINE */
};
static const IpaTwo uken_two[] = {
    { 0x0061, 0x026a, 1, "Y" },   /* LATIN SMALL LETTER A + LATIN LETTER SMALL CAPITAL I */
    { 0x0061, 0x028a, 1, "W" },   /* LATIN SMALL LETTER A + LATIN SMALL LETTER UPSILON */
    { 0x0061, 0x02d0, 1, "a" },   /* LATIN SMALL LETTER A + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0064, 0x0292, 1, "J" },   /* LATIN SMALL LETTER D + LATIN SMALL LETTER EZH */
    { 0x0065, 0x0069, 1, "e" },   /* LATIN SMALL LETTER E + LATIN SMALL LETTER I */
    { 0x0065, 0x026a, 1, "e" },   /* LATIN SMALL LETTER E + LATIN LETTER SMALL CAPITAL I */
    { 0x0069, 0x0065, 1, "Y" },   /* LATIN SMALL LETTER I + LATIN SMALL LETTER E */
    { 0x0069, 0x02d0, 1, "i" },   /* LATIN SMALL LETTER I + MODIFIER LETTER TRIANGULAR COLON */
    { 0x006c, 0x0329, 1, "L" },   /* LATIN SMALL LETTER L + COMBINING VERTICAL LINE BELOW */
    { 0x006d, 0x0329, 1, "xm" },   /* LATIN SMALL LETTER M + COMBINING VERTICAL LINE BELOW */
    { 0x006e, 0x0329, 1, "xn" },   /* LATIN SMALL LETTER N + COMBINING VERTICAL LINE BELOW */
    { 0x006f, 0x0069, 1, "O" },   /* LATIN SMALL LETTER O + LATIN SMALL LETTER I */
    { 0x006f, 0x0075, 1, "W" },   /* LATIN SMALL LETTER O + LATIN SMALL LETTER U */
    { 0x006f, 0x028a, 1, "o" },   /* LATIN SMALL LETTER O + LATIN SMALL LETTER UPSILON */
    { 0x0074, 0x0283, 1, "C" },   /* LATIN SMALL LETTER T + LATIN SMALL LETTER ESH */
    { 0x0075, 0x02d0, 1, "u" },   /* LATIN SMALL LETTER U + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0251, 0x02d0, 1, "a" },   /* LATIN SMALL LETTER ALPHA + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0254, 0x026a, 1, "O" },   /* LATIN SMALL LETTER OPEN O + LATIN LETTER SMALL CAPITAL I */
    { 0x0254, 0x02d0, 1, "c" },   /* LATIN SMALL LETTER OPEN O + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0259, 0x028a, 1, "o" },   /* LATIN SMALL LETTER SCHWA + LATIN SMALL LETTER UPSILON */
    { 0x025c, 0x02d0, 1, "R" },   /* LATIN SMALL LETTER REVERSED OPEN E + MODIFIER LETTER TRIANGULAR COLON */
};

/* GrGr: 86 single, 31 paired */
static const IpaOne grgr_one[] = {
    { 0x002e, 0, "." },   /* FULL STOP */
    { 0x0061, 0, "A" },   /* LATIN SMALL LETTER A */
    { 0x0062, 0, "b" },   /* LATIN SMALL LETTER B */
    { 0x0063, 0, "k" },   /* LATIN SMALL LETTER C */
    { 0x0064, 0, "d" },   /* LATIN SMALL LETTER D */
    { 0x0065, 0, "e" },   /* LATIN SMALL LETTER E */
    { 0x0066, 0, "f" },   /* LATIN SMALL LETTER F */
    { 0x0067, 0, "g" },   /* LATIN SMALL LETTER G */
    { 0x0068, 0, "h" },   /* LATIN SMALL LETTER H */
    { 0x0069, 0, "i" },   /* LATIN SMALL LETTER I */
    { 0x006a, 0, "j" },   /* LATIN SMALL LETTER J */
    { 0x006b, 0, "k" },   /* LATIN SMALL LETTER K */
    { 0x006c, 0, "l" },   /* LATIN SMALL LETTER L */
    { 0x006d, 0, "m" },   /* LATIN SMALL LETTER M */
    { 0x006e, 0, "n" },   /* LATIN SMALL LETTER N */
    { 0x006f, 0, "o" },   /* LATIN SMALL LETTER O */
    { 0x0070, 0, "p" },   /* LATIN SMALL LETTER P */
    { 0x0072, 0, "r" },   /* LATIN SMALL LETTER R */
    { 0x0073, 0, "s" },   /* LATIN SMALL LETTER S */
    { 0x0074, 0, "t" },   /* LATIN SMALL LETTER T */
    { 0x0075, 0, "u" },   /* LATIN SMALL LETTER U */
    { 0x0076, 0, "v" },   /* LATIN SMALL LETTER V */
    { 0x0077, 0, "w" },   /* LATIN SMALL LETTER W */
    { 0x0078, 0, "x" },   /* LATIN SMALL LETTER X */
    { 0x0079, 0, "y" },   /* LATIN SMALL LETTER Y */
    { 0x007a, 0, "z" },   /* LATIN SMALL LETTER Z */
    { 0x00e6, 0, "E" },   /* LATIN SMALL LETTER AE */
    { 0x00e7, 0, "X" },   /* LATIN SMALL LETTER C WITH CEDILLA */
    { 0x00f0, 0, "d" },   /* LATIN SMALL LETTER ETH */
    { 0x00f8, 0, "'oe'" },   /* LATIN SMALL LETTER O WITH STROKE */
    { 0x0131, 0, "i" },   /* LATIN SMALL LETTER DOTLESS I */
    { 0x014b, 0, "G" },   /* LATIN SMALL LETTER ENG */
    { 0x0153, 0, "'OE'" },   /* LATIN SMALL LIGATURE OE */
    { 0x0250, 0, "R" },   /* LATIN SMALL LETTER TURNED A */
    { 0x0251, 0, "A" },   /* LATIN SMALL LETTER ALPHA */
    { 0x0252, 0, "O" },   /* LATIN SMALL LETTER TURNED ALPHA */
    { 0x0254, 0, "O" },   /* LATIN SMALL LETTER OPEN O */
    { 0x0255, 0, "s" },   /* LATIN SMALL LETTER C WITH CURL */
    { 0x0258, 0, "@" },   /* LATIN SMALL LETTER REVERSED E */
    { 0x0259, 0, "@" },   /* LATIN SMALL LETTER SCHWA */
    { 0x025a, 0, "R" },   /* LATIN SMALL LETTER SCHWA WITH HOOK */
    { 0x025b, 0, "E" },   /* LATIN SMALL LETTER OPEN E */
    { 0x025c, 0, "E" },   /* LATIN SMALL LETTER REVERSED OPEN E */
    { 0x025d, 0, "R" },   /* LATIN SMALL LETTER REVERSED OPEN E WITH HOOK */
    { 0x025e, 0, "R" },   /* LATIN SMALL LETTER CLOSED REVERSED OPEN E */
    { 0x025f, 0, "g" },   /* LATIN SMALL LETTER DOTLESS J WITH STROKE */
    { 0x0261, 0, "g" },   /* LATIN SMALL LETTER SCRIPT G */
    { 0x0263, 0, "g" },   /* LATIN SMALL LETTER GAMMA */
    { 0x0264, 0, "o" },   /* LATIN SMALL LETTER RAMS HORN */
    { 0x0266, 0, "h" },   /* LATIN SMALL LETTER H WITH HOOK */
    { 0x0268, 0, "i" },   /* LATIN SMALL LETTER I WITH STROKE */
    { 0x0269, 0, "I" },   /* LATIN SMALL LETTER IOTA */
    { 0x026a, 0, "I" },   /* LATIN LETTER SMALL CAPITAL I */
    { 0x026f, 0, "U" },   /* LATIN SMALL LETTER TURNED M */
    { 0x0271, 0, "m" },   /* LATIN SMALL LETTER M WITH HOOK */
    { 0x0272, 0, "nj" },   /* LATIN SMALL LETTER N WITH LEFT HOOK */
    { 0x0275, 0, "@" },   /* LATIN SMALL LETTER BARRED O */
    { 0x0276, 0, "'OE'" },   /* LATIN LETTER SMALL CAPITAL OE */
    { 0x0277, 0, "U" },   /* LATIN SMALL LETTER CLOSED OMEGA */
    { 0x0279, 0, "r" },   /* LATIN SMALL LETTER TURNED R */
    { 0x027e, 0, "r" },   /* LATIN SMALL LETTER R WITH FISHHOOK */
    { 0x0280, 0, "r" },   /* LATIN LETTER SMALL CAPITAL R */
    { 0x0281, 0, "r" },   /* LATIN LETTER SMALL CAPITAL INVERTED R */
    { 0x0282, 0, "rs" },   /* LATIN SMALL LETTER S WITH HOOK */
    { 0x0283, 0, "S" },   /* LATIN SMALL LETTER ESH */
    { 0x0289, 0, "u" },   /* LATIN SMALL LETTER U BAR */
    { 0x028a, 0, "U" },   /* LATIN SMALL LETTER UPSILON */
    { 0x028b, 0, "w" },   /* LATIN SMALL LETTER V WITH HOOK */
    { 0x028c, 0, "R" },   /* LATIN SMALL LETTER TURNED V */
    { 0x028e, 0, "j" },   /* LATIN SMALL LETTER TURNED Y */
    { 0x028f, 0, "Y" },   /* LATIN LETTER SMALL CAPITAL Y */
    { 0x0290, 0, "rz" },   /* LATIN SMALL LETTER Z WITH RETROFLEX HOOK */
    { 0x0292, 0, "Z" },   /* LATIN SMALL LETTER EZH */
    { 0x029a, 0, "R" },   /* LATIN SMALL LETTER CLOSED OPEN E */
    { 0x029d, 0, "j" },   /* LATIN SMALL LETTER J WITH CROSSED-TAIL */
    { 0x02a4, 0, "J" },   /* LATIN SMALL LETTER DEZH DIGRAPH */
    { 0x02a6, 0, "T" },   /* LATIN SMALL LETTER TS DIGRAPH */
    { 0x02a7, 0, "C" },   /* LATIN SMALL LETTER TESH DIGRAPH */
    { 0x02b2, 0, "j" },   /* MODIFIER LETTER SMALL J */
    { 0x02b7, 0, "w" },   /* MODIFIER LETTER SMALL W */
    { 0x02c8, 0, "1" },   /* MODIFIER LETTER VERTICAL LINE */
    { 0x02cc, 0, "2" },   /* MODIFIER LETTER LOW VERTICAL LINE */
    { 0x032b, 0, "b" },   /* COMBINING INVERTED DOUBLE ARCH BELOW */
    { 0x03b8, 0, "T" },   /* GREEK SMALL LETTER THETA */
    { 0x03c7, 0, "x" },   /* GREEK SMALL LETTER CHI */
    { 0x2016, 0, "`p150" },   /* DOUBLE VERTICAL LINE */
};
static const IpaTwo grgr_two[] = {
    { 0x0061, 0x0069, 1, "'aj'" },   /* LATIN SMALL LETTER A + LATIN SMALL LETTER I */
    { 0x0061, 0x0075, 1, "'aw'" },   /* LATIN SMALL LETTER A + LATIN SMALL LETTER U */
    { 0x0061, 0x026a, 1, "'aj'" },   /* LATIN SMALL LETTER A + LATIN LETTER SMALL CAPITAL I */
    { 0x0061, 0x028a, 1, "'aw'" },   /* LATIN SMALL LETTER A + LATIN SMALL LETTER UPSILON */
    { 0x0061, 0x02d0, 1, "a" },   /* LATIN SMALL LETTER A + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0061, 0x0303, 1, "'a~'" },   /* LATIN SMALL LETTER A + COMBINING TILDE */
    { 0x0064, 0x0292, 1, "J" },   /* LATIN SMALL LETTER D + LATIN SMALL LETTER EZH */
    { 0x0065, 0x02d0, 1, "e" },   /* LATIN SMALL LETTER E + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0065, 0x0303, 1, "'E~'" },   /* LATIN SMALL LETTER E + COMBINING TILDE */
    { 0x0069, 0x02d0, 1, "i" },   /* LATIN SMALL LETTER I + MODIFIER LETTER TRIANGULAR COLON */
    { 0x006c, 0x0329, 1, "@l" },   /* LATIN SMALL LETTER L + COMBINING VERTICAL LINE BELOW */
    { 0x006d, 0x0329, 1, "@m" },   /* LATIN SMALL LETTER M + COMBINING VERTICAL LINE BELOW */
    { 0x006e, 0x0329, 1, "@n" },   /* LATIN SMALL LETTER N + COMBINING VERTICAL LINE BELOW */
    { 0x006f, 0x02d0, 1, "o" },   /* LATIN SMALL LETTER O + MODIFIER LETTER TRIANGULAR COLON */
    { 0x006f, 0x0303, 1, "'o~'" },   /* LATIN SMALL LETTER O + COMBINING TILDE */
    { 0x0070, 0x0066, 1, "p" },   /* LATIN SMALL LETTER P + LATIN SMALL LETTER F */
    { 0x0074, 0x0073, 1, "T" },   /* LATIN SMALL LETTER T + LATIN SMALL LETTER S */
    { 0x0074, 0x0283, 1, "C" },   /* LATIN SMALL LETTER T + LATIN SMALL LETTER ESH */
    { 0x0075, 0x02d0, 1, "u" },   /* LATIN SMALL LETTER U + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0079, 0x02d0, 1, "y" },   /* LATIN SMALL LETTER Y + MODIFIER LETTER TRIANGULAR COLON */
    { 0x00f8, 0x02d0, 1, "'oe'" },   /* LATIN SMALL LETTER O WITH STROKE + MODIFIER LETTER TRIANGULAR COLON */
    { 0x00f8, 0x0303, 1, "'oe~'" },   /* LATIN SMALL LETTER O WITH STROKE + COMBINING TILDE */
    { 0x0153, 0x0303, 1, "'oe~'" },   /* LATIN SMALL LIGATURE OE + COMBINING TILDE */
    { 0x0250, 0x0303, 1, "'a~'" },   /* LATIN SMALL LETTER TURNED A + COMBINING TILDE */
    { 0x0254, 0x0069, 1, "'oj'" },   /* LATIN SMALL LETTER OPEN O + LATIN SMALL LETTER I */
    { 0x0254, 0x0079, 1, "'oj'" },   /* LATIN SMALL LETTER OPEN O + LATIN SMALL LETTER Y */
    { 0x0254, 0x026a, 1, "'oj'" },   /* LATIN SMALL LETTER OPEN O + LATIN LETTER SMALL CAPITAL I */
    { 0x0254, 0x028f, 1, "'oj'" },   /* LATIN SMALL LETTER OPEN O + LATIN LETTER SMALL CAPITAL Y */
    { 0x0254, 0x0303, 1, "'o~'" },   /* LATIN SMALL LETTER OPEN O + COMBINING TILDE */
    { 0x025b, 0x02d0, 1, "'E:'" },   /* LATIN SMALL LETTER OPEN E + MODIFIER LETTER TRIANGULAR COLON */
    { 0x025b, 0x0303, 1, "'E~'" },   /* LATIN SMALL LETTER OPEN E + COMBINING TILDE */
};

/* FrFr: 82 single, 7 paired */
static const IpaOne frfr_one[] = {
    { 0x002e, 0, "." },   /* FULL STOP */
    { 0x0061, 0, "a" },   /* LATIN SMALL LETTER A */
    { 0x0062, 0, "b" },   /* LATIN SMALL LETTER B */
    { 0x0063, 0, "k" },   /* LATIN SMALL LETTER C */
    { 0x0064, 0, "d" },   /* LATIN SMALL LETTER D */
    { 0x0065, 0, "e" },   /* LATIN SMALL LETTER E */
    { 0x0066, 0, "f" },   /* LATIN SMALL LETTER F */
    { 0x0067, 0, "g" },   /* LATIN SMALL LETTER G */
    { 0x0069, 0, "i" },   /* LATIN SMALL LETTER I */
    { 0x006a, 0, "j" },   /* LATIN SMALL LETTER J */
    { 0x006b, 0, "k" },   /* LATIN SMALL LETTER K */
    { 0x006c, 0, "l" },   /* LATIN SMALL LETTER L */
    { 0x006d, 0, "m" },   /* LATIN SMALL LETTER M */
    { 0x006e, 0, "n" },   /* LATIN SMALL LETTER N */
    { 0x006f, 0, "o" },   /* LATIN SMALL LETTER O */
    { 0x0070, 0, "p" },   /* LATIN SMALL LETTER P */
    { 0x0072, 0, "r" },   /* LATIN SMALL LETTER R */
    { 0x0073, 0, "s" },   /* LATIN SMALL LETTER S */
    { 0x0074, 0, "t" },   /* LATIN SMALL LETTER T */
    { 0x0075, 0, "u" },   /* LATIN SMALL LETTER U */
    { 0x0076, 0, "v" },   /* LATIN SMALL LETTER V */
    { 0x0077, 0, "w" },   /* LATIN SMALL LETTER W */
    { 0x0078, 0, "r" },   /* LATIN SMALL LETTER X */
    { 0x0079, 0, "y" },   /* LATIN SMALL LETTER Y */
    { 0x007a, 0, "z" },   /* LATIN SMALL LETTER Z */
    { 0x00e6, 0, "E" },   /* LATIN SMALL LETTER AE */
    { 0x00f0, 0, "d" },   /* LATIN SMALL LETTER ETH */
    { 0x00f8, 0, "'eu'" },   /* LATIN SMALL LETTER O WITH STROKE */
    { 0x0131, 0, "i" },   /* LATIN SMALL LETTER DOTLESS I */
    { 0x014b, 0, "ng" },   /* LATIN SMALL LETTER ENG */
    { 0x0153, 0, "'oe'" },   /* LATIN SMALL LIGATURE OE */
    { 0x0250, 0, "a" },   /* LATIN SMALL LETTER TURNED A */
    { 0x0251, 0, "a" },   /* LATIN SMALL LETTER ALPHA */
    { 0x0252, 0, "c" },   /* LATIN SMALL LETTER TURNED ALPHA */
    { 0x0254, 0, "c" },   /* LATIN SMALL LETTER OPEN O */
    { 0x0255, 0, "s" },   /* LATIN SMALL LETTER C WITH CURL */
    { 0x0258, 0, "e" },   /* LATIN SMALL LETTER REVERSED E */
    { 0x0259, 0, "x" },   /* LATIN SMALL LETTER SCHWA */
    { 0x025a, 0, "Er" },   /* LATIN SMALL LETTER SCHWA WITH HOOK */
    { 0x025b, 0, "E" },   /* LATIN SMALL LETTER OPEN E */
    { 0x025c, 0, "E" },   /* LATIN SMALL LETTER REVERSED OPEN E */
    { 0x025d, 0, "Er" },   /* LATIN SMALL LETTER REVERSED OPEN E WITH HOOK */
    { 0x025e, 0, "'oe'" },   /* LATIN SMALL LETTER CLOSED REVERSED OPEN E */
    { 0x025f, 0, "g" },   /* LATIN SMALL LETTER DOTLESS J WITH STROKE */
    { 0x0261, 0, "g" },   /* LATIN SMALL LETTER SCRIPT G */
    { 0x0263, 0, "g" },   /* LATIN SMALL LETTER GAMMA */
    { 0x0264, 0, "o" },   /* LATIN SMALL LETTER RAMS HORN */
    { 0x0265, 0, "H" },   /* LATIN SMALL LETTER TURNED H */
    { 0x0268, 0, "i" },   /* LATIN SMALL LETTER I WITH STROKE */
    { 0x0269, 0, "i" },   /* LATIN SMALL LETTER IOTA */
    { 0x026a, 0, "i" },   /* LATIN LETTER SMALL CAPITAL I */
    { 0x026f, 0, "u" },   /* LATIN SMALL LETTER TURNED M */
    { 0x0271, 0, "m" },   /* LATIN SMALL LETTER M WITH HOOK */
    { 0x0272, 0, "nj" },   /* LATIN SMALL LETTER N WITH LEFT HOOK */
    { 0x0275, 0, "'eu'" },   /* LATIN SMALL LETTER BARRED O */
    { 0x0276, 0, "'oe'" },   /* LATIN LETTER SMALL CAPITAL OE */
    { 0x0277, 0, "u" },   /* LATIN SMALL LETTER CLOSED OMEGA */
    { 0x0279, 0, "r" },   /* LATIN SMALL LETTER TURNED R */
    { 0x027e, 0, "r" },   /* LATIN SMALL LETTER R WITH FISHHOOK */
    { 0x0280, 0, "r" },   /* LATIN LETTER SMALL CAPITAL R */
    { 0x0281, 0, "r" },   /* LATIN LETTER SMALL CAPITAL INVERTED R */
    { 0x0282, 0, "rs" },   /* LATIN SMALL LETTER S WITH HOOK */
    { 0x0283, 0, "S" },   /* LATIN SMALL LETTER ESH */
    { 0x0289, 0, "u" },   /* LATIN SMALL LETTER U BAR */
    { 0x028a, 0, "u" },   /* LATIN SMALL LETTER UPSILON */
    { 0x028b, 0, "w" },   /* LATIN SMALL LETTER V WITH HOOK */
    { 0x028c, 0, "a" },   /* LATIN SMALL LETTER TURNED V */
    { 0x028e, 0, "j" },   /* LATIN SMALL LETTER TURNED Y */
    { 0x028f, 0, "y" },   /* LATIN LETTER SMALL CAPITAL Y */
    { 0x0290, 0, "rz" },   /* LATIN SMALL LETTER Z WITH RETROFLEX HOOK */
    { 0x0292, 0, "Z" },   /* LATIN SMALL LETTER EZH */
    { 0x029a, 0, "'oe'" },   /* LATIN SMALL LETTER CLOSED OPEN E */
    { 0x029d, 0, "j" },   /* LATIN SMALL LETTER J WITH CROSSED-TAIL */
    { 0x02a4, 0, "dZ" },   /* LATIN SMALL LETTER DEZH DIGRAPH */
    { 0x02a6, 0, "ts" },   /* LATIN SMALL LETTER TS DIGRAPH */
    { 0x02a7, 0, "tS" },   /* LATIN SMALL LETTER TESH DIGRAPH */
    { 0x02b2, 0, "j" },   /* MODIFIER LETTER SMALL J */
    { 0x02b7, 0, "w" },   /* MODIFIER LETTER SMALL W */
    { 0x032b, 0, "b" },   /* COMBINING INVERTED DOUBLE ARCH BELOW */
    { 0x03b8, 0, "s" },   /* GREEK SMALL LETTER THETA */
    { 0x03c7, 0, "r" },   /* GREEK SMALL LETTER CHI */
    { 0x2016, 0, "`p150" },   /* DOUBLE VERTICAL LINE */
};
static const IpaTwo frfr_two[] = {
    { 0x0061, 0x0303, 1, "'a~'" },   /* LATIN SMALL LETTER A + COMBINING TILDE */
    { 0x0065, 0x0303, 1, "'E~'" },   /* LATIN SMALL LETTER E + COMBINING TILDE */
    { 0x006f, 0x0303, 1, "'o~'" },   /* LATIN SMALL LETTER O + COMBINING TILDE */
    { 0x0153, 0x0303, 1, "'oe~'" },   /* LATIN SMALL LIGATURE OE + COMBINING TILDE */
    { 0x0251, 0x0303, 1, "'a~'" },   /* LATIN SMALL LETTER ALPHA + COMBINING TILDE */
    { 0x0254, 0x0303, 1, "'o~'" },   /* LATIN SMALL LETTER OPEN O + COMBINING TILDE */
    { 0x025b, 0x0303, 1, "'E~'" },   /* LATIN SMALL LETTER OPEN E + COMBINING TILDE */
};

/* JpJp: 69 single, 10 paired */
static const IpaOne jpjp_one[] = {
    { 0x002e, 0, "." },   /* FULL STOP */
    { 0x0061, 0, "a" },   /* LATIN SMALL LETTER A */
    { 0x0062, 0, "b" },   /* LATIN SMALL LETTER B */
    { 0x0064, 0, "d" },   /* LATIN SMALL LETTER D */
    { 0x0065, 0, "e" },   /* LATIN SMALL LETTER E */
    { 0x0066, 0, "f" },   /* LATIN SMALL LETTER F */
    { 0x0067, 0, "g" },   /* LATIN SMALL LETTER G */
    { 0x0068, 0, "h" },   /* LATIN SMALL LETTER H */
    { 0x0069, 0, "i" },   /* LATIN SMALL LETTER I */
    { 0x006a, 0, "y" },   /* LATIN SMALL LETTER J */
    { 0x006b, 0, "k" },   /* LATIN SMALL LETTER K */
    { 0x006c, 0, "r" },   /* LATIN SMALL LETTER L */
    { 0x006d, 0, "m" },   /* LATIN SMALL LETTER M */
    { 0x006e, 0, "n" },   /* LATIN SMALL LETTER N */
    { 0x006f, 0, "o" },   /* LATIN SMALL LETTER O */
    { 0x0070, 0, "p" },   /* LATIN SMALL LETTER P */
    { 0x0072, 0, "r" },   /* LATIN SMALL LETTER R */
    { 0x0073, 0, "s" },   /* LATIN SMALL LETTER S */
    { 0x0074, 0, "t" },   /* LATIN SMALL LETTER T */
    { 0x0075, 0, "u" },   /* LATIN SMALL LETTER U */
    { 0x0076, 0, "b" },   /* LATIN SMALL LETTER V */
    { 0x0077, 0, "w" },   /* LATIN SMALL LETTER W */
    { 0x0079, 0, "wi" },   /* LATIN SMALL LETTER Y */
    { 0x007a, 0, "z" },   /* LATIN SMALL LETTER Z */
    { 0x00e6, 0, "e" },   /* LATIN SMALL LETTER AE */
    { 0x00f8, 0, "we" },   /* LATIN SMALL LETTER O WITH STROKE */
    { 0x0131, 0, "i" },   /* LATIN SMALL LETTER DOTLESS I */
    { 0x014b, 0, "g" },   /* LATIN SMALL LETTER ENG */
    { 0x0153, 0, "we" },   /* LATIN SMALL LIGATURE OE */
    { 0x0250, 0, "a" },   /* LATIN SMALL LETTER TURNED A */
    { 0x0251, 0, "a" },   /* LATIN SMALL LETTER ALPHA */
    { 0x0252, 0, "o" },   /* LATIN SMALL LETTER TURNED ALPHA */
    { 0x0254, 0, "o" },   /* LATIN SMALL LETTER OPEN O */
    { 0x0258, 0, "e" },   /* LATIN SMALL LETTER REVERSED E */
    { 0x0259, 0, "e" },   /* LATIN SMALL LETTER SCHWA */
    { 0x025a, 0, "er" },   /* LATIN SMALL LETTER SCHWA WITH HOOK */
    { 0x025b, 0, "e" },   /* LATIN SMALL LETTER OPEN E */
    { 0x025c, 0, "a" },   /* LATIN SMALL LETTER REVERSED OPEN E */
    { 0x025d, 0, "er" },   /* LATIN SMALL LETTER REVERSED OPEN E WITH HOOK */
    { 0x025e, 0, "a" },   /* LATIN SMALL LETTER CLOSED REVERSED OPEN E */
    { 0x0261, 0, "g" },   /* LATIN SMALL LETTER SCRIPT G */
    { 0x0264, 0, "o" },   /* LATIN SMALL LETTER RAMS HORN */
    { 0x0268, 0, "i" },   /* LATIN SMALL LETTER I WITH STROKE */
    { 0x0269, 0, "i" },   /* LATIN SMALL LETTER IOTA */
    { 0x026a, 0, "i" },   /* LATIN LETTER SMALL CAPITAL I */
    { 0x026f, 0, "u" },   /* LATIN SMALL LETTER TURNED M */
    { 0x0272, 0, "ny" },   /* LATIN SMALL LETTER N WITH LEFT HOOK */
    { 0x0274, 0, "N" },   /* LATIN LETTER SMALL CAPITAL N */
    { 0x0275, 0, "e" },   /* LATIN SMALL LETTER BARRED O */
    { 0x0276, 0, "we" },   /* LATIN LETTER SMALL CAPITAL OE */
    { 0x0277, 0, "u" },   /* LATIN SMALL LETTER CLOSED OMEGA */
    { 0x0278, 0, "f" },   /* LATIN SMALL LETTER PHI */
    { 0x0279, 0, "r" },   /* LATIN SMALL LETTER TURNED R */
    { 0x027e, 0, "r" },   /* LATIN SMALL LETTER R WITH FISHHOOK */
    { 0x0280, 0, "r" },   /* LATIN LETTER SMALL CAPITAL R */
    { 0x0281, 0, "r" },   /* LATIN LETTER SMALL CAPITAL INVERTED R */
    { 0x0283, 0, "S" },   /* LATIN SMALL LETTER ESH */
    { 0x0289, 0, "u" },   /* LATIN SMALL LETTER U BAR */
    { 0x028a, 0, "u" },   /* LATIN SMALL LETTER UPSILON */
    { 0x028c, 0, "a" },   /* LATIN SMALL LETTER TURNED V */
    { 0x028f, 0, "wi" },   /* LATIN LETTER SMALL CAPITAL Y */
    { 0x029a, 0, "a" },   /* LATIN SMALL LETTER CLOSED OPEN E */
    { 0x02a4, 0, "'dZ'" },   /* LATIN SMALL LETTER DEZH DIGRAPH */
    { 0x02a6, 0, "'ts'" },   /* LATIN SMALL LETTER TS DIGRAPH */
    { 0x02a7, 0, "'tS'" },   /* LATIN SMALL LETTER TESH DIGRAPH */
    { 0x02c8, 0, "1" },   /* MODIFIER LETTER VERTICAL LINE */
    { 0x02cc, 0, "0" },   /* MODIFIER LETTER LOW VERTICAL LINE */
    { 0x03b2, 0, "b" },   /* GREEK SMALL LETTER BETA */
    { 0x2016, 0, "`p150" },   /* DOUBLE VERTICAL LINE */
};
static const IpaTwo jpjp_two[] = {
    { 0x0061, 0x02d0, 1, "A" },   /* LATIN SMALL LETTER A + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0064, 0x0292, 1, "'dZ'" },   /* LATIN SMALL LETTER D + LATIN SMALL LETTER EZH */
    { 0x0065, 0x02d0, 1, "E" },   /* LATIN SMALL LETTER E + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0069, 0x02d0, 1, "I" },   /* LATIN SMALL LETTER I + MODIFIER LETTER TRIANGULAR COLON */
    { 0x006b, 0x02b7, 1, "ku" },   /* LATIN SMALL LETTER K + MODIFIER LETTER SMALL W */
    { 0x006f, 0x02d0, 1, "O" },   /* LATIN SMALL LETTER O + MODIFIER LETTER TRIANGULAR COLON */
    { 0x0074, 0x0073, 1, "'ts'" },   /* LATIN SMALL LETTER T + LATIN SMALL LETTER S */
    { 0x0074, 0x0283, 1, "'tS'" },   /* LATIN SMALL LETTER T + LATIN SMALL LETTER ESH */
    { 0x0075, 0x02d0, 1, "U" },   /* LATIN SMALL LETTER U + MODIFIER LETTER TRIANGULAR COLON */
    { 0x026f, 0x02d0, 1, "U" },   /* LATIN SMALL LETTER TURNED M + MODIFIER LETTER TRIANGULAR COLON */
};

/* KoKo: 70 single, 21 paired */
static const IpaOne koko_one[] = {
    { 0x002e, 0, "." },   /* FULL STOP */
    { 0x0061, 0, "a" },   /* LATIN SMALL LETTER A */
    { 0x0062, 0, "p" },   /* LATIN SMALL LETTER B */
    { 0x0063, 0, "cc" },   /* LATIN SMALL LETTER C */
    { 0x0064, 0, "t" },   /* LATIN SMALL LETTER D */
    { 0x0065, 0, "e" },   /* LATIN SMALL LETTER E */
    { 0x0066, 0, "ph" },   /* LATIN SMALL LETTER F */
    { 0x0067, 0, "k" },   /* LATIN SMALL LETTER G */
    { 0x0068, 0, "h" },   /* LATIN SMALL LETTER H */
    { 0x0069, 0, "i" },   /* LATIN SMALL LETTER I */
    { 0x006a, 0, "y" },   /* LATIN SMALL LETTER J */
    { 0x006b, 0, "kk" },   /* LATIN SMALL LETTER K */
    { 0x006c, 0, "l" },   /* LATIN SMALL LETTER L */
    { 0x006d, 0, "m" },   /* LATIN SMALL LETTER M */
    { 0x006e, 0, "n" },   /* LATIN SMALL LETTER N */
    { 0x006f, 0, "o" },   /* LATIN SMALL LETTER O */
    { 0x0070, 0, "pp" },   /* LATIN SMALL LETTER P */
    { 0x0072, 0, "r" },   /* LATIN SMALL LETTER R */
    { 0x0073, 0, "ss" },   /* LATIN SMALL LETTER S */
    { 0x0074, 0, "tt" },   /* LATIN SMALL LETTER T */
    { 0x0075, 0, "u" },   /* LATIN SMALL LETTER U */
    { 0x0076, 0, "p" },   /* LATIN SMALL LETTER V */
    { 0x0077, 0, "w" },   /* LATIN SMALL LETTER W */
    { 0x0079, 0, "wi" },   /* LATIN SMALL LETTER Y */
    { 0x007a, 0, "s" },   /* LATIN SMALL LETTER Z */
    { 0x00e6, 0, "A" },   /* LATIN SMALL LETTER AE */
    { 0x00f8, 0, "O" },   /* LATIN SMALL LETTER O WITH STROKE */
    { 0x0131, 0, "i" },   /* LATIN SMALL LETTER DOTLESS I */
    { 0x014b, 0, "ng" },   /* LATIN SMALL LETTER ENG */
    { 0x0153, 0, "O" },   /* LATIN SMALL LIGATURE OE */
    { 0x0250, 0, "a" },   /* LATIN SMALL LETTER TURNED A */
    { 0x0251, 0, "a" },   /* LATIN SMALL LETTER ALPHA */
    { 0x0252, 0, "o" },   /* LATIN SMALL LETTER TURNED ALPHA */
    { 0x0254, 0, "o" },   /* LATIN SMALL LETTER OPEN O */
    { 0x0258, 0, "E" },   /* LATIN SMALL LETTER REVERSED E */
    { 0x0259, 0, "E" },   /* LATIN SMALL LETTER SCHWA */
    { 0x025a, 0, "Ar" },   /* LATIN SMALL LETTER SCHWA WITH HOOK */
    { 0x025b, 0, "A" },   /* LATIN SMALL LETTER OPEN E */
    { 0x025c, 0, "A" },   /* LATIN SMALL LETTER REVERSED OPEN E */
    { 0x025d, 0, "Ar" },   /* LATIN SMALL LETTER REVERSED OPEN E WITH HOOK */
    { 0x025e, 0, "a" },   /* LATIN SMALL LETTER CLOSED REVERSED OPEN E */
    { 0x025f, 0, "c" },   /* LATIN SMALL LETTER DOTLESS J WITH STROKE */
    { 0x0261, 0, "k" },   /* LATIN SMALL LETTER SCRIPT G */
    { 0x0264, 0, "o" },   /* LATIN SMALL LETTER RAMS HORN */
    { 0x0266, 0, "h" },   /* LATIN SMALL LETTER H WITH HOOK */
    { 0x0268, 0, "i" },   /* LATIN SMALL LETTER I WITH STROKE */
    { 0x0269, 0, "i" },   /* LATIN SMALL LETTER IOTA */
    { 0x026a, 0, "i" },   /* LATIN LETTER SMALL CAPITAL I */
    { 0x026f, 0, "U" },   /* LATIN SMALL LETTER TURNED M */
    { 0x0275, 0, "E" },   /* LATIN SMALL LETTER BARRED O */
    { 0x0276, 0, "O" },   /* LATIN LETTER SMALL CAPITAL OE */
    { 0x0277, 0, "u" },   /* LATIN SMALL LETTER CLOSED OMEGA */
    { 0x0279, 0, "r" },   /* LATIN SMALL LETTER TURNED R */
    { 0x027e, 0, "r" },   /* LATIN SMALL LETTER R WITH FISHHOOK */
    { 0x0280, 0, "r" },   /* LATIN LETTER SMALL CAPITAL R */
    { 0x0281, 0, "r" },   /* LATIN LETTER SMALL CAPITAL INVERTED R */
    { 0x0289, 0, "u" },   /* LATIN SMALL LETTER U BAR */
    { 0x028a, 0, "u" },   /* LATIN SMALL LETTER UPSILON */
    { 0x028c, 0, "E" },   /* LATIN SMALL LETTER TURNED V */
    { 0x028f, 0, "O" },   /* LATIN LETTER SMALL CAPITAL Y */
    { 0x029a, 0, "a" },   /* LATIN SMALL LETTER CLOSED OPEN E */
    { 0x02a3, 0, "c" },   /* LATIN SMALL LETTER DZ DIGRAPH */
    { 0x02a4, 0, "c" },   /* LATIN SMALL LETTER DEZH DIGRAPH */
    { 0x02a5, 0, "c" },   /* LATIN SMALL LETTER DZ DIGRAPH WITH CURL */
    { 0x02a6, 0, "ch" },   /* LATIN SMALL LETTER TS DIGRAPH */
    { 0x02a7, 0, "ch" },   /* LATIN SMALL LETTER TESH DIGRAPH */
    { 0x02a8, 0, "ch" },   /* LATIN SMALL LETTER TC DIGRAPH WITH CURL */
    { 0x02c8, 0, "1" },   /* MODIFIER LETTER VERTICAL LINE */
    { 0x02cc, 0, "2" },   /* MODIFIER LETTER LOW VERTICAL LINE */
    { 0x2016, 0, "`p150" },   /* DOUBLE VERTICAL LINE */
};
static const IpaTwo koko_two[] = {
    { 0x0061, 0x0325, 1, "k" },   /* LATIN SMALL LETTER A + COMBINING RING BELOW */
    { 0x0062, 0x0325, 1, "p" },   /* LATIN SMALL LETTER B + COMBINING RING BELOW */
    { 0x0063, 0x02b0, 1, "ch" },   /* LATIN SMALL LETTER C + MODIFIER LETTER SMALL H */
    { 0x0063, 0x02ed, 1, "cc" },   /* LATIN SMALL LETTER C + MODIFIER LETTER UNASPIRATED */
    { 0x0064, 0x0325, 1, "t" },   /* LATIN SMALL LETTER D + COMBINING RING BELOW */
    { 0x0067, 0x0325, 1, "k" },   /* LATIN SMALL LETTER G + COMBINING RING BELOW */
    { 0x006b, 0x02b0, 1, "kh" },   /* LATIN SMALL LETTER K + MODIFIER LETTER SMALL H */
    { 0x006b, 0x02ed, 1, "kk" },   /* LATIN SMALL LETTER K + MODIFIER LETTER UNASPIRATED */
    { 0x0070, 0x02b0, 1, "ph" },   /* LATIN SMALL LETTER P + MODIFIER LETTER SMALL H */
    { 0x0070, 0x02ed, 1, "pp" },   /* LATIN SMALL LETTER P + MODIFIER LETTER UNASPIRATED */
    { 0x0073, 0x02ed, 1, "ss" },   /* LATIN SMALL LETTER S + MODIFIER LETTER UNASPIRATED */
    { 0x0074, 0x02b0, 1, "th" },   /* LATIN SMALL LETTER T + MODIFIER LETTER SMALL H */
    { 0x0074, 0x02ed, 1, "tt" },   /* LATIN SMALL LETTER T + MODIFIER LETTER UNASPIRATED */
    { 0x025f, 0x0325, 1, "c" },   /* LATIN SMALL LETTER DOTLESS J WITH STROKE + COMBINING RING BELOW */
    { 0x026f, 0x0069, 1, "I" },   /* LATIN SMALL LETTER TURNED M + LATIN SMALL LETTER I */
    { 0x02a3, 0x0325, 1, "c" },   /* LATIN SMALL LETTER DZ DIGRAPH + COMBINING RING BELOW */
    { 0x02a4, 0x0325, 1, "c" },   /* LATIN SMALL LETTER DEZH DIGRAPH + COMBINING RING BELOW */
    { 0x02a5, 0x0325, 1, "c" },   /* LATIN SMALL LETTER DZ DIGRAPH WITH CURL + COMBINING RING BELOW */
    { 0x02a6, 0x02ed, 1, "cc" },   /* LATIN SMALL LETTER TS DIGRAPH + MODIFIER LETTER UNASPIRATED */
    { 0x02a7, 0x02ed, 1, "cc" },   /* LATIN SMALL LETTER TESH DIGRAPH + MODIFIER LETTER UNASPIRATED */
    { 0x02a8, 0x02ed, 1, "cc" },   /* LATIN SMALL LETTER TC DIGRAPH WITH CURL + MODIFIER LETTER UNASPIRATED */
};

/* ---- looking one up -------------------------------------------------- */

/* The pair first, since a diphthong beats either of its halves, then the
   single. Nothing found is minus one, and what the caller was given to
   append to is left as it was. */
static int32_t ipa_convert(const IpaOne *one, size_t nOne,
                           const IpaTwo *two, size_t nTwo,
                           int32_t cur, int32_t next, char *out, int32_t *used)
{
    size_t i;

    *used = 0;

    for (i = 0; i < nTwo; i++) {
        if (two[i].cur != (uint16_t)cur || two[i].next != (uint16_t)next)
            continue;
        strcat(out, two[i].spr);
        *used = two[i].used;
        return 0;
    }

    for (i = 0; i < nOne; i++) {
        if (one[i].cur != (uint16_t)cur)
            continue;
        strcat(out, one[i].spr);
        *used = one[i].used;
        return 0;
    }

    return IPA_UNKNOWN;
}

#define CONVERTER(name, tag)                                              \
    int32_t name(int32_t cur, int32_t next, char *out, int32_t *used)      \
    {                                                                     \
        return ipa_convert(tag##_one, sizeof tag##_one / sizeof tag##_one[0], \
                           tag##_two, sizeof tag##_two / sizeof tag##_two[0], \
                           cur, next, out, used);                         \
    }

CONVERTER(UsEnIPAToSPRConverter, usen)
CONVERTER(UkEnIPAToSPRConverter, uken)
CONVERTER(GrGrIPAToSPRConverter, grgr)
CONVERTER(FrFrIPAToSPRConverter, frfr)
CONVERTER(JpJpIPAToSPRConverter, jpjp)
CONVERTER(KoKoIPAToSPRConverter, koko)

/* ---- and the whole string -------------------------------------------- */

/* Which converter a language gets. Every language IBM shipped has one and
   most of them share; a language with no entry is answered with nothing at
   all rather than refused, which is how Polish behaves today. */
static int32_t ipa_forLanguage(int32_t lang, int32_t cur, int32_t next,
                               char *out, int32_t *used)
{
    switch (lang) {
    case 0x10000:
        return UsEnIPAToSPRConverter(cur, next, out, used);
    case 0x10001:
        return UkEnIPAToSPRConverter(cur, next, out, used);
    case 0x30000:
        return FrFrIPAToSPRConverter(cur, next, out, used);
    /* French Canada with German, which is the original's own slip. */
    case 0x30001:
    case 0x40000:
        return GrGrIPAToSPRConverter(cur, next, out, used);
    case 0x50000:
    case 0x60000:
    case 0x60001:
    case 0x60100:
    case 0x60101:
    case 0x60201:
    case 0x60800:
    case 0x60801:
    case 0x70000:
    case 0x80000:
        return JpJpIPAToSPRConverter(cur, next, out, used);
    case 0x80800:
    case 0x90000:
    case 0xa0000:
        return KoKoIPAToSPRConverter(cur, next, out, used);
    default:
        return 0;
    }
}

/* A whole IPA string, in UTF-8, turned into SPR.
 *
 * The bytes are read into code points first, one more slot than characters
 * so that the walk can always look at the one after the last and find the
 * marker put there. Then each symbol is converted with its successor in
 * hand, and what comes back is appended if there is room; a symbol the
 * language has no answer for contributes nothing and the walk carries on.
 *
 * The caller says how much room the answer has in *room and reads back how
 * much was used. Running out is minus three, and what was written up to
 * that point stands. */
int32_t IPAToSPR(uint8_t *utf8, uint32_t bytes, char *spr, uint32_t *room,
                 int32_t lang)
{
    uint32_t *points;
    uint32_t  count = bytes + 1;
    uint32_t  written = 0;
    uint32_t  i = 0;
    int32_t   rc;

    if (utf8 == 0 || bytes == 0)
        return IPA_NO_INPUT;

    points = malloc(count * 4 + 1);
    rc = ConvertUTF8toUCS32(utf8, bytes, points, &count);
    if (rc != 0) {
        free(points);
        return rc;
    }

    strcpy(spr, "");
    points[count] = (uint32_t)-1;

    while (i < count && written < *room) {
        char    one[SPR_ROOM];
        int32_t used = 0;
        size_t  length;

        strcpy(one, "");
        rc = ipa_forLanguage(lang, (int32_t)points[i],
                             (int32_t)points[i + 1], one, &used);
        if (rc == 0) {
            length = strlen(one);
            if (spr != 0 && length != 0 && written + length < *room)
                strcat(spr, one);
            written += (uint32_t)length;
        }

        if (used != 0)
            i++;
        i++;
    }

    free(points);

    rc = written < *room ? 0 : UTF8_NO_ROOM;
    *room = written;
    return rc;
}

ALIAS("?IsValidUTF8@@YA_NPBEH@Z", "IsValidUTF8");
ALIAS("?ConvertUCS32toUTF8@@YAHPBKKPAEAAK@Z", "ConvertUCS32toUTF8");
ALIAS("?ConvertUCS2toUTF8@@YAHPBGHPAEAAK@Z", "u8_convertUCS2toUTF8");
ALIAS("?ConvertUTF8toUCS32@@YAHPBEKPAKAAK@Z", "ConvertUTF8toUCS32");
ALIAS("?UsEnIPAToSPRConverter@@YAHHHPADAAH@Z", "UsEnIPAToSPRConverter");
ALIAS("?UkEnIPAToSPRConverter@@YAHHHPADAAH@Z", "UkEnIPAToSPRConverter");
ALIAS("?GrGrIPAToSPRConverter@@YAHHHPADAAH@Z", "GrGrIPAToSPRConverter");
ALIAS("?FrFrIPAToSPRConverter@@YAHHHPADAAH@Z", "FrFrIPAToSPRConverter");
ALIAS("?JpJpIPAToSPRConverter@@YAHHHPADAAH@Z", "JpJpIPAToSPRConverter");
ALIAS("?KoKoIPAToSPRConverter@@YAHHHPADAAH@Z", "KoKoIPAToSPRConverter");
ALIAS("?IPAToSPR@@YAHPAEK0AAKW4ECILanguageDialect@@@Z", "IPAToSPR");
