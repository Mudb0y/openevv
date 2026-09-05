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
    char *got = (char *)malloc(want);

    (void)mr;
    if (got == NULL)
        return 1;
    memcpy(got, *buf, used);
    free(*buf);
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
        char *got = (char *)malloc(n + 1);

        if (got == NULL)
            return 1;
        free(*buf);
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
                rc = mr_appendText(mr, " ", buf, cap, &len);
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
