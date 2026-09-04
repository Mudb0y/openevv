/* How a number becomes words.
 *
 * TextAnalysis reaches a phrase whose words are digits and punctuation and
 * hands it here. What comes back is the reading: not one string but up to
 * eight of them, because a long number is read as several accent phrases and
 * each of those is a reading of its own.
 *
 * The work is in three passes and the record has a region for each.
 * `SegmentYomiBlock' cuts the digits into substrings -- runs that are read
 * one way -- and `SetYomiType' decides which of eight ways each is read.
 * `GenerateStdForm' then rewrites each substring into a standard form, and
 * `ApplySRule' walks that form and writes the codes of the reading out,
 * taking one of four roads by the substring's kind: by place for an ordinary
 * number, digit by digit for something read as a string of figures, and two
 * more for a decimal and for a fraction. `ApplyJRule' is the last of it: the
 * word after a number is often a counter, and how the number joins it
 * depends on both.
 *
 * Almost nothing here is computed. The scales, the readings of each digit,
 * the counter rules and the characters a code stands for are all tables of
 * IBM's, lifted into lang/jajp by tools/rom/tables.py and reached through
 * DictMan's accessors. What this file does is walk them.
 *
 * The record is IBM's and rom/jajp/numread.h is the map, checked against the
 * object by `tools/rom/offsets.py numread'.
 *
 * Held to IBM's answer by test/harness/romprims.sh.
 */

#include <stdint.h>
#include "jprom.h"
#include "numread.h"
#include "phrasebuf.h"
#include "dictsearch.h"
#include "txtanal.h"
#include "romanizer.h"
#include "rom_tables_jajp.h"

/* The owner, which is parked past the record because IBM keeps it at nought
   with the readings starting at four. */
#define NR_OWNER_OF(nr)  (*(void **)NR_P((nr), NR_OWNER_AT))
#define TA_AT(ta, which) (*(void **)((uint8_t *)(ta) + (which)))
#define TA_L(ta, off)    (*(int32_t *)((uint8_t *)(ta) + (off)))

/* IBM's own two tables, under IBM's own names.
 *
 * m_sanTCodes is twenty-six characters and SINDX is twenty entries, and the
 * two are indexed by the same thing: a code of the standard form. SINDX is
 * read at the code and at the code plus one, so a code of nineteen or more
 * reads past it -- which is safe only because `GenerateStdForm' never puts
 * one there. The codes it writes for an ordinary number run from nought to
 * eighteen, and the five it writes for the other kinds are read by nothing
 * that touches SINDX. */
#define nr_sanTCodes  jajp_m_sanTCodes
#define nr_sindx      jajp_SINDX

/* One word of the phrase this is handed, and the phrase itself. Both are
   PhraseBuf's record and rom/jajp/phrasebuf.h is the map. */
#define WP_B(w, off)  (*((const uint8_t *)(w) + (off)))
#define WW_B(ww, off) (*((const uint8_t *)(ww) + (off)))

/* The romanizer, which two of the passes ask about the caller's settings. */
#define NR_ROM_OF(nr) TA_AT(NR_OWNER_OF(nr), TA_OWNER_AT)
#define RZ_L(rz, off) (*(int32_t *)((uint8_t *)(rz) + (off)))
#define RZ_U16(rz, off) (*(uint16_t *)((uint8_t *)(rz) + (off)))


/* ---- the pair a reading is adjusting ------------------------------- */

/* A reading holds five pairs of numbers, and the rules adjust the one whose
 * index is the count of pairs in hand less one. A reading with none yet is
 * therefore indexed at minus one, and IBM indexes it there rather than
 * refusing: two bytes in front of the reading, and two bytes into it.
 *
 * For every reading but the first that is harmless and exactly reproducible.
 * The two bytes in front of reading n are the last of reading n minus one,
 * which both engines have and hold the same, and the two bytes at the front
 * of the pair array are the reading's own count and length. So all of that
 * is done here as IBM does it.
 *
 * The first reading is the exception, and it is IBM's own defect. The two
 * bytes in front of it are the second half of the owner pointer: IBM reads
 * an address as a number, writes it back as how many codes the reading
 * holds, and `ApplySRuleToKetaYomi' then writes a hundred and thirty-one
 * codes into a field of twenty-two. The record goes and so does whatever
 * follows it -- the harness driving IBM's own object stops with its own loop
 * counter smashed. A number whose reading needs a pair before it has one
 * reaches that.
 *
 * There is nothing to reproduce in an address, so ours reads nought in front
 * of the first reading and does not write there. That is a deliberate
 * divergence and docs/status.md lists it with the others; everything else
 * about the minus-one index is IBM's, to the byte.
 */
static int16_t rd_a(const void *nr, const uint8_t *rd, int16_t i)
{
    if (i < 0 && rd == NR_READ_AT(nr, 0))
        return 0;
    return *(const int16_t *)(rd + RD_A + i * RD_PAIR_SIZE);
}

static int16_t rd_b(const void *nr, const uint8_t *rd, int16_t i)
{
    (void)nr;
    return *(const int16_t *)(rd + RD_B + i * RD_PAIR_SIZE);
}

static void rd_set_a(const void *nr, uint8_t *rd, int16_t i, int16_t v)
{
    if (i < 0 && rd == NR_READ_AT(nr, 0))
        return;
    *(int16_t *)(rd + RD_A + i * RD_PAIR_SIZE) = v;
}

static void rd_set_b(const void *nr, uint8_t *rd, int16_t i, int16_t v)
{
    (void)nr;
    *(int16_t *)(rd + RD_B + i * RD_PAIR_SIZE) = v;
}

/* ---- clearing it out ------------------------------------------------- */

/* What stands in for a constructor. The second half of the substring array
   is the one the passes write, so that is what this clears, and the readings
   with it. Thirty-one of each thirty-two byte run rather than all of them,
   which is IBM's own bound and is left as it stands: nothing reads the last
   byte of either without having written it first. */
void nr_Init(void *nr)
{
    int32_t i, j;

    for (i = 0; i < NR_SUBSTR_HALF; i++) {
        uint8_t *ss = NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i);

        SS_B8(ss, SS_KIND)  = 0;
        SS_B8(ss, SS_COUNT) = 0;
        SS_S16(ss, SS_FROM) = 0;
        SS_S16(ss, SS_TO)   = 0;
        for (j = 0; j < SS_CLEARED; j++) {
            SS_B8(ss, SS_CODES + j) = 0;
            SS_B8(ss, SS_MORE + j)  = 0;
        }
    }

    for (i = 0; i < NR_READ_N; i++) {
        uint8_t *rd = NR_READ_AT(nr, i);

        RD_B8(rd, RD_COUNT) = 0;
        RD_B8(rd, RD_LEN)   = 0;
        for (j = 0; j < RD_PAIR_N; j++) {
            rd_set_a(nr, rd, j, 0);
            rd_set_b(nr, rd, j, 0);
        }
        for (j = 0; j < RD_CODES_N; j++)
            RD_B8(rd, RD_CODES + j) = 0;
    }
}

/* ---- the four roads through a reading -------------------------------- */

/* A fraction: the reading gets the three codes that say "over", and the
   substring after this one says how much of what follows belongs to it. */
int16_t nr_ApplySRuleToBunsu(void *nr, int16_t n, int16_t at, int16_t *got,
                             const uint8_t *ss)
{
    uint8_t       *rd   = NR_READ_AT(nr, n);
    const uint8_t *next = ss + NR_SUBSTR_SIZE;
    int16_t        k    = got[n];
    int16_t        more;

    rd_set_a(nr, rd, k - 1, (int16_t)(rd_a(nr, rd, k - 1) + 3));
    rd_set_b(nr, rd, k - 1, 0);
    RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) + 0) = 0xca;
    RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) + 1) = 0xfe;
    RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) + 2) = 0x64;
    RD_B8(rd, RD_COUNT) = (uint8_t)(RD_B8(rd, RD_COUNT) + 3);

    if (SS_B8(next, SS_COUNT) == 0) {
        more = 0;
    } else {
        more = 1;
        RD_B8(rd, RD_LEN) = (uint8_t)(SS_B8(ss, SS_MORE + 1) - at);
    }
    return (int16_t)(n + more);
}

/* A decimal point. Which of four ways the reading in hand is fixed up before
   the point goes on depends on the last code of the substring before this
   one, and the length comes from this one, which is why the method takes
   both. Two of the four ways only fire when the caller has not asked for
   figures to be spelt out. */
int16_t nr_ApplySRuleToShosu(void *nr, int16_t n, int16_t at, int16_t *got,
                             const uint8_t *prev, const uint8_t *ss)
{
    uint8_t *rd = NR_READ_AT(nr, n);
    uint8_t  c  = nr_sanTCodes[SS_B8(prev, SS_CODES
                                     + SS_B8(prev, SS_COUNT) - 1)];
    int16_t  k;

    switch (c) {
    case '0':
        if (RZ_L(NR_ROM_OF(nr), RZ_SPELL_ENGLISH) == 0) {
            RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) - 2) = 0x83;
            RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) - 1) = 0xf3;
        }
        k = got[n];
        rd_set_a(nr, rd, k - 1, (int16_t)(rd_a(nr, rd, k - 1) + 2));
        break;
    case '2':
    case '5':
        RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT)) =
            (uint8_t)((RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) - 1) & 7)
                      + 0xf8);
        RD_B8(rd, RD_COUNT)++;
        k = got[n];
        rd_set_a(nr, rd, k - 1, (int16_t)(rd_a(nr, rd, k - 1) + 3));
        break;
    case '1':
    case '8':
    case 'a':
        if (RZ_L(NR_ROM_OF(nr), RZ_SPELL_ENGLISH) == 0)
            RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) - 1) = 0xfd;
        k = got[n];
        rd_set_a(nr, rd, k - 1, (int16_t)(rd_a(nr, rd, k - 1) + 2));
        break;
    default:
        k = got[n];
        rd_set_a(nr, rd, k - 1, (int16_t)(rd_a(nr, rd, k - 1) + 2));
        break;
    }

    k = got[n];
    rd_set_b(nr, rd, k - 1, (int16_t)(rd_a(nr, rd, k - 1) - 3));
    RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) + 0) = 0x23;
    RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) + 1) = 0xfe;
    RD_B8(rd, RD_COUNT) = (uint8_t)(RD_B8(rd, RD_COUNT) + 2);
    RD_B8(rd, RD_LEN) = (uint8_t)(SS_B8(ss, SS_MORE + 1) - at);
    return (int16_t)(n + 1);
}

/* One entry of the reading table copied onto the end of a reading's codes.
 *
 * IBM asks GetNumYomiPtrAt afresh for every byte it copies, and for the
 * loop's own bound besides. The table is a base and a stride of six and
 * nothing writes it, so the pointer is taken once here; the bytes written are
 * the same either way. */
static void nr_append(uint8_t *rd, uint8_t code)
{
    const uint8_t *ym = dm_GetNumYomiPtrAt(code);
    int8_t         k;

    for (k = 0; k < (int8_t)ym[0]; k++)
        RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) + k) = ym[1 + k];
    RD_B8(rd, RD_COUNT) = (uint8_t)(RD_B8(rd, RD_COUNT) + ym[0]);
}

/* A code's own place in the reading table, which is the last entry of the
   range SINDX gives it. */
static int8_t nr_place(uint8_t code)
{
    return (int8_t)(nr_sindx[code + 1] - 1);
}

/* And the same for a digit that has another beside it. Two of the places
   stand for a pair read together and are moved aside to entries of their
   own -- which the digit left over at the end of an odd count does not do,
   so the two readings really are different and the pairing is what makes
   them so. */
static int8_t nr_pair_place(uint8_t code)
{
    int8_t at = nr_place(code);

    if (at == 0x06)
        return 0x39;
    if (at == 0x12)
        return 0x3a;
    return at;
}

/* Digit by digit, two at a time. Each pair of codes names two entries of the
   reading table and both go on the end of the codes together; a count that is
   odd leaves one digit over for the tail. Two readings are as many as one
   accent phrase takes, so a third closes the one in hand and starts the
   next. */
int16_t nr_ApplySRuleToBouYomi(void *nr, int16_t n, int16_t at, int16_t *got,
                               const uint8_t *ss)
{
    int8_t count = (int8_t)SS_B8(ss, SS_COUNT);
    int8_t i;

    for (i = 0; i < count / 2; i++) {
        uint8_t *rd;
        int8_t   a = nr_pair_place(SS_B8(ss, SS_CODES + i * 2));
        int8_t   b = nr_pair_place(SS_B8(ss, SS_CODES + i * 2 + 1));

        /* IBM indexes SS_MORE two behind the pair it is on, which at the
           first pair reaches back into SS_TO. The byte offset is written out
           rather than dressed up as an index, because that is what it is. */
        if (got[n] == 2) {
            RD_B8(NR_READ_AT(nr, n), RD_LEN) =
                (uint8_t)(SS_B8(ss, SS_MORE - 2 + i * 2) - at);
            at = SS_B8(ss, SS_MORE - 2 + i * 2);
            n++;
        }
        rd = NR_READ_AT(nr, n);

        rd_set_a(nr, rd, got[n],
            (int16_t)((int8_t)dm_GetNumYomiPtrAt((uint8_t)a)[0]
                      + (int8_t)dm_GetNumYomiPtrAt((uint8_t)b)[0]));
        rd_set_b(nr, rd, got[n], (int16_t)(rd_a(nr, rd, got[n]) - 1));
        got[n]++;

        nr_append(rd, (uint8_t)a);
        nr_append(rd, (uint8_t)b);
    }

    if (count % 2 != 0) {
        uint8_t *rd;
        int8_t   a = nr_place(SS_B8(ss, SS_CODES + count - 1));

        if (got[n] == 2) {
            RD_B8(NR_READ_AT(nr, n), RD_LEN) =
                (uint8_t)(SS_B8(ss, SS_MORE + count - 1) - at);
            at = SS_B8(ss, SS_MORE + count - 1);
            n++;
        }
        rd = NR_READ_AT(nr, n);

        rd_set_a(nr, rd, got[n],
            (int16_t)(int8_t)dm_GetNumYomiPtrAt((uint8_t)a)[0]);
        rd_set_b(nr, rd, got[n],
            (int16_t)(int8_t)dm_GetNumYomiPtrAt((uint8_t)a)[5]);
        got[n]++;
        nr_append(rd, (uint8_t)a);
    }
    return n;
}

/* By place: the ordinary way a number is read.
 *
 * The codes are walked in pairs, because a digit and the scale after it are
 * often one entry of the reading table rather than two -- that is what the
 * inner loop asks GetNumMDAt, over every entry of the range SINDX gives the
 * digit's code. What comes out is a place in the reading table, and the four
 * roads below differ only in what they put in the reading's B field before
 * the reading itself goes on the end.
 *
 * The three named places are the myriad markers: ten thousand, a hundred
 * million and a million million, which is where Japanese groups digits
 * rather than every three. `ss[1 + j]' is IBM's own arithmetic, one behind
 * the pair being read, and is written out as the offset it is.
 */
int16_t nr_ApplySRuleToKetaYomi(void *nr, int16_t which, int16_t n,
                                int16_t at, int16_t *got, const uint8_t *ss)
{
    int8_t count = (int8_t)SS_B8(ss, SS_COUNT);
    int8_t scale = 0;
    int8_t prev  = 0;
    int16_t i;

    if (count == 0)
        n++;

    for (i = 0; i < count; ) {
        int16_t  j = i;
        int8_t   matched = 0;
        int8_t   place;
        int8_t   advance;
        uint8_t *rd;
        uint8_t  c;
        int8_t   k;

        if (j < count - 1)
            for (k = (int8_t)nr_sindx[SS_B8(ss, SS_CODES + j)];
                 k < (int8_t)(nr_sindx[SS_B8(ss, SS_CODES + j) + 1] - 1);
                 k++)
                if (dm_GetNumMDAt((uint16_t)(k * 2 + 1))
                    == nr_sanTCodes[SS_B8(ss, SS_CODES + j + 1)]) {
                    matched = 1;
                    i = (int16_t)(i + 2);
                    prev  = scale;
                    scale = k;
                    break;
                }

        if (!matched) {
            prev  = scale;
            scale = (int8_t)(nr_sindx[SS_B8(ss, SS_CODES + j) + 1] - 1);
            i++;
        }

        place = (int8_t)(got[n] - 1);

        if (scale == 0x26) {
            if (SS_B8(ss, 0x01 + j) == 0x0e
                || SS_B8(ss, 0x01 + j) == 0x0f)
                continue;
            rd = NR_READ_AT(nr, n);
            rd_set_a(nr, rd, place,
                (int16_t)(rd_a(nr, rd, place)
                          + (int8_t)dm_GetNumYomiPtrAt((uint8_t)scale)[0]));
            c = nr_sanTCodes[SS_B8(ss, 0x01 + j)];
            if (c == 'x' || c == 'y' || c == 'z')
                rd_set_b(nr, rd, place, 1);
            else
                rd_set_b(nr, rd, place, (int16_t)(rd_a(nr, rd, place) - 1));
            nr_append(rd, (uint8_t)scale);
            if (i > count - 1) {
                advance = 0;
            } else {
                advance = 1;
                RD_B8(rd, RD_LEN) = (uint8_t)(SS_B8(ss, SS_MORE + j) - at);
                at = SS_B8(ss, SS_MORE + j);
            }
            n = (int16_t)(n + advance);
            continue;
        }

        if (scale == 0x27) {
            if (SS_B8(ss, 0x01 + j) == 0x0f)
                continue;
            rd = NR_READ_AT(nr, n);
            rd_set_a(nr, rd, place,
                (int16_t)(rd_a(nr, rd, place)
                          + (int8_t)dm_GetNumYomiPtrAt((uint8_t)scale)[0]));
            c = nr_sanTCodes[SS_B8(ss, 0x01 + j)];
            if ((c >= '3' && c <= '4') || c == '9' || c == 'a' || c == 'c')
                rd_set_b(nr, rd, place, (int16_t)(rd_a(nr, rd, place) - 3));
            else if (c >= 'x' && c <= 'z')
                rd_set_b(nr, rd, place, 1);
            else
                rd_set_b(nr, rd, place, (int16_t)(rd_a(nr, rd, place) - 2));
            nr_append(rd, (uint8_t)scale);
            if (i < count - 1) {
                advance = 1;
                RD_B8(rd, RD_LEN) = (uint8_t)(SS_B8(ss, SS_MORE + j) - at);
                at = SS_B8(ss, SS_MORE + j);
            } else {
                advance = 0;
            }
            n = (int16_t)(n + advance);
            continue;
        }

        if (scale == 0x28) {
            rd = NR_READ_AT(nr, n);
            rd_set_a(nr, rd, place,
                (int16_t)(rd_a(nr, rd, place)
                          + (int8_t)dm_GetNumYomiPtrAt((uint8_t)scale)[0]));
            c = nr_sanTCodes[SS_B8(ss, 0x01 + j)];
            if (c == '1' || c == '8' || c == 'a') {
                RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) - 1) = 0xfd;
                rd_set_b(nr, rd, place, (int16_t)(rd_a(nr, rd, place) - 3));
            } else if (c == '9' || (c >= '3' && c <= '4') || c == 'c') {
                rd_set_b(nr, rd, place, (int16_t)(rd_a(nr, rd, place) - 3));
            } else if (c >= 'x' && c <= 'z') {
                rd_set_b(nr, rd, place, 1);
            } else {
                rd_set_b(nr, rd, place, (int16_t)(rd_a(nr, rd, place) - 2));
            }
            nr_append(rd, (uint8_t)scale);
            if (i < count - 2) {
                advance = 1;
                RD_B8(rd, RD_LEN) = (uint8_t)(SS_B8(ss, SS_MORE + j) - at);
                at = SS_B8(ss, SS_MORE + j);
            } else {
                advance = 0;
            }
            n = (int16_t)(n + advance);
            continue;
        }

        /* Everything else. Three cases: a place word standing on its own
           after a substring that was one too, a digit that follows one of
           the four scales that take a joined reading, and the plain one. */
        rd = NR_READ_AT(nr, n);
        if (which > 0 && prev == 0
            && SS_B8(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + which - 1), SS_KIND)
               == 5) {
            if (scale == 0x0b)
                scale = 0x35;
            else if (scale == 0x0e)
                scale = 0x36;
            else if (scale == 0x17)
                scale = 0x37;
            else if (scale == 0x1a)
                scale = 0x38;
            rd_set_a(nr, rd, place,
                (int16_t)(rd_a(nr, rd, place)
                          + (int8_t)dm_GetNumYomiPtrAt((uint8_t)scale)[0]));
            nr_append(rd, (uint8_t)scale);
            continue;
        }

        if ((prev == 0x0f || prev == 0x13 || prev == 0x1b || prev == 0x23)
            && (scale == 0x02 || scale == 0x06 || scale == 0x0a
                || scale == 0x0e || scale == 0x12 || scale == 0x16
                || scale == 0x1a || scale == 0x1e || scale == 0x22)) {
            if (scale == 0x0a)
                rd_set_b(nr, rd, place,
                    (int16_t)(int8_t)dm_GetNumYomiPtrAt((uint8_t)scale)[5]);
            else
                rd_set_b(nr, rd, place,
                    (int16_t)(rd_a(nr, rd, place)
                              + (int8_t)dm_GetNumYomiPtrAt(
                                    (uint8_t)scale)[5]));
            rd_set_a(nr, rd, place,
                (int16_t)(rd_a(nr, rd, place)
                          + (int8_t)dm_GetNumYomiPtrAt((uint8_t)scale)[0]));
            nr_append(rd, (uint8_t)scale);
            continue;
        }

        rd_set_a(nr, rd, got[n],
            (int16_t)(int8_t)dm_GetNumYomiPtrAt((uint8_t)scale)[0]);
        rd_set_b(nr, rd, got[n],
            (int16_t)(int8_t)dm_GetNumYomiPtrAt((uint8_t)scale)[5]);
        got[n]++;
        nr_append(rd, (uint8_t)scale);
    }
    return n;
}

/* ---- walking the substrings ----------------------------------------- */

/* Every substring turned into a reading, taking one of eight roads by the
 * kind SetYomiType gave it. Four are the methods above; three write a code or
 * two straight onto the reading in hand and close it off; one only counts.
 *
 * `nBig' is how many substrings are read as something more than a scale
 * word, and the fraction road wants it to be exactly one -- a fraction has
 * a numerator and a denominator and nothing else.
 */
int16_t nr_ApplySRule(void *nr, int16_t howmany, int16_t *got)
{
    int16_t at = 0;
    int16_t n  = 0;
    int16_t big = 0;
    int16_t i;

    for (i = 0; i < howmany; i++)
        if (SS_B8(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i), SS_KIND) >= 2)
            big++;

    for (i = 0; i < howmany; i++) {
        const uint8_t *ss = NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i);
        uint8_t       *rd;
        int16_t        k;

        switch (SS_B8(ss, SS_KIND)) {
        case 0:
            n = nr_ApplySRuleToKetaYomi(nr, i, n, at, got, ss);
            break;

        case 1:
            n = nr_ApplySRuleToBouYomi(nr, n, at, got, ss);
            break;

        case 2:
            if (i > 0) {
                n = nr_ApplySRuleToShosu(
                        nr, n, at, got,
                        NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i - 1), ss);
                at = SS_B8(ss, SS_MORE + 1);
            }
            break;

        case 3:
            if (i > 0 && i < howmany - 1 && big == 1)
                n = nr_ApplySRuleToBunsu(nr, n, at, got, ss);
            break;

        case 4:
            /* A sign or a symbol between two numbers: two codes and a fresh
               reading after it. */
            if (i > 0 && i < howmany - 1) {
                rd = NR_READ_AT(nr, n);
                k  = (int16_t)(got[n] - 1);
                rd_set_a(nr, rd, k, (int16_t)(rd_a(nr, rd, k) + 2));
                RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) + 0) = 0x00;
                RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) + 1) = 0x80;
                RD_B8(rd, RD_COUNT) = (uint8_t)(RD_B8(rd, RD_COUNT) + 2);
                RD_B8(rd, RD_LEN) = (uint8_t)(SS_B8(ss, SS_MORE) - at);
                at = SS_B8(ss, SS_MORE);
                n++;
            }
            break;

        case 5:
            /* A scale word standing alone. What the substring before it
               ended with decides whether the reading it joins is shortened,
               and two of those endings rewrite the last codes. IBM reads
               that character twice into two slots of its own; it cannot
               differ between the two reads. */
            if (i > 0 && i < howmany - 1) {
                const uint8_t *prev =
                    NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i - 1);
                uint8_t c = nr_sanTCodes[SS_B8(prev, 0x01
                                               + SS_B8(prev, SS_COUNT))];

                rd = NR_READ_AT(nr, n);
                k  = (int16_t)(got[n] - 1);
                if (c == '3' || c == '9' || c == 'a' || c == 'c') {
                    rd_set_b(nr, rd, k, (int16_t)(rd_a(nr, rd, k) - 1));
                } else {
                    if (c == '4') {
                        RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) - 2) = 0x19;
                        RD_B8(rd, RD_COUNT)--;
                        rd_set_a(nr, rd, k, (int16_t)(rd_a(nr, rd, k) - 1));
                    } else if (c == '7') {
                        RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) - 2) = 0x19;
                        RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) - 1) = 0x29;
                    }
                    rd_set_b(nr, rd, k, rd_a(nr, rd, k));
                }
            }
            break;

        case 6:
            n++;
            break;

        case 7:
            /* A separator: one code and a fresh reading. */
            if (i > 0 && i < howmany - 1) {
                rd = NR_READ_AT(nr, n);
                k  = (int16_t)(got[n] - 1);
                rd_set_a(nr, rd, k, (int16_t)(rd_a(nr, rd, k) + 1));
                RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT)) = 0x64;
                RD_B8(rd, RD_COUNT)++;
                RD_B8(rd, RD_LEN) = (uint8_t)(SS_B8(ss, SS_MORE) - at);
                at = SS_B8(ss, SS_MORE);
                n++;
            }
            break;

        default:
            break;
        }
    }

    /* And how long the last reading runs, which is where the last substring
       said it ends. */
    {
        const uint8_t *last =
            NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + howmany - 1);

        RD_B8(NR_READ_AT(nr, n), RD_LEN) =
            (uint8_t)(SS_B8(last, SS_MORE + SS_B8(last, SS_COUNT)) - at);
    }
    return n;
}

/* ---- deciding how each substring is read ---------------------------- */

/* Which of the eight roads each substring takes.
 *
 * Mostly the first digit of the substring says it outright. The interesting
 * case is a scale word with a number on either side of it: whether that is
 * read as a scale or as a separator depends on whether the numbers touching
 * it are single digits and on what follows, and getting it wrong turns a
 * date into a sum. A substring the walk gives up on ends the number there,
 * and the digit count is cut back to match.
 *
 * The last thing it does only fires for exactly three substrings: a decimal
 * in the middle of three has the two either side of it swapped, so that the
 * whole part is read before the point rather than after it.
 */
int16_t nr_SetYomiType(void *nr, int16_t howmany)
{
    void   *rz   = NR_ROM_OF(nr);
    int16_t mode = (int16_t)RZ_U16(rz, RZ_NUMBER_MODE);
    int8_t  cut  = 0;
    int16_t last = 0;
    int16_t markers = 0;
    int16_t i;

    if (mode == 0)
        mode = (RZ_L(rz, RZ_SPELL_ENGLISH) > 0) ? 2 : 0;

    for (i = 0; i < howmany; i++) {
        const uint8_t *ss = NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i);
        uint8_t        c  = NR_DIGIT_AT(nr, SS_S16(ss, SS_FROM));

        if (c == 0x18 || c == 0x1b)
            markers++;
    }

    for (i = 0; i < howmany; i++) {
        uint8_t *ss   = NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i);
        uint8_t *back = NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i - 1);
        uint8_t  c;

        SS_B8(ss, SS_KIND) = (uint8_t)((mode == 2) ? 1 : 0);

        if (i > 1 && (SS_B8(back, SS_KIND) == 2
                      || SS_B8(back, SS_KIND) == 7)) {
            SS_B8(ss, SS_KIND) = 1;
            continue;
        }

        c = NR_DIGIT_AT(nr, SS_S16(ss, SS_FROM));
        if (c == 0x00) {
            SS_B8(ss, SS_KIND) = 1;
            continue;
        }
        if (c == 0x13 || c == 0x14) {
            SS_B8(ss, SS_KIND) = 2;
            continue;
        }
        if (c == 0x15) {
            SS_B8(ss, SS_KIND) = 3;
            continue;
        }
        if (c == 0x16) {
            SS_B8(ss, SS_KIND) = 4;
            continue;
        }
        if (c == 0x17 || c == 0x1a) {
            if (SS_B8(back, SS_KIND) == 0)
                SS_B8(back, SS_KIND) = 1;
            SS_B8(ss, SS_KIND) = 7;
            continue;
        }
        if (c != 0x18 && c != 0x1b)
            continue;

        /* A scale word with something on both sides of it. */
        if (markers == 1) {
            const uint8_t *fwd = NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i + 1);

            if (NR_DIGIT_AT(nr, SS_S16(fwd, SS_FROM))
                - NR_DIGIT_AT(nr, SS_S16(back, SS_TO)) != 1)
                SS_B8(ss, SS_KIND) = 6;
            else if (SS_S16(back, SS_FROM) == SS_S16(back, SS_TO))
                SS_B8(ss, SS_KIND) = 5;
            else if (SS_S16(fwd, SS_FROM) == SS_S16(fwd, SS_TO))
                SS_B8(ss, SS_KIND) = 5;
            else if (NR_DIGIT_AT(nr, SS_S16(fwd, SS_FROM) + 1) >= 0x0d
                     && NR_DIGIT_AT(nr, SS_S16(fwd, SS_FROM) + 1) <= 0x0f)
                SS_B8(ss, SS_KIND) = 5;
            else
                SS_B8(ss, SS_KIND) = 6;

            if (SS_B8(ss, SS_KIND) == 5)
                continue;
        }

        /* The number stops here. */
        cut  = 1;
        last = (int16_t)(i + 1);
    }

    if (cut == 1) {
        howmany = (int16_t)(last + 1);
        NR_B(nr, NR_COUNT) =
            (uint8_t)(SS_S16(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + howmany),
                             SS_TO) + 1);
    }

    if (howmany == 3)
        for (i = 1; i < howmany - 1; i++)
            if (SS_B8(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i), SS_KIND) == 3) {
                uint8_t  keep[NR_SUBSTR_SIZE];
                uint8_t *a = NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i - 1);
                uint8_t *b = NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i + 1);
                int16_t  k;

                for (k = 0; k < NR_SUBSTR_SIZE; k++)
                    keep[k] = a[k];
                for (k = 0; k < NR_SUBSTR_SIZE; k++)
                    a[k] = b[k];
                for (k = 0; k < NR_SUBSTR_SIZE; k++)
                    b[k] = keep[k];
            }

    return howmany;
}

/* ---- driving the whole of it ---------------------------------------- */

/* One number read. The word number and where in the output it belongs are
   both taken and given back, because a number may swallow the counter word
   after it. */
int16_t nr_Do(void *nr, const uint8_t *w, int16_t *pWord, int16_t *pOut)
{
    int16_t word = *pWord;
    int16_t out  = *pOut;
    int16_t got[NR_READINGS_MOST];
    int16_t n = 0;
    int16_t howmany;
    int16_t i;

    nr_Init(nr);
    /* IBM keeps and clears eight of these and lets `ApplySRule' index past
       them; rom/jajp/numread.h says why there are more here and all of them
       are cleared. */
    for (i = 0; i < NR_READINGS_MOST; i++)
        got[i] = 0;

    howmany = nr_SegmentYomiBlock(nr, w, word);
    howmany = nr_SetYomiType(nr, howmany);
    nr_GenerateStdForm(nr, howmany);
    n = nr_ApplySRule(nr, howmany, got);
    word++;
    out++;

    if (word < (int16_t)WP_B(w, WP_WORDS)) {
        i = nr_ApplyJRule(nr, w, word, n, howmany, got);
        word = (int16_t)(word + i);
        out  = (int16_t)(out + i);
    }
    n++;

    for (i = 0; i < NR_ANSWER_N; i++)
        NR_ANSWER_AT(nr, i) = got[i];
    *pWord = word;
    *pOut  = out;
    return n;
}

/* ---- cutting the digits up ------------------------------------------ */

/* The digits of the word, and the substrings they fall into.
 *
 * Three passes. First the reading is copied out of the word -- or out of the
 * spine's long-reading store, if it was too long for the ten bytes a word
 * holds. Then it is cut at every scale word that cannot be part of the
 * number, which is what the walk backwards decides: Japanese groups digits
 * in fours, so a scale word arriving at the wrong distance from the end ends
 * the number rather than continuing it. Last, each of those blocks is cut
 * again at every scale word inside it that starts a group of its own, and
 * the answers go into the second half of the array where the later passes
 * look for them.
 *
 * The clamp at the end is IBM's and reads a digit as a count: a number that
 * cuts into more than twenty blocks is cut back to twenty, and the byte the
 * clamped index then names is nineteen bytes into the digits rather than in
 * the substring array at all. It is reproduced because it is what the
 * original does.
 */
int16_t nr_SegmentYomiBlock(void *nr, const uint8_t *w, int16_t word)
{
    const uint8_t *ww = WW_SLOT(w, word);
    uint8_t       *count = NR_P(nr, NR_COUNT);
    int8_t         marker = 0;
    int8_t         cut = 0;
    int16_t        i, n, b, out, p;

    n = 0;
    if (WW_B(ww, WW_KANALEN) > 9) {
        /* Too long for the word: the spine holds it, and the first byte of
           the word's own reading is which slot. */
        const uint8_t *ta = NR_OWNER_OF(nr);
        int16_t        slot = WW_B(ww, WW_KANA);

        for (i = 0; i < WW_B(ww, WW_KANALEN); i++) {
            count[1 + n] = ta[TA_LONGWORD + slot * TA_LONGWORD_SIZE + i];
            n++;
        }
    } else {
        for (i = 0; i < WW_B(ww, WW_KANALEN); i++) {
            count[1 + n] = WW_B(ww, WW_KANA + i);
            n++;
        }
    }
    count[0] = WW_B(ww, WW_KANALEN);

    for (i = 0; i < count[0]; i++)
        if (count[1 + i] == 0x18 || count[1 + i] == 0x1b) {
            marker = 1;
            break;
        }

    if (marker == 1) {
        int8_t run = -1;

        n = 0;
        for (i = (int16_t)(count[0] - 1); i >= 0; i--) {
            if (count[1 + i] <= 9) {
                n++;
                continue;
            }
            if (count[1 + i] != 0x18 && count[1 + i] != 0x1b) {
                cut = 1;
                break;
            }
            if (count[1 + i] == 0x1b) {
                if (run < 0) {
                    run = 1;
                } else if (run == 0) {
                    cut = 1;
                    break;
                }
            } else {
                if (run < 0) {
                    run = 0;
                } else if (run == 1) {
                    cut = 1;
                    break;
                }
            }
            if ((n + 1) % 4 != 0)
                cut = 1;
            else
                n = 0;
        }
    }

    b = 0;
    SS_S16(NR_SUBSTR_AT(nr, b), SS_FROM) = 0;
    for (i = 0; i < count[0]; i++) {
        uint8_t c = count[1 + i];

        if (c < 0x13 || c > 0x1a || c == 0x1b || c == 0x18)
            continue;
        SS_S16(NR_SUBSTR_AT(nr, b), SS_TO) = (int16_t)(i - 1);
        SS_S16(NR_SUBSTR_AT(nr, b + 1), SS_FROM) = i;
        SS_S16(NR_SUBSTR_AT(nr, b + 1), SS_TO) = i;
        if (i + 1 < count[0]) {
            SS_S16(NR_SUBSTR_AT(nr, b + 2), SS_FROM) = (int16_t)(i + 1);
            b = (int16_t)(b + 2);
        } else {
            b++;
        }
    }
    SS_S16(NR_SUBSTR_AT(nr, b), SS_TO) = (int16_t)(count[0] - 1);
    b++;

    out = 0;
    for (p = 0; p < b; p++) {
        const uint8_t *in = NR_SUBSTR_AT(nr, p);

        SS_S16(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + out), SS_FROM) =
            SS_S16(in, SS_FROM);

        for (i = SS_S16(in, SS_FROM); i <= SS_S16(in, SS_TO); i++) {
            int8_t here = 0;

            if (count[1 + i] != 0x18 && count[1 + i] != 0x1b)
                continue;

            if ((SS_S16(in, SS_TO) - i + 1) % 4 != 0) {
                here = 1;
            } else {
                int16_t k = SS_S16(in, SS_FROM);
                int16_t m = 0;

                /* Either one ends it: a scale before this marker, or four
                   digits since the last one. */
                for (; k < i; k++, m++) {
                    if (count[1 + k] == 0x18 || count[1 + k] == 0x1b) {
                        m = -1;
                        continue;
                    }
                    if (count[1 + k] > 9 || m >= 3) {
                        here = 1;
                        break;
                    }
                }
                for (k = (int16_t)(i + 1);
                     k <= SS_S16(in, SS_TO); k++) {
                    if (count[1 + k] == 0x18 || count[1 + k] == 0x1b)
                        continue;
                    if (count[1 + k] > 9) {
                        here = 1;
                        break;
                    }
                }
            }

            if (here == 1 || cut == 1) {
                SS_S16(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + out), SS_TO) =
                    (int16_t)(i - 1);
                SS_S16(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + out + 1),
                       SS_FROM) = i;
                SS_S16(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + out + 1),
                       SS_TO) = i;
                SS_S16(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + out + 2),
                       SS_FROM) = (int16_t)(i + 1);
                out = (int16_t)(out + 2);
            }
        }

        SS_S16(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + out), SS_TO) =
            SS_S16(in, SS_TO);
        out++;
    }

    if (out > NR_SUBSTR_HALF) {
        out = NR_SUBSTR_HALF;
        count[0] = (uint8_t)SS_S16(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + out),
                                   SS_TO);
    }
    return out;
}

/* ---- the standard form ---------------------------------------------- */

/* Each substring rewritten into the form the reading rules walk.
 *
 * For every kind but the ordinary number this is a code or two. For an
 * ordinary number it is the whole of the difficulty: the digits are walked
 * backwards, because what a digit is read as depends on how far it is from
 * the end, and a scale word is put in front of each group of four rather
 * than left where it was written. A zero contributes nothing but a place,
 * and a one in front of a scale is dropped -- Japanese says the scale alone.
 * The form is built backwards in a buffer and copied out forwards.
 *
 * IBM reads one and two bytes in front of that buffer while deciding whether
 * to drop a leading one. At the first byte the two roads write the same thing
 * either way, so what it finds cannot matter; at the second they do not, and
 * an out-of-range read is taken here as not matching. Both are guarded rather
 * than reproduced, because the bytes in front of the buffer are MSVC's own
 * padding and hold nothing either engine can agree on.
 */
void nr_GenerateStdForm(void *nr, int16_t howmany)
{
    int16_t i;

    for (i = 0; i < howmany; i++) {
        uint8_t *ss = NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + i);
        uint8_t  form[SS_CODES_N];
        uint8_t  where[SS_CODES_N];
        int16_t  n = 0;
        int16_t  m = 0;
        int16_t  group = 0;
        int16_t  scale = 0;
        int16_t  k;

        for (k = 0; k < SS_CLEARED; k++)
            form[k] = 0;

        switch (SS_B8(ss, SS_KIND)) {
        case 2:
            SS_B8(ss, SS_CODES + n) = 0x13;
            SS_B8(ss, SS_MORE + n)  = (uint8_t)SS_S16(ss, SS_FROM);
            n++;
            break;
        case 3:
            SS_B8(ss, SS_CODES + n) = 0x15;
            SS_B8(ss, SS_MORE + n)  = (uint8_t)SS_S16(ss, SS_FROM);
            n++;
            break;
        case 4:
            SS_B8(ss, SS_CODES + n) = 0x16;
            SS_B8(ss, SS_MORE + n)  = (uint8_t)SS_S16(ss, SS_FROM);
            n++;
            break;
        case 7:
            SS_B8(ss, SS_CODES + n) = 0x17;
            SS_B8(ss, SS_MORE + n)  = (uint8_t)SS_S16(ss, SS_FROM);
            n++;
            break;
        case 5:
            SS_B8(ss, SS_CODES + n) = 0x18;
            SS_B8(ss, SS_MORE + n)  = (uint8_t)SS_S16(ss, SS_FROM);
            n++;
            break;

        case 1:
            for (k = SS_S16(ss, SS_FROM); k <= SS_S16(ss, SS_TO); k++) {
                if (NR_DIGIT_AT(nr, k) > 9)
                    continue;
                SS_B8(ss, SS_CODES + n) = NR_DIGIT_AT(nr, k);
                SS_B8(ss, SS_MORE + n)  = (uint8_t)k;
                n++;
            }
            break;

        case 0:
            /* A single nought is read as itself and nothing else. */
            if (SS_S16(ss, SS_TO) == SS_S16(ss, SS_FROM)
                && NR_DIGIT_AT(nr, SS_S16(ss, SS_TO)) == 0) {
                n++;
                SS_B8(ss, SS_CODES + n) =
                    NR_DIGIT_AT(nr, SS_S16(ss, SS_TO));
                SS_B8(ss, SS_MORE + n) = (uint8_t)SS_S16(ss, SS_TO);
                break;
            }

            for (k = SS_S16(ss, SS_TO); k >= SS_S16(ss, SS_FROM); k--) {
                uint8_t c = NR_DIGIT_AT(nr, k);

                if (c == 0) {
                    if (group == 0 && k < SS_S16(ss, SS_TO)
                        && NR_DIGIT_AT(nr, k + 1) < 0x0a) {
                        form[m]  = (uint8_t)(scale + 0x0c);
                        where[m] = (uint8_t)k;
                        m++;
                    }
                    if (group != 0 && m != 0
                        && form[m - 1] >= 0x0a && form[m - 1] <= 0x0c)
                        m--;
                    group = (int16_t)((group + 1) % 4);
                    if (group == 0)
                        scale = (int16_t)((scale + 1) % 4);
                    continue;
                }

                if (c >= 1 && c <= 9) {
                    if (k < SS_S16(ss, SS_TO)
                        && (NR_DIGIT_AT(nr, k + 1) < 0x0a
                            || NR_DIGIT_AT(nr, k + 1) == 0x18
                            || NR_DIGIT_AT(nr, k + 1) == 0x1b)) {
                        /* Another digit or a myriad marker after it, so no
                           scale of its own follows and the place this digit
                           stands in has to be said. A digit that is last, or
                           that has its own scale written after it, needs
                           nothing added. */
                        if (group == 0)
                            form[m] = (uint8_t)(scale + 0x0c);
                        else
                            form[m] = (uint8_t)(group + 0x09);
                        form[m + 1]  = NR_DIGIT_AT(nr, k);
                        where[m]     = (uint8_t)k;
                        where[m + 1] = (uint8_t)k;
                        m = (int16_t)(m + 2);
                    } else {
                        form[m]  = NR_DIGIT_AT(nr, k);
                        where[m] = (uint8_t)k;
                        m++;
                    }
                    group = (int16_t)((group + 1) % 4);
                    if (group == 0)
                        scale = (int16_t)((scale + 1) % 4);
                    continue;
                }

                if (c >= 0x0a && c <= 0x0c) {
                    form[m]  = c;
                    where[m] = (uint8_t)k;
                    m++;
                    group = (int16_t)(c - 9);
                    scale = 0;
                    continue;
                }

                if (c >= 0x0d && c <= 0x0f) {
                    form[m]  = c;
                    where[m] = (uint8_t)k;
                    m++;
                    group = 0;
                    scale = (int16_t)(c - 0x0c);
                    continue;
                }

                if (c >= 0x10 && c <= 0x12) {
                    form[m]  = c;
                    where[m] = (uint8_t)k;
                    m++;
                }
            }

            for (k = (int16_t)(m - 1); k >= 0; k--) {
                int32_t keep = 1;

                if (form[k] == 1 && k >= 1
                    && form[k - 1] >= 0x0a && form[k - 1] <= 0x0c)
                    keep = (k == 0)
                           || (form[k - 1] == 0x0c && k >= 2
                               && form[k - 2] >= 0x0d
                               && form[k - 2] <= 0x0f);
                if (keep) {
                    SS_B8(ss, SS_CODES + n) = form[k];
                    SS_B8(ss, SS_MORE + n)  =
                        (uint8_t)((int8_t)where[k] + 1);
                    n++;
                }
            }
            break;

        default:
            break;
        }

        SS_B8(ss, SS_COUNT) = (uint8_t)n;
        SS_B8(ss, SS_MORE + n) = (uint8_t)(SS_S16(ss, SS_TO) + 1);
    }
}

/* ---- joining the counter word --------------------------------------- */

/* The word after a number, when that word is a counter.
 *
 * Japanese counters change both themselves and the number in front of them,
 * and which change is IBM's table rather than a rule: three rows of the
 * number data, indexed by the counter's own place in the JCC table and by
 * the last code of the number. The first row rewrites the end of the number
 * and may shorten it, the second rewrites the counter's own first kana, and
 * the third says where the accent falls. The counter is copied into
 * DictSearch's three-record area first, because that is where the rest of
 * the analyser expects a word it has taken over.
 *
 * A word whose type group does not say it is a counter is left alone, which
 * is the nought this answers; anything it does take is one word, which is
 * the one.
 */
int16_t nr_ApplyJRule(void *nr, const uint8_t *w, int16_t word, int16_t n,
                      int16_t howmany, int16_t *got)
{
    const uint8_t *nd = dm_GetNumberDataPtr();
    const uint8_t *ww = WW_SLOT(w, word);
    void          *d;
    uint8_t       *rec;
    uint8_t       *rd;
    const uint8_t *ss;
    int16_t        place, last, prev, rule1, rule2, rule3;
    int16_t        row, col, keep, jcc;
    int16_t        i;

    if (!(dm_GetTGAt2(WW_B(ww, WW_POS), 3) & 0x10))
        return 0;

    d   = TA_AT(NR_OWNER_OF(nr), TA_DICTSEARCH_AT);
    rec = (uint8_t *)d + DS_REC;
    *(int16_t *)(rec + 0) = *(const int16_t *)(ww + WW_ACCENT);
    rec[2] = WW_B(ww, WW_KANALEN);
    rec[3] = WW_B(ww, WW_CHARS);
    for (i = 0; i < (int16_t)WW_B(ww, WW_KANALEN); i++)
        rec[6 + i] = WW_B(ww, WW_KANA + i);

    rd    = NR_READ_AT(nr, n);
    place = (int16_t)(got[n] - 1);
    RD_B8(rd, RD_LEN) = (uint8_t)(rec[3] + RD_B8(rd, RD_LEN));

    ss   = NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + howmany - 1);
    last = SS_B8(ss, 0x01 + SS_B8(ss, SS_COUNT));
    prev = (SS_B8(ss, SS_COUNT) > 1)
           ? SS_B8(ss, 0x00 + SS_B8(ss, SS_COUNT)) : 0;

    jcc = (int16_t)dm_GetNumJCCAt(
              (uint16_t)(*(int16_t *)(rec + 0) * 4 - 3));
    rule1 = nd[0xa7 + (jcc - 'a') * 0x13 + last];

    /* What the number's own end becomes. */
    if (SS_B8(ss, SS_KIND) != 1 && SS_B8(ss, SS_KIND) != 7) {
        if (rule1 == 2 || rule1 == 4 || rule1 == 8) {
            int32_t shorten = 1;

            if (rule1 != 8 && howmany > 2
                && SS_B8(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + howmany - 2),
                         SS_KIND) == 5)
                shorten = 0;
            if (shorten && RD_B8(rd, RD_COUNT) > 1) {
                rd_set_a(nr, rd, place, (int16_t)(rd_a(nr, rd, place) - 1));
                rd_set_b(nr, rd, place, rd_a(nr, rd, place));
                RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) - 2) =
                    nd[0x7d + rule1 * 4];
                RD_B8(rd, RD_COUNT) = (uint8_t)(RD_B8(rd, RD_COUNT) - 1);
            }
            if (rule1 == 8 && howmany >= 3
                && SS_B8(NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + howmany - 2),
                         SS_KIND) == 5) {
                const uint8_t *two =
                    NR_SUBSTR_AT(nr, NR_SUBSTR_HALF + howmany - 3);

                if (nr_sanTCodes[SS_B8(two, 0x01 + SS_B8(two, SS_COUNT))]
                        == '8'
                    && RD_B8(rd, RD_COUNT) > 1)
                    RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) - 2) = 0xfd;
            }
        } else if (rule1 == 1 || rule1 == 3 || rule1 == 5 || rule1 == 6
                   || rule1 == 7 || rule1 == 9 || rule1 == 10) {
            int8_t howlong = (int8_t)nd[0x7b + rule1 * 4];

            rd_set_b(nr, rd, place, (int16_t)(int8_t)nd[0x7c + rule1 * 4]);
            for (i = 0; i < howlong; i++)
                RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) - howlong + i) =
                    nd[0x7b + rule1 * 4 + 2 + i];
        }
    }

    jcc = (int16_t)dm_GetNumJCCAt(
              (uint16_t)(*(int16_t *)(rec + 0) * 4 - 2));
    rule2 = nd[0x178 + (jcc - 'a') * 0x13 + last];

    /* And what the counter's own first kana becomes. Its code is a row and a
       column of eight, which is why the arms below work on the row and the
       write puts the two back together. */
    keep = 0;
    if (SS_B8(ss, SS_KIND) != 1 && SS_B8(ss, SS_KIND) != 7) {
        row = (int16_t)(rec[6] / 8);
        col = (int16_t)(rec[6] % 8);

        if (rule2 == 1) {
            if (row <= 2 && row >= 0)
                row = (int16_t)(row + 0x14);
            else if (row == 3 || row == 5)
                row = (int16_t)((col == 1) ? 0x16 : 0x17);
            else if (row == 4)
                row = (int16_t)(row + 0x14);
            else if (row == 6)
                row = 0x16;
            else if (row == 9 || row == 0x0b || row == 0x13)
                row = 0x19;
            else if (row == 0x0a)
                row = 0x1a;
            RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT)) =
                (uint8_t)(col + row * 8);
            keep = 1;
        } else if (rule2 == 2) {
            if (row == 9 || row == 0x0b || row == 0x13)
                row = 7;
            else if (row == 0x0a)
                row = 8;
            RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT)) =
                (uint8_t)(col + row * 8);
            keep = 1;
        } else if (rule2 == 3) {
            /* The counter loses its first kana outright. */
            rec[6] = 0;
            if (rec[2] >= 3)
                for (i = 2; i < (int16_t)rec[2]; i++)
                    rec[5 + i] = rec[6 + i];
            rec[2] = (uint8_t)(rec[2] - 1);
        }
    }

    /* The counter's kana onto the end of the reading. */
    if ((int16_t)rec[2] >= keep)
        for (i = keep; i < (int16_t)rec[2]; i++)
            RD_B8(rd, RD_CODES + RD_B8(rd, RD_COUNT) + i) = rec[6 + i];
    rd_set_a(nr, rd, place, (int16_t)(rd_a(nr, rd, place) + rec[2]));
    RD_B8(rd, RD_COUNT) = (uint8_t)(RD_B8(rd, RD_COUNT) + rec[2]);

    jcc = (int16_t)dm_GetNumJCCAt(
              (uint16_t)(*(int16_t *)(rec + 0) * 4 - 1));
    if (jcc < 0x68)
        rule3 = nd[0x1fd + (jcc - 'a') * 0x13 + last];
    else
        rule3 = nd[0x282 + (jcc - 0x68) * 0x13 + last];

    /* And where the accent falls. */
    if (rule3 == 0
        || (rule3 > 3 && rule3 < 7 && prev > 1 && prev < 10)
        || SS_B8(ss, SS_KIND) == 1) {
        uint8_t c = dm_GetNumJCCAt(
                        (uint16_t)(*(int16_t *)(rec + 0) * 4 - 4));

        if (c == '*') {
            rd_set_b(nr, rd, place, 0);
        } else if (c == '0') {
            uint8_t e = nr_sanTCodes[SS_B8(ss, 0x01
                                           + SS_B8(ss, SS_COUNT))];
            int32_t back = 0;

            if ((e == '3' || e == '4' || e == '9' || e == 'a' || e == 'c'
                 || e == 'x' || e == 'y')
                && !(rule1 == 2 || rule1 == 4 || rule1 == 8))
                back = 1;
            else if ((e == '1' || e == '6' || e == '8' || e == 'b')
                     && (rule1 == 1 || rule1 == 5 || rule1 == 7
                         || rule1 == 10))
                back = 1;
            rd_set_b(nr, rd, place,
                (int16_t)(rd_a(nr, rd, place) - rec[2] - (back ? 1 : 0)));
        } else if (c == '1') {
            rd_set_b(nr, rd, place,
                (int16_t)(rd_a(nr, rd, place) - rec[2] + 1));
        } else {
            rd_set_b(nr, rd, place,
                (int16_t)(rd_a(nr, rd, place) - rec[2] + c - 0x30));
        }
        return 1;
    }

    if (rule3 == 1
        || (rule3 == 6 && !(prev >= 2 && prev <= 9))
        || (rule3 == 7 && rule2 == 3))
        rd_set_b(nr, rd, place, 0);
    else if (rule3 == 2
             || (rule3 == 4 && !(prev >= 2 && prev <= 9)))
        rd_set_b(nr, rd, place, (int16_t)(rd_a(nr, rd, place) - rec[2] + 1));
    else if (rule3 == 3
             || (rule3 == 5 && !(prev >= 2 && prev <= 9))
             || (rule3 == 7 && rule2 != 3))
        rd_set_b(nr, rd, place, rd_a(nr, rd, place));
    return 1;
}
