/* Making a sentence readable: dates, times, money and the rest into words.
 *
 * This runs in front of the analyser rather than behind it. A stretch of text
 * that is not words -- 1999/12/31, 3:45, \1,200, 03-1234-5678 -- is rewritten
 * into the words a reader would say, and only then does `TextAnalysis' see
 * it. `TextNormalizer' decides which of the eight normalisers a stretch
 * wants; this file is the eight of them, for Japanese, and the machinery they
 * share.
 *
 * That machinery is four functions and a rule. The rule is that the caller
 * owns the buffer and this class may grow it: every appender is handed the
 * buffer, how big it is and how much is in it, and reallocates with a quarter
 * of a kilobyte of slack where the answer will not fit. Nothing here writes
 * past what it has asked for.
 *
 * The twelve predicates each ask one question of one table, and the tables
 * are IBM's: lang/jajp/rom_tables_jajp.c holds them and tools/rom/tables.py
 * lifts them, pairs of what a symbol means and how it is written.
 *
 * rom/jajp/makereadable.h is the record, such as it is -- both classes hold
 * nothing but a vtable.
 *
 * Held to IBM's answer by test/harness/romprims.sh.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "jprom.h"
#include "makereadable.h"
#include "rom_tables_jajp.h"

/* The one field either class has. */
#define MR_VTABLE_OF(mr) (*(void **)((uint8_t *)(mr) + MR_VTABLE_AT))

/* ---- being made and unmade ------------------------------------------ */

/* The base holds a vtable and nothing else, and so does what derives from
   it: the constructor plants one and the destructor clears it. Nothing here
   dispatches through either, so nought stands in that slot. */
void *mrl_ctor(void *mr)
{
    MR_VTABLE_OF(mr) = 0;
    return mr;
}

void mrl_dtor(void *mr)
{
    MR_VTABLE_OF(mr) = 0;
}

void *mr_ctor(void *mr)
{
    mrl_ctor(mr);
    MR_VTABLE_OF(mr) = 0;
    return mr;
}

void mr_dtor(void *mr)
{
    MR_VTABLE_OF(mr) = 0;
    mrl_dtor(mr);
}

/* The scalar deleting destructor, which is the shape MSVC gives a virtual
   destructor: the flag says whether to give the storage back as well. */
void *mr_destroy(void *mr, int32_t freeIt)
{
    mr_dtor(mr);
    if (freeIt & 1)
        cpp_delete(mr);
    return mr;
}

/* ---- the buffer the answer goes in ---------------------------------- */

/* A bigger buffer with what was in the old one copied over. The old one goes
   back whether the copy was wanted or not, so a caller that hands in a
   buffer it did not allocate is a caller that crashes -- IBM's arrangement,
   and every caller here allocates. */
int32_t mr_reallocateBuf(void *mr, char **buf, uint32_t used, uint32_t want)
{
    /* One byte more than asked for, and that byte is the divergence. Every
       normaliser here ends by writing a nought at the length it reached, and
       a length may reach the size exactly -- an append fits when what is
       there plus what is coming equals the size, and only grows the buffer
       when it is more. IBM allocates exactly what was asked and writes that
       nought one past the end; its own heap says nothing and this one aborts
       the process. The byte is never read, so no answer changes. */
    char *got = (char *)cpp_new(want + 1);

    (void)mr;
    if (got == NULL)
        return 1;
    memcpy(got, *buf, used);
    cpp_delete(*buf);
    *buf = got;
    return 0;
}

/* The answer copied in whole, which is what the two normalisers that do no
   work at all use. The buffer is replaced rather than grown, so what was in
   it is not kept. */
int32_t mrl_copyAndReturn(void *mr, const char *text, uint32_t n, char **buf,
                          uint32_t *cap)
{
    (void)mr;
    if (n + 1 > *cap) {
        char *got = (char *)cpp_new(n + 1);

        if (got == NULL)
            return 1;
        cpp_delete(*buf);
        *buf = got;
        *cap = n + 1;
    }
    memcpy(*buf, text, n);
    (*buf)[n] = '\0';
    return 0;
}

/* So many bytes appended, with the buffer grown by a quarter of a kilobyte
   more than is needed where it will not fit. */
int32_t mr_appendTextN(void *mr, const char *text, uint32_t n, char **buf,
                       uint32_t *cap, uint32_t *len)
{
    if (*len + n > *cap) {
        if (mr_reallocateBuf(mr, buf, *cap, *len + n + MR_SLACK) != 0)
            return 1;
        *cap = *len + n + MR_SLACK;
    }
    memcpy(*buf + *len, text, n);
    *len += n;
    return 0;
}

/* And a whole string. */
int32_t mr_appendText(void *mr, const char *text, char **buf, uint32_t *cap,
                      uint32_t *len)
{
    return mr_appendTextN(mr, text, (uint32_t)strlen(text), buf, cap, len);
}

/* One character, which is one byte or two by whether it leads a two-byte
   one. The slack differs by one between the two arms, which is the
   terminator each of them leaves room for. */
int32_t mr_appendChar(void *mr, const char *c, char **buf, uint32_t *cap,
                      uint32_t *len)
{
    if (c == NULL)
        return 0;

    if (ju_IsDBCSLeadByte(c[0])) {
        if (*len + 2 > *cap) {
            if (mr_reallocateBuf(mr, buf, *cap, *len + MR_SLACK + 2) != 0)
                return 1;
            *cap = *len + MR_SLACK + 2;
        }
        (*buf)[(*len)++] = c[0];
        (*buf)[(*len)++] = c[1];
    } else {
        if (*len + 1 > *cap) {
            if (mr_reallocateBuf(mr, buf, *cap, *len + MR_SLACK + 1) != 0)
                return 1;
            *cap = *len + MR_SLACK + 1;
        }
        (*buf)[(*len)++] = c[0];
    }
    return 0;
}

/* ---- what the text begins with -------------------------------------- */

/* Every one of the twelve predicates is this walk over a table of its own:
   the first entry whose string the text begins with wins, and what comes
   back is what that entry means, with how long it was written where the
   caller asked. A table ends on an entry with no string. */
int32_t mr_isSymbol(void *mr, const char *text, const jajp_symbol *table,
                    uint32_t *howLong)
{
    int32_t i;

    (void)mr;
    for (i = 0; table[i].how != NULL; i++) {
        size_t n = strlen(table[i].how);

        if (strncmp(text, table[i].how, n) == 0) {
            if (howLong != NULL)
                *howLong = (uint32_t)strlen(table[i].how);
            return table[i].what;
        }
    }
    if (howLong != NULL)
        *howLong = 0;
    return 0;
}

int32_t mr_isCurrencySymbol(void *mr, const char *t, uint32_t *howLong)
{
    return mr_isSymbol(mr, t, jajp_aCURRENCY_SYMBOLS, howLong);
}

int32_t mr_isBoolSymbol(void *mr, const char *t, uint32_t *howLong)
{
    return mr_isSymbol(mr, t, jajp_aBOOL_SYMBOLS, howLong);
}

int32_t mr_isCurrencyPunct(void *mr, const char *t)
{
    return mr_isSymbol(mr, t, jajp_aCURRENCY_PUNCTS, NULL);
}

int32_t mr_isDecimalPoint(void *mr, const char *t)
{
    return mr_isSymbol(mr, t, jajp_aDECIMAL_POINTS, NULL);
}

int32_t mr_isParenthesis(void *mr, const char *t)
{
    return mr_isSymbol(mr, t, jajp_aPARENTHESIS_SYMBOLS, NULL);
}

int32_t mr_isTimeDelimiter(void *mr, const char *t)
{
    return mr_isSymbol(mr, t, jajp_aTIME_DELIMS, NULL);
}

int32_t mr_isPlusMinusSymbol(void *mr, const char *t)
{
    return mr_isSymbol(mr, t, jajp_aPLUS_MINUS_SYMBOLS, NULL);
}

int32_t mr_isDayOfWeek(void *mr, const char *t)
{
    return mr_isSymbol(mr, t, jajp_aDAYOFWEEK_SYMBOLS, NULL);
}

int32_t mr_isRangeSymbol(void *mr, const char *t)
{
    return mr_isSymbol(mr, t, jajp_aRANGE_SYMBOLS, NULL);
}

int32_t mr_isDateSeparator(void *mr, const char *t)
{
    return mr_isSymbol(mr, t, jajp_aDATE_SEPARATORS, NULL);
}

int32_t mr_isTelSymbol(void *mr, const char *t)
{
    return mr_isSymbol(mr, t, jajp_aTEL_SYMBOLS, NULL);
}

/* The two that are not table walks. A full-width digit is the ten codes from
   0x824f, and a digit is either that or a half-width one. */
int32_t mr_isDBCSDigit(void *mr, const char *t)
{
    (void)mr;
    if (!ju_IsDBCSLeadByte(t[0]))
        return 0;
    return ((uint8_t)t[0] == 0x82
            && (uint8_t)t[1] >= 0x4f && (uint8_t)t[1] <= 0x58) ? 1 : 0;
}

int32_t mr_isDigit(void *mr, const char *t)
{
    if (ju_IsNum(t[0]) || mr_isDBCSDigit(mr, t))
        return 1;
    return 0;
}

/* ---- the two normalisers that do nothing ---------------------------- */

/* A string of digits and a literal are both handed straight back. The flag
   the caller passes is not read by either. */
int32_t mr_normalizeDigits(void *mr, const char *text, uint32_t n,
                           char **buf, uint32_t *cap, int32_t flag)
{
    (void)flag;
    return mrl_copyAndReturn(mr, text, n, buf, cap);
}

int32_t mr_normalizeLiteral(void *mr, const char *text, uint32_t n,
                            char **buf, uint32_t *cap, int32_t flag)
{
    (void)flag;
    return mrl_copyAndReturn(mr, text, n, buf, cap);
}

/* ---- the two simple normalisers ------------------------------------- */

/* Every Japanese word this file writes is Shift-JIS and is written here as
   the bytes rather than as the characters, so that a file in another encoding
   still compiles to the same thing. What each one says is in the comment
   beside it. */
#define MR_HAI      "\x82\xcd\x82\xa2"                  /* hai, yes */
#define MR_IIE      "\x82\xa2\x82\xa2\x82\xa6"          /* iie, no */
#define MR_PURASU   "\x83\x76\x83\x89\x83\x58"          /* purasu, plus */
#define MR_MAINASU  "\x83\x7d\x83\x43\x83\x69\x83\x58"  /* mainasu, minus */
#define MR_PURAMAI  MR_PURASU MR_MAINASU                /* plus or minus */
#define MR_ZERO     "\x82\x4f"                          /* the full-width 0 */

/* Yes and no, whatever they were written as. Everything else in the text is
   copied through a character at a time. */
int32_t mr_normalizeBool(void *mr, const char *text, uint32_t n, char **buf,
                         uint32_t *cap, int32_t flag)
{
    uint32_t    len = 0;
    const char *p = text;
    const char *end = text + n;
    int32_t     rc;

    (void)flag;
    while (p < end) {
        const char *next = ju_IsDBCSLeadByte(p[0]) ? p + 2 : p + 1;
        uint32_t    howLong = 0;
        int32_t     what = mr_isBoolSymbol(mr, p, &howLong);

        if (what != 0) {
            next = p + howLong;
            if (what == 1)
                rc = mr_appendText(mr, MR_HAI, buf, cap, &len);
            else
                rc = mr_appendText(mr, MR_IIE, buf, cap, &len);
        } else {
            rc = mr_appendChar(mr, p, buf, cap, &len);
        }
        if (rc != 0)
            return rc;
        p = next;
    }
    (*buf)[len] = '\0';
    return 0;
}

/* A sign in front of a number said as a word. Note the walk decides how far
   to step before it asks what the symbol is and then steps by what the symbol
   said instead, which is how a two-byte sign written as one byte still
   advances by one. */
int32_t mr_normalizeNumber(void *mr, const char *text, uint32_t n, char **buf,
                           uint32_t *cap, int32_t flag)
{
    uint32_t    len = 0;
    const char *p = text;
    const char *end = text + n;
    int32_t     rc;

    (void)flag;
    while (p < end) {
        const char *next = ju_IsDBCSLeadByte(p[0]) ? p + 2 : p + 1;
        int32_t     what = mr_isPlusMinusSymbol(mr, p);

        if (what != 0) {
            switch (what) {
            case 1:
                rc = mr_appendText(mr, MR_PURASU, buf, cap, &len);
                break;
            case 2:
                rc = mr_appendText(mr, MR_MAINASU, buf, cap, &len);
                break;
            case 3:
                rc = mr_appendText(mr, MR_PURAMAI, buf, cap, &len);
                break;
            default:
                rc = 0;
                break;
            }
        } else {
            rc = mr_appendChar(mr, p, buf, cap, &len);
        }
        if (rc != 0)
            return rc;
        p = next;
    }
    (*buf)[len] = '\0';
    return 0;
}

/* Where a run of leading zeros ends, so that a number is not read out with
   them. The last character is never skipped: a number of nothing but zeros
   keeps one, which is what a reader says. */
const char *mr_suppressZero(void *mr, const char *p, const char *end)
{
    (void)mr;
    while (p < end) {
        if (ju_IsDBCSLeadByte(p[0])) {
            if (strncmp(p, MR_ZERO, strlen(MR_ZERO)) != 0)
                break;
            if (p + 2 == end)
                break;
            p += 2;
        } else if (p[0] == '0') {
            if (p + 1 == end)
                break;
            p += 1;
        } else {
            break;
        }
    }
    return p;
}

/* ---- a number, as a pair of ends ------------------------------------ */

/* What the normalisers pass a number about as: where it starts and where it
   stops, so that a piece of the caller's own text can be handed on without
   being copied first. */
#define MR_NUM_FROM(p) (((const char **)(p))[0])
#define MR_NUM_TO(p)   (((const char **)(p))[1])

/* A number appended, with its leading zeros dropped first where the caller
   asks. Dropping them moves the number's own start, so a caller that asks
   twice gets the shortened one the second time. */
int32_t mr_appendMakeReadableNumber(void *mr, void *num, char **buf,
                                    uint32_t *cap, uint32_t *len,
                                    int32_t trimZeros)
{
    uint32_t n;

    if (trimZeros != 0)
        MR_NUM_FROM(num) = mr_suppressZero(mr, MR_NUM_FROM(num),
                                           MR_NUM_TO(num));
    n = (uint32_t)(MR_NUM_TO(num) - MR_NUM_FROM(num));
    if (*len + n > *cap) {
        if (mr_reallocateBuf(mr, buf, *cap, *len + n + MR_SLACK) != 0)
            return 1;
        *cap = *len + n + MR_SLACK;
    }
    memcpy(*buf + *len, MR_NUM_FROM(num), n);
    *len += n;
    return 0;
}

/* A number cut in two at its decimal point, which is only a cut if a digit
 * follows the point: `1.' is one number and not two.
 *
 * IBM looks for the full-width comma here and not the full-width point. Its
 * own table of decimal points holds the point and the half-width stop, and
 * this function uses neither -- it tests the half-width stop by hand and the
 * two-byte one against 0x8143, which is the comma. So a number written with
 * the full-width point is not split and one written with the full-width comma
 * is. That is IBM's and it is reproduced.
 */
#define MR_FW_COMMA "\x81\x43"

int32_t mr_separateNumberByDecimalPoint(void *mr, const void *whole,
                                        void *left, void *right)
{
    const char *p;
    int32_t     found = 0;

    MR_NUM_FROM(left)  = MR_NUM_FROM(whole);
    p                  = MR_NUM_FROM(left);
    MR_NUM_TO(right)   = MR_NUM_TO(whole);
    MR_NUM_FROM(right) = NULL;
    MR_NUM_TO(left)    = NULL;

    while (p < MR_NUM_TO(whole)) {
        if (ju_IsDBCSLeadByte(p[0])) {
            if (strncmp(p, MR_FW_COMMA, strlen(MR_FW_COMMA)) == 0) {
                MR_NUM_TO(left) = p;
                if (mr_isDigit(mr, p + 2)) {
                    MR_NUM_FROM(right) = p + 2;
                    found = 1;
                }
                break;
            }
            p += 2;
        } else if (p[0] == '.') {
            MR_NUM_TO(left) = p;
            if (mr_isDigit(mr, p + 1)) {
                MR_NUM_FROM(right) = p + 1;
                found = 1;
            }
            break;
        } else {
            p += 1;
        }
    }
    if (found == 0)
        MR_NUM_TO(left) = MR_NUM_TO(whole);
    return found;
}

/* ---- a telephone number --------------------------------------------- */

#define MR_TS_ON    "`ts1"          /* the mark that turns the mode on */
#define MR_TS_OFF   "`ts0"          /* and off again */
#define MR_SHAAPU   "\x83\x56\x83\x83\x81\x5b\x83\x76"  /* shaapu, hash */
#define MR_KOMEJI   "\x83\x52\x83\x81\x83\x57\x83\x8b\x83\x56"
                                                        /* komejirushi, star */
#define MR_NAISEN   "\x93\xe0\x90\xfc"                  /* naisen, extension */
#define MR_NO       "\x82\xcc"                          /* no, the particle */
#define MR_NII      "\x82\xc9\x81\x5b"                  /* nii, a long two */
#define MR_GOO      "\x82\xb2\x81\x5b"                  /* goo, a long five */
#define MR_FW_2     "\x82\x51"                          /* the full-width 2 */
#define MR_FW_5     "\x82\x54"                          /* and 5 */

/* A telephone number said the way a Japanese speaker says one.
 *
 * The whole answer is wrapped in the mark that turns the reading mode on and
 * off. Inside it, every digit is followed by a space so that each is said on
 * its own, and two of the ten are not said as themselves: two becomes `nii'
 * and five becomes `goo', both drawn out, because `ni' and `go' are too easy
 * to confuse with each other and with `shi' over a telephone. The symbols
 * become words -- hash, star, extension -- and the dash becomes the particle
 * `no', which is how a number is dictated.
 *
 * None of that happens unless the caller passes exactly 0x10301. Any other
 * value copies the text through a character at a time, marks and all.
 */
int32_t mr_normalizePhone(void *mr, const char *text, uint32_t n, char **buf,
                          uint32_t *cap, int32_t flag)
{
    uint32_t    len = 0;
    const char *p = text;
    const char *end = text + n;
    int32_t     rc;

    rc = mr_appendText(mr, MR_TS_ON, buf, cap, &len);
    rc = mr_appendText(mr, " ", buf, cap, &len);

    while (p < end) {
        const char *next = ju_IsValidDBCS(p) ? p + 2 : p + 1;

        if (flag == 0x10301) {
            int32_t what = mr_isTelSymbol(mr, p);

            if (what != 0) {
                switch (what) {
                case 1:
                    rc = mr_appendText(mr, MR_SHAAPU, buf, cap, &len);
                    break;
                case 2:
                    rc = mr_appendText(mr, MR_KOMEJI, buf, cap, &len);
                    break;
                case 3:
                    rc = mr_appendText(mr, MR_NAISEN, buf, cap, &len);
                    break;
                case 4:
                    rc = mr_appendText(mr, MR_NO, buf, cap, &len);
                    break;
                default:
                    break;
                }
                if (rc != 0)
                    return rc;
                rc = mr_appendText(mr, ", ", buf, cap, &len);
            } else if (mr_isDigit(mr, p)) {
                if (p[0] == '2' || strncmp(p, MR_FW_2, 2) == 0)
                    rc = mr_appendText(mr, MR_NII, buf, cap, &len);
                else if (p[0] == '5' || strncmp(p, MR_FW_5, 2) == 0)
                    rc = mr_appendText(mr, MR_GOO, buf, cap, &len);
                else
                    rc = mr_appendChar(mr, p, buf, cap, &len);
                if (rc != 0)
                    return rc;
                rc = mr_appendText(mr, ", ", buf, cap, &len);
            } else {
                rc = mr_appendChar(mr, p, buf, cap, &len);
            }
        } else {
            rc = mr_appendChar(mr, p, buf, cap, &len);
        }
        if (rc != 0)
            return rc;
        p = next;
    }

    rc = mr_appendText(mr, MR_TS_OFF, buf, cap, &len);
    if (rc != 0)
        return rc;
    rc = mr_appendText(mr, " ", buf, cap, &len);
    if (rc != 0)
        return rc;
    (*buf)[len] = '\0';
    return 0;
}

/* ---- a time of day -------------------------------------------------- */

#define MR_JI     "\x8e\x9e"          /* ji, the hour */
#define MR_FUN    "\x95\xaa"          /* fun, the minute */
#define MR_BYOU   "\x95\x62"          /* byou, the second */
#define MR_GOZEN  "\x8c\xdf\x91\x4f"  /* gozen, before noon */
#define MR_GOGO   "\x8c\xdf\x8c\xe3"  /* gogo, after noon */

/* A time of day, as three numbers and the words for what each of them is.
 *
 * The walk is a state machine of three states over the text: reading a
 * number, having just seen a delimiter, and reading anything else. A number
 * followed by a delimiter is an hour, the next is a minute and the next a
 * second, and the unit word goes in after each; a number followed by
 * something else ends the time and puts the last unit in.
 *
 * The one shape that is not a run of numbers with colons between is four
 * digits in a row followed by an `a', a `p' or an `h', which is a clock time
 * written the short way: 0930a becomes gozen, nine ji, thirty fun. Both
 * halves have their leading zeros dropped separately, so it is nine and not
 * oh-nine.
 */
int32_t mr_normalizeTime(void *mr, const char *text, uint32_t n, char **buf,
                         uint32_t *cap, int32_t flag)
{
    const char *num[2];              /* where the number in hand runs */
    uint32_t    len = 0;
    const char *p = text;
    const char *end = text + n;
    const char *next;
    int32_t     rc = 0;
    int32_t     part = 0;            /* hour, minute, second */
    int32_t     state;
    int32_t     digits = 0;
    int32_t     flushNum = 0;
    int32_t     flushChar = 0;
    int32_t     writeUnit = 0;
    int32_t     resetPart = 0;
    int32_t     shortForm = 0;
    int32_t     ampm = 0;

    (void)flag;
    num[1] = NULL;
    num[0] = num[1];

    if (mr_isDigit(mr, p)) {
        digits++;
        state  = 0;
        num[0] = p;
    } else {
        state = 2;
    }

    while (p < end) {
        next = ju_IsDBCSLeadByte(p[0]) ? p + 2 : p + 1;

        switch (state) {
        case 0:
            if (mr_isDigit(mr, next)) {
                digits++;
                break;
            }
            if (digits == 4 && next - num[0] == 4) {
                shortForm = 1;
                num[1]    = next;
                ampm      = 3;
                if (next[0] == 'a') {
                    ampm = 1;
                    next++;
                } else if (next[0] == 'p') {
                    ampm = 2;
                    next++;
                } else if (next[0] == 'h') {
                    next++;
                }
                if (mr_isDigit(mr, next)) {
                    state  = 0;
                    num[0] = next;
                    digits = 1;
                } else {
                    state = 2;
                }
                break;
            }
            if (mr_isTimeDelimiter(mr, next) && part < 2) {
                state = 1;
            } else {
                if (part > 0) {
                    writeUnit = 1;
                    resetPart = 1;
                }
                state = 2;
            }
            num[1]   = next;
            flushNum = 1;
            break;

        case 1:
            if (mr_isDigit(mr, next)) {
                digits    = 1;
                writeUnit = 1;
                num[0]    = next;
                state     = 0;
            } else {
                if (part > 0) {
                    writeUnit = 1;
                    resetPart = 1;
                }
                state     = 2;
                flushChar = 1;
            }
            break;

        case 2:
            if (mr_isDigit(mr, next)) {
                digits = 1;
                num[0] = next;
                state  = 0;
            }
            flushChar = 1;
            break;

        default:
            break;
        }

        if (flushNum != 0) {
            rc = mr_appendMakeReadableNumber(mr, num, buf, cap, &len, 1);
            flushNum = 0;
            if (rc != 0)
                return rc;
        }

        if (writeUnit != 0) {
            switch (part) {
            case 0:
                rc = mr_appendText(mr, MR_JI, buf, cap, &len);
                break;
            case 1:
                rc = mr_appendText(mr, MR_FUN, buf, cap, &len);
                break;
            case 2:
                rc = mr_appendText(mr, MR_BYOU, buf, cap, &len);
                break;
            default:
                break;
            }
            if (rc != 0)
                return rc;
            part++;
            writeUnit = 0;
            if (resetPart != 0) {
                part      = 0;
                resetPart = 0;
            }
        }

        if (shortForm != 0) {
            const char *hour;
            const char *minute;

            if (ampm == 1)
                rc = mr_appendText(mr, MR_GOZEN, buf, cap, &len);
            else if (ampm == 2)
                rc = mr_appendText(mr, MR_GOGO, buf, cap, &len);
            if (rc != 0)
                return rc;

            hour   = mr_suppressZero(mr, num[0], num[0] + 2);
            minute = mr_suppressZero(mr, num[0] + 2, num[1]);

            rc = mr_appendTextN(mr, hour, (uint32_t)(num[0] + 2 - hour),
                                buf, cap, &len);
            if (rc != 0)
                return rc;
            rc = mr_appendText(mr, MR_JI, buf, cap, &len);
            if (rc != 0)
                return rc;
            rc = mr_appendTextN(mr, minute, (uint32_t)(num[1] - minute),
                                buf, cap, &len);
            if (rc != 0)
                return rc;
            rc = mr_appendText(mr, MR_FUN, buf, cap, &len);
            if (rc != 0)
                return rc;
            shortForm = 0;
        }

        if (flushChar != 0) {
            rc = mr_appendChar(mr, p, buf, cap, &len);
            if (rc != 0)
                return rc;
            flushChar = 0;
        }

        p = next;
    }
    (*buf)[len] = '\0';
    return 0;
}

/* ---- an amount of money --------------------------------------------- */

#define MR_EN      "\x89\x7e"                              /* en, the yen */
#define MR_SEN     "\x91\x4b"                              /* sen, a hundredth */
#define MR_DORU    "\x83\x68\x83\x8b"                      /* doru, dollar */
#define MR_SENTO   "\x83\x5a\x83\x93\x83\x67"              /* sento, cent */
#define MR_YUURO   "\x83\x86\x81\x5b\x83\x8d"              /* yuuro, euro */
#define MR_GEN     "\x83\x51\x83\x93"                      /* gen, the yuan */
#define MR_PONDO   "\x83\x7c\x83\x93\x83\x68"              /* pondo, pound */
#define MR_WON     "\x83\x45\x83\x48\x83\x93"              /* won */
#define MR_RUUBURU "\x83\x8b\x81\x5b\x83\x75\x83\x8b"      /* ruuburu, rouble */
#define MR_ONSU    "\x83\x49\x83\x93\x83\x58"              /* onsu, ounce */
#define MR_KARA    "\x82\xa9\x82\xe7"                      /* kara, from */
#define MR_AUD  "\x83\x49\x81\x5b\x83\x58\x83\x67\x83\x89\x83\x8a\x83\x41\x83\x68\x83\x8b"
#define MR_CAD  "\x83\x4a\x83\x69\x83\x5f\x83\x68\x83\x8b"
#define MR_CHF  "\x83\x58\x83\x43\x83\x58\x83\x74\x83\x89\x83\x93"
#define MR_HKD  "\x83\x7a\x83\x93\x83\x52\x83\x93\x83\x68\x83\x8b"
#define MR_NOK  "\x83\x6d\x83\x8b\x83\x45\x83\x46\x81\x5b\x83\x4e\x83\x8d\x81\x5b\x83\x6c"
#define MR_NZD  "\x83\x6a\x83\x85\x81\x5b\x83\x57\x81\x5b\x83\x89\x83\x93\x83\x68\x83\x68\x83\x8b"
#define MR_SEK  "\x83\x58\x83\x45\x83\x46\x81\x5b\x83\x66\x83\x93\x83\x4e\x83\x8d\x81\x5b\x83\x69"
#define MR_SGD  "\x83\x56\x83\x93\x83\x4b\x83\x7c\x81\x5b\x83\x8b\x83\x68\x83\x8b"
#define MR_TRL  "\x83\x67\x83\x8b\x83\x52\x83\x8a\x83\x89"
#define MR_TWD  "\x83\x5e\x83\x43\x83\x8f\x83\x93\x83\x68\x83\x8b"

/* An amount of money, as the number and then what it is money in.
 *
 * The walk is three states -- having just seen a currency symbol, reading a
 * number, and reading anything else -- and what makes it long is that a
 * currency amount has so many parts: a symbol before the number or after it,
 * a sign in front, thousands separators inside, a decimal point, a range with
 * another amount after it. Each of those is a flag, and the tail of the loop
 * writes whatever the flags say, in order: the sign, then the number, then
 * the currency, then the fraction with its own unit, then any character that
 * was not part of any of it, then the word for a range.
 *
 * Twenty-five currencies, and the names are IBM's. The fraction is only
 * named for three of them -- sen for the yen, cents for the dollar and the
 * euro -- and every other currency's fraction is said as a bare number.
 *
 * A number is only split at its decimal point where at most two digits
 * follow the point, which is what keeps 3.14159 from being read as three
 * point fourteen thousand.
 */
int32_t mr_normalizeCurrency(void *mr, const char *text, uint32_t n,
                             char **buf, uint32_t *cap, int32_t flag)
{
    const char *num[2];
    const char *left[2];
    const char *right[2];
    uint32_t    len = 0;
    uint32_t    curLen = 0;
    const char *p = text;
    const char *end = text + n;
    const char *next;
    const char *afterSign;
    int32_t     rc = 0;
    int32_t     state;
    int32_t     cur;
    int32_t     unit = 0;
    int32_t     sign;
    int32_t     signToWrite = 0;
    int32_t     inNumber = 0;
    int32_t     sawPunct = 0;
    int32_t     punctCount = 0;
    int32_t     numberReady = 0;
    int32_t     haveNumber = 0;
    int32_t     sawRange = 0;
    int32_t     split = 0;
    int32_t     flushChar = 0;

    (void)flag;
    num[0] = NULL;
    num[1] = NULL;

    cur   = mr_isCurrencySymbol(mr, p, &curLen);
    state = cur != 0 ? 0 : 3;

    while (p < end) {
        next = ju_IsValidDBCS(p) ? p + 2 : p + 1;

        switch (state) {
        case 0:
            inNumber = 0;
            if (cur != 0)
                next = p + curLen;
            if (*next != '\0') {
                if (mr_isDigit(mr, next)) {
                    num[0] = next;
                    state  = 1;
                    unit   = cur;
                } else if ((sign = mr_isPlusMinusSymbol(mr, next)) != 0) {
                    afterSign = ju_IsDBCSLeadByte(next[0]) ? next + 2
                                                          : next + 1;
                    if (mr_isDigit(mr, afterSign)) {
                        num[0]      = afterSign;
                        state       = 1;
                        next        = afterSign;
                        signToWrite = sign;
                        if (inNumber == 0)
                            unit = cur;
                        haveNumber = 1;
                    } else {
                        state     = 3;
                        flushChar = 1;
                    }
                } else {
                    state     = 3;
                    flushChar = 1;
                }
            } else {
                signToWrite = mr_isPlusMinusSymbol(mr, p);
                if (signToWrite != 0)
                    haveNumber = 1;
                else if (sawRange == 0)
                    flushChar = 1;
            }
            break;

        case 1:
            inNumber = 1;
            if (mr_isDigit(mr, next)) {
                if (num[0] == NULL)
                    num[0] = next;
                if (sawPunct != 0)
                    punctCount++;
            } else if (mr_isCurrencyPunct(mr, next) && mr_isDigit(mr, p)) {
                /* A separator inside a number is neither read nor kept. */
            } else if (mr_isDecimalPoint(mr, next) && mr_isDigit(mr, p)
                       && sawPunct == 0) {
                sawPunct   = 1;
                punctCount = 0;
            } else if (mr_isRangeSymbol(mr, next) && mr_isDigit(mr, p)) {
                numberReady = 1;
                sawRange    = 1;
                num[1]      = next;
            } else {
                if (num[0] != NULL) {
                    if (mr_isCurrencyPunct(mr, p)
                        || mr_isDecimalPoint(mr, p)) {
                        num[1]    = p;
                        flushChar = 1;
                    } else {
                        num[1] = next;
                    }
                    numberReady = 1;
                }
                cur   = mr_isCurrencySymbol(mr, next, &curLen);
                state = cur != 0 ? 0 : 3;
            }
            break;

        case 3:
            if (mr_isDigit(mr, next) && inNumber != 0) {
                state  = 1;
                num[0] = next;
            } else {
                cur = mr_isCurrencySymbol(mr, next, &curLen);
                if (cur != 0)
                    state = 0;
            }
            signToWrite = mr_isPlusMinusSymbol(mr, p);
            if (signToWrite != 0)
                haveNumber = 1;
            else if (mr_isRangeSymbol(mr, p))
                sawRange = 1;
            else
                flushChar = 1;
            break;

        default:
            break;
        }

        if (haveNumber != 0) {
            switch (signToWrite) {
            case 1:
                rc = mr_appendText(mr, MR_PURASU, buf, cap, &len);
                break;
            case 2:
                rc = mr_appendText(mr, MR_MAINASU, buf, cap, &len);
                break;
            case 3:
                rc = mr_appendText(mr, MR_PURAMAI, buf, cap, &len);
                break;
            default:
                break;
            }
            if (rc != 0)
                return rc;
            signToWrite = 0;
            haveNumber  = 0;
        }

        if (numberReady != 0) {
            split = 0;
            if (sawPunct != 0 && punctCount <= 2) {
                split = mr_separateNumberByDecimalPoint(mr, num, left, right);
                rc = mr_appendMakeReadableNumber(mr, left, buf, cap, &len, 1);
            } else {
                rc = mr_appendMakeReadableNumber(mr, num, buf, cap, &len, 1);
            }
            if (rc != 0)
                return rc;

            switch (unit) {
            case 1:  rc = mr_appendText(mr, MR_EN, buf, cap, &len); break;
            case 2:  rc = mr_appendText(mr, MR_DORU, buf, cap, &len); break;
            case 3:  rc = mr_appendText(mr, MR_YUURO, buf, cap, &len); break;
            case 4:  rc = mr_appendText(mr, (const char *)jajp_szKANA_ARS,
                                        buf, cap, &len); break;
            case 5:  rc = mr_appendText(mr, MR_AUD, buf, cap, &len); break;
            case 6:  rc = mr_appendText(mr, MR_CAD, buf, cap, &len); break;
            case 7:  rc = mr_appendText(mr, MR_CHF, buf, cap, &len); break;
            case 8:  rc = mr_appendText(mr, (const char *)jajp_szKANA_CLP,
                                        buf, cap, &len); break;
            case 9:  rc = mr_appendText(mr, MR_GEN, buf, cap, &len); break;
            case 10: rc = mr_appendText(mr, (const char *)jajp_szKANA_COP,
                                        buf, cap, &len); break;
            case 11: rc = mr_appendText(mr, MR_PONDO, buf, cap, &len); break;
            case 12: rc = mr_appendText(mr, MR_HKD, buf, cap, &len); break;
            case 13: rc = mr_appendText(mr, MR_WON, buf, cap, &len); break;
            case 14: rc = mr_appendText(mr, MR_WON, buf, cap, &len); break;
            case 15: rc = mr_appendText(mr, (const char *)jajp_szKANA_MXN,
                                        buf, cap, &len); break;
            case 16: rc = mr_appendText(mr, MR_NOK, buf, cap, &len); break;
            case 17: rc = mr_appendText(mr, MR_NZD, buf, cap, &len); break;
            case 18: rc = mr_appendText(mr, MR_RUUBURU, buf, cap, &len); break;
            case 19: rc = mr_appendText(mr, MR_SEK, buf, cap, &len); break;
            case 20: rc = mr_appendText(mr, MR_SGD, buf, cap, &len); break;
            case 21: rc = mr_appendText(mr, MR_TRL, buf, cap, &len); break;
            case 22: rc = mr_appendText(mr, MR_TWD, buf, cap, &len); break;
            case 23: rc = mr_appendText(mr, MR_ONSU, buf, cap, &len); break;
            case 24: rc = mr_appendText(mr, MR_ONSU, buf, cap, &len); break;
            case 25: rc = mr_appendText(mr, MR_ONSU, buf, cap, &len); break;
            default: break;
            }
            if (rc != 0)
                return rc;

            if (split != 0) {
                rc = mr_appendMakeReadableNumber(mr, right, buf, cap, &len,
                                                 1);
                if (rc != 0)
                    return rc;
                if (unit == 1)
                    rc = mr_appendText(mr, MR_SEN, buf, cap, &len);
                else if (unit == 2)
                    rc = mr_appendText(mr, MR_SENTO, buf, cap, &len);
                else if (unit == 3)
                    rc = mr_appendText(mr, MR_SENTO, buf, cap, &len);
                if (rc != 0)
                    return rc;
            }

            if (inNumber == 0)
                unit = 0;
            sawPunct    = 0;
            num[0]      = NULL;
            num[1]      = NULL;
            numberReady = 0;
            if (state != 0)
                curLen = 0;
        }

        if (flushChar != 0) {
            if (curLen > 1 && state != 0) {
                rc = mr_appendTextN(mr, p, curLen, buf, cap, &len);
                curLen = 0;
            } else {
                rc = mr_appendChar(mr, p, buf, cap, &len);
            }
            if (rc != 0)
                return rc;
            flushChar = 0;
            inNumber  = 0;
            unit      = 0;
        }

        if (sawRange != 0) {
            rc = mr_appendText(mr, MR_KARA, buf, cap, &len);
            if (rc != 0)
                return rc;
            sawRange = 0;
        }

        p = next;
    }
    (*buf)[len] = '\0';
    return 0;
}

/* ---- a date --------------------------------------------------------- */

#define MR_NEN     "\x94\x4e"                  /* nen, the year */
#define MR_GATSU   "\x8c\x8e"                  /* gatsu, the month */
#define MR_NICHI   "\x93\xfa"                  /* nichi, the day */
#define MR_GETSUYO "\x8c\x8e\x97\x6a\x93\xfa"  /* getsuyoubi, Monday */
#define MR_KAYO    "\x89\xce\x97\x6a\x93\xfa"  /* kayoubi, Tuesday */
#define MR_SUIYO   "\x90\x85\x97\x6a\x93\xfa"  /* suiyoubi, Wednesday */
#define MR_MOKUYO  "\x96\xd8\x97\x6a\x93\xfa"  /* mokuyoubi, Thursday */
#define MR_KINYO   "\x8b\xe0\x97\x6a\x93\xfa"  /* kinyoubi, Friday */
#define MR_DOYO    "\x93\x79\x97\x6a\x93\xfa"  /* doyoubi, Saturday */
#define MR_NICHIYO "\x93\xfa\x97\x6a\x93\xfa"  /* nichiyoubi, Sunday */

/* A date, in whichever of ten orders the caller says it is written in.
 *
 * The caller's number is what says which: 0x20100 through 0x20b00, and
 * anything outside that is handed straight back. Below 0x20401 a date has
 * three parts, up to 0x20900 it has two, and above that one -- and which part
 * is the year, which the month and which the day is what the ten arms at the
 * end differ in. The year keeps its leading zeros and the month and the day
 * lose theirs, which is the trim flag on each.
 *
 * The walk is three states: reading the numbers, having just seen a day of
 * the week, and reading anything else. A day of the week in brackets after a
 * date is picked up and written after it with a comma either side; a day of
 * the week before one starts the date over.
 *
 * A date whose parts came out but whose format is none of the ten is written
 * back as the text it was, from the first part's start to the last part's
 * end. Note that the last part's end is reached as the entry before the one
 * the counter names, which for a counter of nought would read the length of
 * the answer as a pointer -- unreachable, since the counter is only ever read
 * after it has been stepped.
 */
int32_t mr_normalizeDate(void *mr, const char *text, uint32_t n, char **buf,
                         uint32_t *cap, int32_t flag)
{
    const char *parts[3][2];
    uint32_t    len = 0;
    const char *p = text;
    const char *end = text + n;
    const char *next;
    int32_t     rc = 0;
    int32_t     state;
    int32_t     partAt = 0;
    int32_t     wantParts;
    int32_t     dateFormat = 0;
    int32_t     dateReady = 0;
    int32_t     flushChar = 0;
    int32_t     sawDayOfWeek = 0;
    int32_t     dayOfWeek = 0;

    if (flag < 0x20100 || flag > 0x20b00)
        return mrl_copyAndReturn(mr, text, n, buf, cap);

    if (flag <= 0x20400)
        wantParts = 3;
    else if (flag <= 0x20900)
        wantParts = 2;
    else
        wantParts = 1;

    memset(parts, 0, sizeof parts);

    if (mr_isDigit(mr, p)) {
        state       = 0;
        parts[0][0] = p;
    } else if (mr_isDayOfWeek(mr, p)) {
        state = 1;
    } else {
        state = 2;
    }

    while (p < end) {
        next = ju_IsDBCSLeadByte(p[0]) ? p + 2 : p + 1;

        switch (state) {
        case 0:
            if (mr_isDigit(mr, next)) {
                if (parts[partAt][0] == NULL)
                    parts[partAt][0] = next;
                if (mr_isDateSeparator(mr, p) && partAt < wantParts - 1) {
                    parts[partAt][1] = p;
                    partAt++;
                    parts[partAt][0] = next;
                }
            } else if (mr_isDateSeparator(mr, next) && mr_isDigit(mr, p)
                       && partAt < wantParts - 1) {
                /* A separator between two numbers is neither read nor kept. */
            } else {
                if (mr_isDigit(mr, p))
                    parts[partAt][1] = next;
                else
                    parts[partAt][1] = p;
                partAt++;
                if (mr_isDigit(mr, p) && partAt == wantParts) {
                    dateFormat = flag;
                } else {
                    dateFormat = 0;
                    if (!mr_isDigit(mr, p))
                        flushChar = 1;
                }
                dateReady = 1;
                state     = 2;
            }
            break;

        case 1:
            dayOfWeek = mr_isDayOfWeek(mr, p);
            if (mr_isParenthesis(mr, next) == 2) {
                next = ju_IsValidDBCS(next) ? next + 2 : next + 1;
                if (mr_isDigit(mr, next)) {
                    state  = 0;
                    memset(parts, 0, sizeof parts);
                    partAt = 0;
                    parts[0][0] = next;
                } else {
                    state = 2;
                }
                sawDayOfWeek = 1;
            } else if (mr_isDigit(mr, next)) {
                state  = 0;
                memset(parts, 0, sizeof parts);
                partAt = 0;
                parts[0][0] = next;
            } else {
                state     = 2;
                flushChar = 1;
            }
            break;

        case 2:
            if (mr_isParenthesis(mr, p) == 1 && mr_isDayOfWeek(mr, next)) {
                if (ju_IsValidDBCS(next) && mr_isParenthesis(mr, next + 2))
                    state = 1;
                else
                    flushChar = 1;
            } else {
                if (mr_isDigit(mr, next)) {
                    state  = 0;
                    memset(parts, 0, sizeof parts);
                    partAt = 0;
                    parts[0][0] = next;
                }
                flushChar = 1;
            }
            break;

        default:
            break;
        }

        if (dateReady != 0) {
            /* Which part is the year, which the month and which the day.
               The year keeps its leading zeros; the others lose theirs. */
            if (dateFormat == 0x20100) {
                rc = mr_appendMakeReadableNumber(mr, parts[0], buf, cap, &len, 0);
                if (rc == 0) rc = mr_appendText(mr, MR_NEN, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[1], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_GATSU, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[2], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_NICHI, buf, cap, &len);
            } else if (dateFormat == 0x20200) {
                rc = mr_appendMakeReadableNumber(mr, parts[0], buf, cap, &len, 0);
                if (rc == 0) rc = mr_appendText(mr, MR_NEN, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[2], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_GATSU, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[1], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_NICHI, buf, cap, &len);
            } else if (dateFormat == 0x20300) {
                rc = mr_appendMakeReadableNumber(mr, parts[2], buf, cap, &len, 0);
                if (rc == 0) rc = mr_appendText(mr, MR_NEN, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[0], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_GATSU, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[1], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_NICHI, buf, cap, &len);
            } else if (dateFormat == 0x20400) {
                rc = mr_appendMakeReadableNumber(mr, parts[2], buf, cap, &len, 0);
                if (rc == 0) rc = mr_appendText(mr, MR_NEN, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[1], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_GATSU, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[0], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_NICHI, buf, cap, &len);
            } else if (dateFormat == 0x20600) {
                rc = mr_appendMakeReadableNumber(mr, parts[0], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_GATSU, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[1], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_NICHI, buf, cap, &len);
            } else if (dateFormat == 0x20700) {
                rc = mr_appendMakeReadableNumber(mr, parts[1], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_GATSU, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[0], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_NICHI, buf, cap, &len);
            } else if (dateFormat == 0x20800) {
                rc = mr_appendMakeReadableNumber(mr, parts[1], buf, cap, &len, 0);
                if (rc == 0) rc = mr_appendText(mr, MR_NEN, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[0], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_GATSU, buf, cap, &len);
            } else if (dateFormat == 0x20900) {
                rc = mr_appendMakeReadableNumber(mr, parts[0], buf, cap, &len, 0);
                if (rc == 0) rc = mr_appendText(mr, MR_NEN, buf, cap, &len);
                if (rc == 0) rc = mr_appendMakeReadableNumber(mr, parts[1], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_GATSU, buf, cap, &len);
            } else if (dateFormat == 0x20a00) {
                rc = mr_appendMakeReadableNumber(mr, parts[0], buf, cap, &len, 0);
                if (rc == 0) rc = mr_appendText(mr, MR_NEN, buf, cap, &len);
            } else if (dateFormat == 0x20b00) {
                rc = mr_appendMakeReadableNumber(mr, parts[0], buf, cap, &len, 1);
                if (rc == 0) rc = mr_appendText(mr, MR_GATSU, buf, cap, &len);
            } else {
                uint32_t span = (uint32_t)(((const char **)parts)[partAt * 2 - 1]
                                           - parts[0][0]);

                rc = mr_appendTextN(mr, parts[0][0], span, buf, cap, &len);
            }
            if (rc != 0)
                return rc;

            dateReady = 0;
            partAt    = 0;
            memset(parts, 0, sizeof parts);
        }

        if (sawDayOfWeek != 0) {
            rc = mr_appendText(mr, ",", buf, cap, &len);
            if (rc != 0)
                return rc;
            switch (dayOfWeek) {
            case 1: rc = mr_appendText(mr, MR_GETSUYO, buf, cap, &len); break;
            case 2: rc = mr_appendText(mr, MR_KAYO, buf, cap, &len); break;
            case 3: rc = mr_appendText(mr, MR_SUIYO, buf, cap, &len); break;
            case 4: rc = mr_appendText(mr, MR_MOKUYO, buf, cap, &len); break;
            case 5: rc = mr_appendText(mr, MR_KINYO, buf, cap, &len); break;
            case 6: rc = mr_appendText(mr, MR_DOYO, buf, cap, &len); break;
            case 7: rc = mr_appendText(mr, MR_NICHIYO, buf, cap, &len); break;
            default: break;
            }
            if (rc != 0)
                return rc;
            sawDayOfWeek = 0;
            rc = mr_appendText(mr, ",", buf, cap, &len);
            if (rc != 0)
                return rc;
        } else if (flushChar != 0) {
            rc = mr_appendChar(mr, p, buf, cap, &len);
            if (rc != 0)
                return rc;
            flushChar = 0;
        }

        p = next;
    }
    (*buf)[len] = '\0';
    return 0;
}

/* ---- a sound written as kana ----------------------------------------- */

/* The five bytes an entry of either half of the kana table takes. */
#define MR_KANA_STRIDE 5
#define MR_KANA_N      163
#define MR_KANA_SOKUON 160   /* `Q', the small tsu that doubles a consonant */

#define MR_PHONE(i) ((const char *)jajp_j_phones + (i) * MR_KANA_STRIDE)
#define MR_KANA(i)  ((const char *)jajp_j_kana + (i) * MR_KANA_STRIDE)

/* Only ever asked of a byte the caller has already refused a lead byte, and
   the C locale is the only one the engine ever runs in, where isalpha is the
   two runs of letters and nothing else. Written out rather than called so
   that a byte with its top bit set is a defined thing to ask about, which it
   is not when a signed char is handed to the library's own. */
#define MR_ISALPHA(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))

/* The porting layer's counted case-insensitive compare, which nothing else in
   the tree calls and no header therefore publishes. */
extern int ralStrNicmp(int flags, const char *a, const char *b, int n);

/* The consonants a doubled consonant may be made of, which is the whole of
   the plosives and fricatives as this converter spells them. */
#define MR_DOUBLES "CSgbhzstkdpf"

/* The synthesiser's own phoneme string, turned back into kana.
 *
 * This is the one method of the class that does not normalise anything: it
 * takes what the synthesiser was told to say and writes what a reader would
 * have written, which is what the caller shows a user. The answer opens with
 * a tag -- a space, up to sixteen letters of the text, a tilde -- and closes
 * with a space.
 *
 * A phoneme string is a run of groups separated by full stops, with a digit
 * or an apostrophe in a group meaning an accent rather than a sound; a `1' is
 * the accent, which comes out as a caret after the kana it fell on --
 * not the tilde that closes the tag, though the two sit together. Within a
 * group `N' is the moraic nasal and `S' the palatal fricative, spelt here as
 * `*' and `C' because that is how the table spells them, and every vowel is
 * followed by an `H' so that a long vowel is two entries rather than one.
 *
 * Reading a group is longest-match-first over three characters: for each
 * length from three down to one, the whole table is walked for an entry of
 * exactly that length that the group starts with. A case-insensitive match
 * counts too, but only when the character after it is an `H' -- that is what
 * lets an `AH' find the table's `a' and leave the `H' to be read as the long
 * mark on the next turn.
 *
 * Nothing matching leaves two ways out, both of them the small tsu: a single
 * consonant left over at the end of a group, or a consonant that a vowel
 * follows which no entry covers. Neither consumes the vowel.
 *
 * Note the group buffer. IBM gives it eighty bytes and tests the count
 * before appending rather than after, so a vowel arriving on the eightieth
 * writes two past the end -- over a pointer that is assigned again before it
 * is read, which is why nothing ever noticed. Ours has the room those two
 * bytes want, so the bytes written are the same and none of them is
 * somewhere else's.
 */
int32_t mr_convertSPR(void *mr, const char *text, uint32_t n, char **buf,
                      uint32_t *cap)
{
    char        tag[19];
    char        phon[96];
    char       *q;
    uint32_t    len = 0;
    const char *p = text;
    int32_t     rc = 0;
    int32_t     nph = 0;
    int32_t     left = 0;
    int32_t     take = 0;
    int32_t     tagLen = 0;
    int32_t     matched = 0;
    int32_t     wantAccent = 0;
    int32_t     overflow = 0;
    int32_t     flush = 0;
    uint32_t    i;
    char        c;

    if (*cap == 0 || text == NULL || n == 0)
        return 1;

    tag[tagLen++] = ' ';
    for (i = 0; i < n; i++) {
        c = text[i];
        if (ju_IsDBCSLeadByte(c)) {
            i++;
            continue;
        }
        if (MR_ISALPHA(c)) {
            tag[tagLen++] = c;
            if (tagLen > 0x10)
                break;
        }
    }
    tag[tagLen++] = '~';
    tag[tagLen] = '\0';
    rc = mr_appendText(mr, tag, buf, cap, &len);
    if (rc != 0)
        return rc;

    for (i = 0; i <= n; i++) {
        c = *p;

        if (i == n || c == '.') {
            flush = 1;
        } else if ((c >= '0' && c <= '9') || c == '\'') {
            if (c == '1')
                wantAccent = 1;
        } else {
            if (nph > 0x50) {
                overflow = 1;
                break;
            }
            if (c == 'N')
                c = '*';
            else if (c == 'S')
                c = 'C';
            phon[nph++] = c;
            if (strchr("AIUEO", c) != NULL)
                phon[nph++] = 'H';
        }

        if (flush != 0 && nph > 0) {
            phon[nph] = '\0';
            left = nph;
            q    = phon;

            while (left > 0) {
                int32_t k;

                matched = 0;
                take    = left > 3 ? 3 : left;

                for (k = take; k > 0; k--) {
                    int32_t e;

                    for (e = 0; e < MR_KANA_N; e++) {
                        size_t w = strlen(MR_PHONE(e));

                        if (w != (size_t)k)
                            continue;
                        if (strncmp(q, MR_PHONE(e), (size_t)k) != 0) {
                            if (ralStrNicmp(0, q, MR_PHONE(e), k) != 0)
                                continue;
                            if (q[strlen(MR_PHONE(e))] != 'H')
                                continue;
                        }
                        matched = 1;
                        rc = mr_appendText(mr, MR_KANA(e), buf, cap, &len);
                        if (rc != 0)
                            return rc;
                        if (wantAccent != 0) {
                            rc = mr_appendText(mr, "^", buf, cap, &len);
                            if (rc != 0)
                                return rc;
                            wantAccent = 0;
                        }
                        break;
                    }
                    if (matched != 0) {
                        if (k <= left) {
                            left -= k;
                            q    += k;
                        }
                        break;
                    }
                }
                if (matched != 0)
                    continue;

                /* Nothing read it, so it is the small tsu -- either the one
                   consonant a group ended on, or a consonant and vowel no
                   entry spells. */
                if (left == 1 && nph > 1 && strstr(MR_DOUBLES, q) != NULL) {
                    left--;
                    rc = mr_appendText(mr, MR_KANA(MR_KANA_SOKUON), buf, cap,
                                       &len);
                    if (rc != 0)
                        return rc;
                } else if (left >= 2 && strchr(MR_DOUBLES, q[0]) != NULL
                           && strchr("aiueo", q[1]) != NULL) {
                    left--;
                    q++;
                    rc = mr_appendText(mr, MR_KANA(MR_KANA_SOKUON), buf, cap,
                                       &len);
                    if (rc != 0)
                        return rc;
                } else {
                    break;
                }
            }
        }
        if (flush != 0) {
            nph   = 0;
            flush = 0;
        }
        p++;
    }
    if (overflow != 0)
        return 1;
    rc = mr_appendText(mr, " ", buf, cap, &len);
    if (rc != 0)
        return rc;
    (*buf)[len] = '\0';
    return 0;
}
