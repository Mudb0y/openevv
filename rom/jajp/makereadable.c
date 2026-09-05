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
