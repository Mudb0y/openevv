/* The two codesets that are not Shift-JIS, folded into the one that is.
 *
 * A caller may hand the romanizer Japanese in EUC-JP or in one of the
 * seven-bit JIS encodings, and everything above this file assumes Shift-JIS,
 * so ConverterInterface::trans2defaultCodeset recodes the text before anyone
 * looks at it. These are the five static methods of JpnUtil that do the work.
 * They are in IBM's codeconv object rather than its jpnutil one, which is why
 * they are here rather than in rom/jajp/jpnutil.c.
 *
 * The interesting one is han2zen, which is not a codeset conversion at all:
 * it turns a half-width katakana character into the full-width one it stands
 * for, and joins a following voicing mark onto it where the pair has a
 * character of its own. Its table is sixty-three entries wide and lives on
 * the stack in the original, one pair of ints per half-width byte from 0xa1
 * to 0xdf; here it is static, since nothing writes it.
 *
 * Three things are IBM's and are kept. euc2shift reads the second byte of a
 * two-byte character without first asking whether there is one, so a text
 * that ends on a lead byte is read one past its end. seven2shift tests
 * whether it is in two-byte mode before clearing the flag, which is the same
 * as clearing it. And jis2sjis divides by two to find the row, which the
 * compiler wrote as a real division because the value it works on is signed.
 *
 * Held to IBM's answer by test/romprims.sh.
 */

#include <stdint.h>
#include "jprom.h"

/* Half-width katakana 0xa1 to 0xdf as the Shift-JIS character it stands for.
   Two bytes each, in the order the original's stack table had them. */
#define HAN_FIRST 0xa1
#define HAN_LAST  0xdf

static const uint8_t han2zenTable[HAN_LAST - HAN_FIRST + 1][2] = {
    { 0x81, 0x42 }, { 0x81, 0x75 }, { 0x81, 0x76 }, { 0x81, 0x41 },
    { 0x81, 0x45 }, { 0x83, 0x92 }, { 0x83, 0x40 }, { 0x83, 0x42 },
    { 0x83, 0x44 }, { 0x83, 0x46 }, { 0x83, 0x48 }, { 0x83, 0x83 },
    { 0x83, 0x85 }, { 0x83, 0x87 }, { 0x83, 0x62 }, { 0x81, 0x5b },
    { 0x83, 0x41 }, { 0x83, 0x43 }, { 0x83, 0x45 }, { 0x83, 0x47 },
    { 0x83, 0x49 }, { 0x83, 0x4a }, { 0x83, 0x4c }, { 0x83, 0x4e },
    { 0x83, 0x50 }, { 0x83, 0x52 }, { 0x83, 0x54 }, { 0x83, 0x56 },
    { 0x83, 0x58 }, { 0x83, 0x5a }, { 0x83, 0x5c }, { 0x83, 0x5e },
    { 0x83, 0x60 }, { 0x83, 0x63 }, { 0x83, 0x65 }, { 0x83, 0x67 },
    { 0x83, 0x69 }, { 0x83, 0x6a }, { 0x83, 0x6b }, { 0x83, 0x6c },
    { 0x83, 0x6d }, { 0x83, 0x6e }, { 0x83, 0x71 }, { 0x83, 0x74 },
    { 0x83, 0x77 }, { 0x83, 0x7a }, { 0x83, 0x7d }, { 0x83, 0x7e },
    { 0x83, 0x80 }, { 0x83, 0x81 }, { 0x83, 0x82 }, { 0x83, 0x84 },
    { 0x83, 0x86 }, { 0x83, 0x88 }, { 0x83, 0x89 }, { 0x83, 0x8a },
    { 0x83, 0x8b }, { 0x83, 0x8c }, { 0x83, 0x8d }, { 0x83, 0x8f },
    { 0x83, 0x93 }, { 0x81, 0x4a }, { 0x81, 0x4b },
};

/* The two voicing marks as they arrive on their own in half width. */
#define HAN_DAKUTEN     0xde
#define HAN_HANDAKUTEN  0xdf

/* Which half-width kana a dakuten may be joined to: the k, s, t and h rows,
   the h row again, and u on its own, which becomes the vu nobody else has. */
static int32_t takesDakuten(uint8_t c)
{
    return (c >= 0xb6 && c <= 0xc4) || (c >= 0xca && c <= 0xce) || c == 0xb3;
}

/* And which may take a handakuten, which is the h row alone. */
static int32_t takesHandakuten(uint8_t c)
{
    return c >= 0xca && c <= 0xce;
}

/* One escape sequence stepped over, and whether it changed the mode.
 *
 * `at' comes in pointing at the byte after the escape and goes out pointing
 * past the sequence. A sequence that begins `$' or `(' has a third byte;
 * either of `$' and `K' turns two-byte mode on and anything else turns it
 * off. Nothing checks that the sequence is one JIS actually defines. */
int32_t ju_SkipESCSeq(const char *text, long *at, int32_t *twoByte)
{
    int32_t     was = *twoByte;
    const char *p   = text + *at;

    (*at)++;
    if (*p == '$' || *p == '(')
        (*at)++;

    *twoByte = (*p == 'K' || *p == '$');
    return was != *twoByte;
}

/* One JIS character turned into the Shift-JIS one, in place.
 *
 * The row is halved and moved to whichever of the two Shift-JIS bands it
 * belongs in, and the cell is shifted by one of three amounts depending on
 * whether the row was odd and where in the row the cell falls. */
void ju_jis2sjis(uint8_t *lead, uint8_t *trail)
{
    uint8_t row  = *lead;
    uint8_t cell = *trail;
    int32_t band = row >= 0x5f ? 0xb0 : 0x70;
    int32_t step;

    if (row % 2 != 0)
        step = cell > 0x5f ? 0x20 : 0x1f;
    else
        step = 0x7e;

    *lead  = (uint8_t)(((row + 1) >> 1) + band);
    *trail = (uint8_t)(cell + step);
}

/* A half-width katakana character widened, and a voicing mark after it taken
 * in where the two together have a character of their own.
 *
 * `lead' comes in holding the half-width byte and both go out holding the
 * Shift-JIS pair. `kind' says which encoding `at' is walking: five for
 * Shift-JIS, where the next byte stands on its own, and four for EUC, where
 * a half-width character is introduced by 0x8e and the mark is two bytes
 * rather than one. Either way `at' is left where the mark was if there was
 * none to take, so the caller reads it again as an ordinary character. */
void ju_han2zen(const char *text, long *at, uint8_t *lead, uint8_t *trail,
                int32_t kind)
{
    uint8_t  was        = *lead;
    int32_t  handakuten = 0;
    int32_t  dakuten    = 0;

    if (kind == 5) {
        *trail = (uint8_t)text[*at];
        (*at)++;
        if (*trail == HAN_DAKUTEN) {
            if (takesDakuten(*lead))
                dakuten = 1;
            else
                (*at)--;
        } else if (*trail == HAN_HANDAKUTEN) {
            if (takesHandakuten(*lead))
                handakuten = 1;
            else
                (*at)--;
        } else {
            (*at)--;
        }
    } else if (kind == 4) {
        /* The single shift that introduces a half-width character in EUC.
           IBM loads it as a signed byte and compares it against 0x8e as a
           number, and 0x8e sign-extended is minus a hundred and fourteen, so
           the two are never equal and everything below this test is dead.
           The effect is that a half-width kana coming out of EUC is widened
           but never has its voicing mark joined to it, which is the one thing
           this arm existed to do. Written as IBM wrote it, sign extension and
           all: the sweep cannot tell a branch that is never taken from one
           that is not there, and the next reader should not have to work out
           which of the two this was. */
        int32_t shift = (signed char)text[*at];

        (*at)++;
        if (shift == 0x8e) {
            *trail = (uint8_t)text[*at];
            (*at)++;
            if (*trail == HAN_DAKUTEN) {
                if (takesDakuten(*lead))
                    dakuten = 1;
                else
                    *at -= 2;
            } else if (*trail == HAN_HANDAKUTEN) {
                if (takesHandakuten(*lead))
                    handakuten = 1;
                else
                    *at -= 2;
            } else {
                *at -= 2;
            }
        } else {
            (*at)--;
        }
    }

    if (*lead >= HAN_FIRST && *lead <= HAN_LAST) {
        *lead  = han2zenTable[was - HAN_FIRST][0];
        *trail = han2zenTable[was - HAN_FIRST][1];
    }

    /* A voiced kana is the next character along in Shift-JIS, except in the
       h row where it is two along, and except for u, whose voiced form is
       nowhere near it. */
    if (dakuten) {
        if ((*trail >= 0x4a && *trail <= 0x67) ||
            (*trail >= 0x6e && *trail <= 0x7a))
            *trail = (uint8_t)(*trail + 1);
        else if (*lead == 0x83 && *trail == 0x45)
            *trail = 0x94;
    } else if (handakuten) {
        if (*trail >= 0x6e && *trail <= 0x7a)
            *trail = (uint8_t)(*trail + 2);
    }
}

/* A whole text from EUC-JP to Shift-JIS, and how many bytes that came to.
 *
 * A two-byte character has both bytes in 0xa1 to 0xfe and is moved down into
 * JIS and across into Shift-JIS. A half-width katakana character arrives
 * behind 0x8e; `wantZen' says whether to widen it, and where it is not
 * widened the 0x8e is dropped and the byte after it is kept as it stands.
 * Anything else goes across unchanged.
 *
 * The lead byte's test does not first ask whether there is a second byte, so
 * a text ending on one is read one byte past its end. That is IBM's and is
 * kept: it cannot be corrected without changing what the function answers on
 * a text that does not end that way. */
long ju_euc2shift(const char *in, long len, char *out, int32_t wantZen)
{
    long    i = 0;
    long    o = 0;
    uint8_t c0;
    uint8_t c1;

    while (i < len) {
        c0 = (uint8_t)in[i];
        i++;

        if (c0 >= 0xa1 && c0 <= 0xfe) {
            c1 = (uint8_t)in[i];
            i++;
            if (c1 >= 0xa1 && c1 <= 0xfe) {
                c0 = (uint8_t)(c0 - 0x80);
                c1 = (uint8_t)(c1 - 0x80);
                ju_jis2sjis(&c0, &c1);
            }
            out[o++] = (char)c0;
            out[o++] = (char)c1;
        } else if (c0 == 0x8e) {
            c1 = (uint8_t)in[i];
            i++;
            if (c1 >= HAN_FIRST && c1 <= HAN_LAST) {
                c0 = c1;
                if (wantZen) {
                    ju_han2zen(in, &i, &c0, &c1, 4);
                    out[o++] = (char)c0;
                    out[o++] = (char)c1;
                } else {
                    out[o++] = (char)c0;
                }
            } else {
                out[o++] = (char)c0;
                out[o++] = (char)c1;
            }
        } else {
            out[o++] = (char)c0;
        }
    }

    out[o] = 0;
    return o;
}

/* And from seven-bit JIS, which says with escape sequences which of its two
 * modes it is in rather than by the value of the bytes.
 *
 * A newline leaves two-byte mode, which is what the standard asks for; the
 * test before the flag is cleared is IBM's and does the same as clearing it
 * outright. */
long ju_seven2shift(const char *in, long len, char *out)
{
    long    o       = 0;
    long    i       = 0;
    int32_t twoByte = 0;
    uint8_t c0;
    uint8_t c1;

    while (i < len) {
        c0 = (uint8_t)in[i];
        i++;

        if (c0 == 0x1b) {
            ju_SkipESCSeq(in, &i, &twoByte);
        } else if (c0 == 0x0a || c0 == 0x0d) {
            if (twoByte)
                twoByte = 0;
            out[o++] = (char)c0;
        } else if (twoByte) {
            c1 = (uint8_t)in[i];
            i++;
            ju_jis2sjis(&c0, &c1);
            out[o++] = (char)c0;
            out[o++] = (char)c1;
        } else {
            out[o++] = (char)c0;
        }
    }

    out[o] = 0;
    return o;
}
