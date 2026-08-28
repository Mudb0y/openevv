/* The dictionary search, as far as it is written.
 *
 * This is the class the rest of the analyser leans on: it turns a stretch of
 * the input into candidate words with readings, and everything above it
 * chooses between what it produced. IBM spreads it over four objects --
 * dictsearch, dictapi, fdictapi and kanastr -- and sixty-four methods, and
 * three of them are here.
 *
 * The layout is IBM's rather than ours, which is a departure from the other
 * files in this directory. Two reasons, and both are about being able to prove
 * a piece at a time. A class this size arrives half-written for a long while,
 * and a half-written one has to work with state built by hand; keeping IBM's
 * offsets means test/romprims.c can build that state the same way on both
 * sides and compare, instead of maintaining two descriptions of the same
 * bytes. And the record is only partly understood -- rom/jajp/dictsearch.h
 * says which parts -- so a tidy struct would have to invent names for fields
 * nobody has read yet.
 *
 * Everything here is held to IBM's own answer by test/romprims.sh.
 */

#include <string.h>
#include "jprom.h"
#include "dictsearch.h"

/* Reaching a field. The block is bytes and these say how to read one, at the
   offsets rom/jajp/dictsearch.h works out. */
#define DS_AT(d, off)      ((uint8_t *)(d) + (off))
#define DS_B(d, off)       (*(uint8_t *)DS_AT(d, off))
#define DS_W(d, off)       (*(int16_t *)DS_AT(d, off))
#define DS_P(d, off)       (*(void **)DS_AT(d, off))

#define DS_INPUT(d)        ((uint8_t *)DS_P(d, DS_INPUTCHAR))

/* And into the input reader, whose own file will name these properly. */
#define IC_CHAR(in, i)     ((char *)((in) + IC_TEXT + (i) * 2))
#define IC_KIND_AT(in, i)  (*(int32_t *)((in) + IC_KIND + (i) * 4))
#define IC_COUNT_AT(in)    (*(int16_t *)((in) + IC_COUNT))

/* What a character is, as InputChar classes it. Only the four the text walk
   cares about are named; the rest end a run. */
#define KIND_KANJI      1
#define KIND_HIRAGANA   4
#define KIND_KATAKANA   8
#define KIND_CHOON      9

/* The particle wo, which marks the object of a verb. That is what the name
   means -- a case marker in the grammatical sense, not a typographic one -- and
   a run of text stops at it because what follows it is a new word.
 *
 * Read out of the object rather than decoded from the name it is stored
 * under: 0x82f0 is wo, and the mangled form `?$IC?p' reads as 0x8270 to
 * anybody working the encoding out by hand, which is a different character
 * altogether. test/romprims.sh is what caught that. */
static const char CASE_MARKER[] = "\x82\xf0";

/* How many characters one lookup may take, and how many the kana buffers
   hold. */
#define TEXT_MOST       5

/* ---- the three that are written ------------------------------------- */

/* Whether the character at `at' is the case marker. */
int32_t ds_CheckCaseMarker(void *d, int16_t at)
{
    return ju_DbCmp(IC_CHAR(DS_INPUT(d), at), CASE_MARKER) ? 1 : 0;
}

/* A long-vowel mark after a vowel becomes the vowel doubled.
 *
 * The codes are read as a row and a column -- over eight and the remainder --
 * and a mark is row 0x1f. It doubles when the two are in the same column, and
 * for two pairs of columns that sound the same: three after one, and four
 * after two. Nothing happens when what came before is itself row 0x1e. */
void ds_CheckCnvChoon(void *d, uint8_t code, uint8_t *next)
{
    int16_t before;
    int16_t mark;

    (void)d;
    if (code / 8 == 0x1e)
        return;
    if (*next / 8 != 0x1f)
        return;

    mark = (int16_t)(*next % 8);
    before = (int16_t)(code % 8);

    if (before == mark) {
        *next = (uint8_t)(mark + 0xf0);
        return;
    }
    if ((before == 3 && mark == 1) || (before == 4 && mark == 2))
        *next = (uint8_t)(before + 0xf0);
}

/* Copy the run of text starting at `from' into the lookup buffer.
 *
 * It takes kanji, hiragana and one long-vowel mark, stops at anything else,
 * and never takes more than five characters. A case marker ends it -- and if
 * the very first character is one, there is nothing to look up and it answers
 * nought.
 *
 * The choon count is what allows exactly one: the second time one arrives, the
 * test for it has already been satisfied once, so the run ends there.
 *
 * Answers one when there is something worth looking up, which means at least
 * two characters, and writes where the run began and ended. */
int32_t ds_GetTextBuf(void *d, int16_t from)
{
    uint8_t *in = DS_INPUT(d);
    int16_t  at = from;
    int16_t  n = 0;
    int16_t  choon = 0;

    for (; at < IC_COUNT_AT(in) && n < TEXT_MOST; at++) {
        int32_t kind = IC_KIND_AT(in, at);

        if (!((kind == KIND_CHOON && choon == 0)
              || kind == KIND_HIRAGANA
              || kind == KIND_KANJI))
            break;

        if (ju_DbCmp(IC_CHAR(in, at), CASE_MARKER)) {
            if (at == from)
                return 0;
            break;
        }

        ju_DbCpy((char *)DS_AT(d, DS_TEXT + n * 2), IC_CHAR(in, at));
        n++;
        if (IC_KIND_AT(in, at) == KIND_CHOON)
            choon++;
    }

    DS_W(d, DS_COPIED) = n;
    if (n <= 1)
        return 0;

    DS_B(d, DS_TEXT + n * 2) = 0;
    DS_W(d, DS_FROM) = from;
    DS_W(d, DS_TO) = (int16_t)(from + n);
    return 1;
}
