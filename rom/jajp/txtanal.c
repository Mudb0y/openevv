/* The spine of the Japanese analyser.
 *
 * `TextAnalysis' is the object every other class in the romanizer is handed a
 * reference to, and `rom/jajp/txtanal.h' is its record -- mapped before any of
 * this was written, because nothing below it could be built until it was. This
 * file is the code: twenty-one of the class's methods are in `txtanal.obj',
 * five more in `kakutei.obj', four in `comppenalty.obj', four in `unknown.obj'
 * and three in `jpnrom.obj' beside `Romanizer'.
 *
 * They go in from the leaves up, which is what the differential sweep wants:
 * a method whose callees are all written can be held against IBM's own on its
 * own fixtures, and one whose callees are not cannot.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "jprom.h"
#include "txtanal.h"
#include "inputchar.h"
#include "phrasebuf.h"
#include "phrasetable.h"
#include "jpath.h"
#include "dictsearch.h"
#include "textnormalizer.h"
#include "intonphrase.h"
#include "romanizer.h"

#define TA_AT(ta, which) (*(void **)((uint8_t *)(ta) + (which)))
#define TA_W(ta, off)    (*(int16_t *)((uint8_t *)(ta) + (off)))
#define WP_B(w, off)     (*((uint8_t *)(w) + (off)))

/* ---- the two that only ask something else ---------------------------- */

/* The text the reader is holding, thrown away. */
void ta_ClearInputBuf(void *ta)
{
    ic_Init(TA_AT(ta, TA_INPUTCHAR_AT));
}

/* Whether the reader has run out of text. */
int32_t ta_IsEndOfInput(void *ta)
{
    return *(int32_t *)((uint8_t *)TA_AT(ta, TA_INPUTCHAR_AT) + IC_AT_END);
}

/* ---- the free list over the phrase table ----------------------------- */

/* The chain that says which rows of the phrase table are free, laid out over
   however many rows the caller names. It is circular at both ends rather than
   ending in a stop: the first entry's back link is the count and the last
   entry's forward link is the count as well, which is one past the last row
   there is. Beside it go the four numbers the walk starts from -- the first
   and the last, which are both the count as given, a nought, and the count
   less one. */
void ta_InitPhraseTable(void *ta, int16_t n)
{
    uint8_t *link = (uint8_t *)ta + TA_LINK;
    int16_t  i;

    *(int16_t *)(link + 0) = n;
    *(int16_t *)(link + 2) = 1;
    for (i = 1; i < (int16_t)(n - 1); i++) {
        *(int16_t *)(link + (size_t)i * TA_LINK_SIZE + 0) = (int16_t)(i - 1);
        *(int16_t *)(link + (size_t)i * TA_LINK_SIZE + 2) = (int16_t)(i + 1);
    }
    *(int16_t *)(link + (size_t)(n - 1) * TA_LINK_SIZE + 0) = (int16_t)(n - 2);
    *(int16_t *)(link + (size_t)(n - 1) * TA_LINK_SIZE + 2) = n;

    TA_W(ta, TA_FIRST)    = n;
    TA_W(ta, TA_LAST)     = n;
    TA_W(ta, TA_SPARE_18) = 0;
    TA_W(ta, TA_TOP)      = (int16_t)(n - 1);
}

/* And the whole table given back: the phrase table's own chain of filled rows
   forgotten and every row put back on the free list. */
void ta_ClearPhraseTable(void *ta)
{
    void *pt = TA_AT(ta, TA_PHRASETABLE_AT);

    *(void **)((uint8_t *)pt + PTB_HEAD_AT) = NULL;
    *(void **)((uint8_t *)pt + PTB_TAIL_AT) = NULL;
    ta_InitPhraseTable(ta, TA_LINK_N);
}

/* ---- where a line may not be broken ---------------------------------- */

/* Japanese typesetting will not let certain characters begin or end a line,
   and the analyser borrows the word for the same idea: minus one is this
   phrase refusing the break the caller is asking about.
 *
 * Two things refuse. A phrase with no function word on it whose first word
 * carries the tag bit refuses a break of three kinds, and a phrase whose last
 * function word has its top bit set refuses a break of two of them. Note the
 * two tests are asked one after the other rather than as arms of one choice,
 * so a phrase can be asked both -- though not answer both, since the first
 * wants no function word and the second wants one.
 */
int16_t ta_Kinsoku(void *ta, int16_t slot, int16_t kind)
{
    uint8_t *wp = WP_SLOT((uint8_t *)TA_AT(ta, TA_PHRASEBUF_AT) + PB_BUFFER,
                          slot);
    int16_t  rc = 0;

    if (WP_B(wp, WP_FZKS) == 0) {
        if (kind == 2 || kind == 5 || kind == 4) {
            if (dm_GetTGAt2(*(WW_SLOT(wp, 0) + WW_POS), 3) & 0x08)
                rc = -1;
        }
    }
    if (WP_B(wp, WP_FZKS) != 0) {
        int16_t last = (int16_t)(WP_B(wp, WP_FZKS) - 1);

        if (WF_SLOT(wp, last)[WF_CODE] & 0x80) {
            if (kind == 5 || kind == 4)
                rc = -1;
        }
    }
    return rc;
}

/* ---- where to cut a phrase that ran too long ------------------------- */

/* A stretch of words that came out longer than a row of the phrase table
   holds has to be broken somewhere, and this says where. It walks the words
   from one bound to the other and scores each join by the tags of the word in
   front of it and the word behind, best first: a five is a join between two
   words that say nothing about each other and is the best place to cut.
 *
 * Two answers are kept as it goes. One is the join with the best score, and
   the other is the join nearest the middle -- the one where the moras read so
   far come closest to half of what the whole stretch runs to. Which of the
   two is given back depends on whether the best score reached three: below
   that no join is good enough to be worth the shape of the phrase, and the
   middle wins.
 *
 * The walk also stops early once it has read more than sixteen moras, since
   past that the front of the stretch is already a phrase's worth.
 *
 * Note the word behind is read at one past the walk's own bound, so the last
   join asked about is with a word the caller did not name.
 */
int16_t ta_SearchJrtSeparate(void *ta, void *wp, int16_t from, int16_t to,
                             int16_t moras)
{
    int16_t score = 0;
    int16_t gap   = 0;
    int16_t best  = 0;
    int16_t half  = (int16_t)(moras / 2);
    int16_t run   = 0;
    int16_t at    = -1;
    int16_t mid   = (int16_t)(to - 1);
    int16_t i;

    (void)ta;
    for (i = from; i < to; i++) {
        uint8_t a = dm_GetTGAt2(*(WW_SLOT(wp, i) + WW_POS), 2);
        uint8_t b = dm_GetTGAt2(*(WW_SLOT(wp, i + 1) + WW_POS), 2);
        int32_t hi, lo;

        if ((a & 0x01) && (b & 0x02))
            score = 4;
        else if ((a & 0x20) && (b & 0x02))
            score = 3;
        else if (((a & 0x20) && (b & 0x20)) || ((a & 0x01) && (b & 0x01)))
            score = 2;
        else if (((a & 0x20) && (b & 0x01)) || ((a & 0x02) && (b & 0x20)))
            score = 1;
        else
            score = 5;

        run = (int16_t)(run + WW_SLOT(wp, i)[WW_KANALEN]);
        if (run > 0x10)
            return best < 3 ? mid : at;

        if (best <= score) {
            best = score;
            at   = i;
        }
        hi  = run > moras / 2 ? run : moras / 2;
        lo  = run < moras / 2 ? run : moras / 2;
        gap = (int16_t)(hi - lo);
        if (half >= gap) {
            half = gap;
            mid  = i;
        }
    }
    return best < 3 ? mid : at;
}

/* ---- the table overflowing ------------------------------------------- */

/* What happens when the phrase table has no room left: the sentence is cut
   short where the last row that fits ends, the reader is wound back to that
   place so the rest is read again as the next sentence, and every row after
   it is given back to the free list.
 *
 * The caller may name the row to cut at or hand a null, in which case the
   walk down the chain from the head finds the last row but one. Where the row
   named is the last of the chain there is nothing behind it to take a place
   from, and the place the caller gave is used instead.
 *
 * The row is left saying it is of the fourth kind and ending the chain, and
   the table's tail is set to it.
 */
void ta_HandleOverflow(void *ta, void *row, int32_t at)
{
    void *ic = TA_AT(ta, TA_INPUTCHAR_AT);
    void *pt = TA_AT(ta, TA_PHRASETABLE_AT);
    void *p;
    void *next;

    *(int32_t *)((uint8_t *)ic + IC_ENDED) = 1;

    if (row == NULL) {
        p = *(void **)((uint8_t *)pt + PTB_HEAD_AT);
        while (PT_NEXT_OF(PT_NEXT_OF(p)) != NULL)
            p = PT_NEXT_OF(p);
        row = p;
        *(int32_t *)((uint8_t *)ic + IC_POS) =
            *(int32_t *)((uint8_t *)PT_NEXT_OF(row) + PT_COST);
    } else if (PT_NEXT_OF(row) == NULL) {
        *(int32_t *)((uint8_t *)ic + IC_POS) = at;
    } else {
        *(int32_t *)((uint8_t *)ic + IC_POS) =
            *(int32_t *)((uint8_t *)PT_NEXT_OF(row) + PT_COST);
    }

    for (p = PT_NEXT_OF(row); p != NULL; p = next) {
        uint16_t which;

        next  = PT_NEXT_OF(p);
        which = (uint16_t)*(int16_t *)((uint8_t *)p + PT_INDEX);
        ju_TableFree((uint16_t *)((uint8_t *)ta + TA_LAST),
                     (uint16_t *)((uint8_t *)ta + TA_SPARE_18),
                     (uint16_t *)((uint8_t *)ta + TA_TOP),
                     (uint8_t *)ta + TA_LINK, TA_LINK_N, which);
    }

    PT_NEXT_SET(row, NULL);
    ((uint8_t *)row)[PT_KIND] = 4;
    *(void **)((uint8_t *)pt + PTB_TAIL_AT) = row;
}

/* ---- whether a phrase may follow the last one settled ---------------- */

/* One slot of a phrase buffer copied into the working area, its kakari worked
   out for a link, and asked against the last row the table holds: three if the
   row will take it and nought if it will not. The row has to be of the first
   kind for the question to be asked at all.
 *
 * IBM reads the table's tail three times over rather than once. */
int16_t ta_CheckPhraseLinkEnd(void *ta, int16_t slot)
{
    uint8_t  uke[4];
    uint8_t *work = (uint8_t *)ta + TA_WORK;
    void    *pt;
    void    *tail;
    int16_t  rc = 0;
    int16_t  i;

    for (i = 0; i < 4; i++)
        uke[i] = 0;

    memcpy(work,
           WP_SLOT((uint8_t *)TA_AT(ta, TA_PHRASEBUF_AT) + PB_BUFFER, slot),
           PB_SLOT_SIZE);
    ta_SetUkeTypeForLink(ta, uke, work);

    pt   = TA_AT(ta, TA_PHRASETABLE_AT);
    tail = *(void **)((uint8_t *)pt + PTB_TAIL_AT);
    if (tail != NULL) {
        tail = *(void **)((uint8_t *)pt + PTB_TAIL_AT);
        if (((uint8_t *)tail)[PT_KIND] == 1) {
            tail = *(void **)((uint8_t *)pt + PTB_TAIL_AT);
            if (((uint8_t *)tail)[PT_RIGHT] & uke[0])
                rc = (int16_t)(rc + 3);
        }
    }
    return rc;
}

/* ---- the next sentence's worth of candidates -------------------------- */

/* The marks are shifted down by half their length -- what was the second half
   becomes the first -- the dictionary search is run over what the reader has
   next, and each of the three per-buffer entries that names a slot has a path
   made through it and a phrase buffer filled from it.
 *
 * Minus one is the search finding nothing at all. Otherwise the answer is how
   many buffers were filled, which is also written into the analysis.
 */
int16_t ta_SetNextPhraseBuffer(void *ta, int16_t off)
{
    void   *pb = TA_AT(ta, TA_PHRASEBUF_AT);
    void   *ds = TA_AT(ta, TA_DICTSEARCH_AT);
    int16_t n = 0;
    int16_t i;

    for (i = 0; i < 0x2d6; i++)
        *((uint8_t *)ta + TA_MARKS + i) = *((uint8_t *)ta + TA_MARKS + 0x2d6
                                            + i);

    ds_Do(ds);
    if (*(int16_t *)((uint8_t *)ds + DS_COUNT) == 0)
        return -1;

    for (i = 0; i < TA_BUFFER_N; i++)
        *(int16_t *)((uint8_t *)ta + TA_USED + i * 2) = 0;

    for (i = 0; i < TA_BUFFER_N; i++) {
        int16_t at = *(int16_t *)((uint8_t *)ta + TA_PERBUF
                                  + (size_t)i * TA_PERBUF_SIZE);

        if (at >= 0) {
            int16_t where = (int16_t)
                (*(uint16_t *)(WP_SLOT((uint8_t *)pb + PB_BUFFER, at)
                               + WP_MORAS) + off);

            jp_Make(TA_AT(ta, TA_JPATH_AT), where);
            *(int16_t *)((uint8_t *)ta + TA_USED + n * 2) =
                pb_SetPhraseBuffer(pb, (uint8_t *)ta + TA_BUFFERS
                                       + (size_t)n * TA_BUFFER_SIZE);
            n++;
        }
    }
    *(int16_t *)((uint8_t *)ta + TA_COUNT) = n;
    return n;
}

/* ---- the three best phrases at each of three end positions ------------ */

/* Every phrase in the working copy of a buffer is offered to three ranked
 * places, best first, and each place keeps up to four phrases that end where
 * it says. A phrase that ends further on than the place holds takes it over
 * and pushes the places below it down; a phrase that ends exactly where the
 * place says is filed among its four by how few characters it covers and,
 * where two cover the same, by how few function words it carries.
 *
 * Minus one is nothing kept at all. Where something was, the mark at each
 * kept end position is set to two -- into the second half of the marks, at
 * the caller's own offset.
 *
 * The four slots of a place are IBM's `_PERBUF' entry: four sixteen-bit slot
 * numbers, which is the whole of the eight bytes.
 */
int16_t ta_SetPhraseMakeTable(void *ta, int16_t off)
{
    void    *pb = TA_AT(ta, TA_PHRASEBUF_AT);
    int16_t  best[3];
    int16_t  rc;
    int16_t  i, b, k, m;

    memset((uint8_t *)ta + TA_MARKS2, 0, TA_MARKS_HALF);
    memset((uint8_t *)ta + TA_PERBUF, -1, TA_PERBUF_N * TA_PERBUF_SIZE);
    memset(best, -1, sizeof best);

    for (i = 0; i < *(int16_t *)((uint8_t *)pb + PB_TAIL); i++) {
        uint8_t *wp = WP_SLOT((uint8_t *)pb + PB_BUFFER, i);
        int16_t  ends = (int16_t)*(uint16_t *)(wp + WP_MORAS);

        for (b = 0; b < TA_PERBUF_N; b++) {
            int16_t *place = TA_PERBUF_AT(ta, b);

            if (place[0] < 0 || best[b] < ends) {
                for (k = 1; k >= b; k--) {
                    for (m = 0; m < 4; m++)
                        TA_PERBUF_AT(ta, k + 1)[m] = TA_PERBUF_AT(ta, k)[m];
                    best[k + 1] = best[k];
                }
                for (k = 0; k < 4; k++)
                    place[k] = -1;
                best[b] = ends;
                place[0] = i;
                break;
            }
            if (best[b] != ends)
                continue;
            for (k = 0; k < 4; k++) {
                int16_t   other = place[k];
                uint8_t  *ow;

                if (other >= 0) {
                    ow = WP_SLOT((uint8_t *)pb + PB_BUFFER, other);
                    if (ow[WP_CHARS] > wp[WP_CHARS])
                        ;
                    else if (ow[WP_CHARS] != wp[WP_CHARS])
                        continue;
                    else if (ow[WP_FZKS] <= wp[WP_FZKS])
                        continue;
                }
                for (m = 2; m >= k; m--)
                    place[m + 1] = place[m];
                place[k] = i;
                break;
            }
            break;
        }
    }

    rc = -1;
    for (i = 0; i < TA_PERBUF_N; i++) {
        if (best[i] >= 0) {
            *((uint8_t *)ta + TA_MARKS2 + off + best[i]) = 2;
            rc = 0;
        }
    }
    return rc;
}

/* ---- whether one phrase may follow another --------------------------- */

/* Three if the phrase in the buffer will take the phrase in the working copy
 * in front of it, and nought if it will not. Two kakari are worked out and
 * asked against each other: the one the phrase behind offers, and the one the
 * phrase in front will accept.
 *
 * The phrase in front is the buffer's own slot, copied into the working area
 * first -- and the phrase behind is copied in after it, into the second slot
 * of that area, which nothing then reads. IBM works its kakari out from the
 * phrase buffer's slot rather than from the copy.
 *
 * Where the phrase in front ends on a function word, its kakari comes whole
 * out of DictMan's table, six bytes indexed by what the extended function-word
 * dictionary says the word's rule is, with the extra bits that word's own
 * number adds. Where it does not, three bits of the last content word's tags
 * decide it between them, and one of those asks whether the phrase table has
 * anything in it at all.
 *
 * The last row settled is asked as well, and only where it is of the first
 * kind: a row that will take everything -- both bits of 0x23 -- is not asked
 * again, and otherwise the phrase behind is held against what that row will
 * accept. With nothing settled yet the two are asked directly, and a phrase
 * behind that carries the bit answers on its own.
 */
int16_t ta_CheckPhraseLink(void *ta, int16_t buf, int16_t slot, int16_t at)
{
    void    *pb   = TA_AT(ta, TA_PHRASEBUF_AT);
    void    *pt   = TA_AT(ta, TA_PHRASETABLE_AT);
    uint8_t *work = (uint8_t *)ta + TA_WORK;
    uint8_t *wp   = WP_SLOT((uint8_t *)pb + PB_BUFFER, slot);
    uint8_t  behind[4];
    uint8_t  front[4];
    uint8_t  kkr[6];
    uint8_t  flag = 0;
    uint8_t *d;
    uint8_t *s;
    void    *tail;
    int16_t  i;
    int16_t  rc;

    for (i = 0; i < 4; i++) {
        front[i]  = 0;
        behind[i] = 0;
    }
    for (i = 0; i < 6; i++)
        kkr[i] = 0;

    d = work;
    s = (uint8_t *)ta + TA_BUFFERS + (size_t)buf * TA_BUFFER_SIZE
        + (size_t)at * PB_SLOT_SIZE;
    for (i = 0; (uint16_t)i < PB_SLOT_SIZE; i++)
        *d++ = *s++;
    ta_SetUkeTypeForLink(ta, behind, work);
    behind[0] = (uint8_t)(behind[0] & 0xfc);

    /* And on into the second slot of the working area, which nothing reads. */
    s = wp;
    for (i = 0; (uint16_t)i < PB_SLOT_SIZE; i++)
        *d++ = *s++;
    ta_SetUkeTypeForLink(ta, front, wp);
    front[0] = (uint8_t)(front[0] & 0xfc);

    if (WP_B(wp, WP_FZKS) != 0) {
        int16_t last = (int16_t)(WP_B(wp, WP_FZKS) - 1);
        int16_t code = (int16_t)(WF_SLOT(wp, last)[WF_CODE] & 0x7f);
        int16_t rule = *(int16_t *)(WF_SLOT(wp, last) + WF_AT);
        int16_t row  = (int16_t)((dm_GetFuncDictEx()[rule + 3] - 1) & 0x3f);

        for (i = 0; i < 6; i++)
            kkr[i] = dm_GetKakariAt((uint16_t)(row * 6 + i));
        ta_ExtKkrForLink(ta, kkr, code, &flag);
    } else {
        int16_t last = (int16_t)(WP_B(wp, WP_WORDS) - 1);
        uint8_t tg   = dm_GetTGAt2(*(WW_SLOT(wp, last) + WW_POS), 2);

        if (tg & 0x1c) {
            if (tg & 0x04)
                kkr[0] = *(void **)((uint8_t *)pt + PTB_TAIL_AT) != NULL
                         ? 0x00 : 0xfc;
            if (tg & 0x08)
                kkr[0] = 0xd0;
            if (tg & 0x10)
                kkr[0] = (uint8_t)(kkr[0] | 0x20);
        } else {
            kkr[0] = 0;
        }
    }

    rc   = 0;
    tail = *(void **)((uint8_t *)pt + PTB_TAIL_AT);
    if (tail != NULL && ((uint8_t *)tail)[PT_KIND] == 1) {
        if ((((uint8_t *)tail)[PT_RIGHT] & 0x23) != 0x23) {
            if (front[0] & ((uint8_t *)tail)[PT_RIGHT])
                rc = (int16_t)(rc + 3);
        }
        if (behind[0] & kkr[0])
            rc = (int16_t)(rc + 3);
    } else if ((front[0] & 0x04) || (behind[0] & kkr[0])) {
        rc = (int16_t)(rc + 3);
    }
    return rc;
}

/* ---- the sentence ending on a full stop ------------------------------- */

/* Whether the phrases in the buffer between them reach exactly to the end of
 * what the reader has, and if they do, settling the best of them.
 *
 * The best is the one that ends furthest into the text; between two that end
 * in the same place, the one whose characters cost least once the link to the
 * last settled row is taken off; and between two of those, the one with fewest
 * function words. A hundred and one is the cost nothing has yet beaten.
 *
 * One is not an error: it is the answer where the phrases do not reach the end
 * of the text, which is every sentence but the last of a stretch.
 */
int16_t ta_CheckMaru(void *ta, int16_t off)
{
    void    *pb = TA_AT(ta, TA_PHRASEBUF_AT);
    int16_t  ends = 0;
    int16_t  cost = 0x63;
    int16_t  fzks = 0x63;
    int16_t  at   = -1;
    int16_t  i;

    if (*(int16_t *)((uint8_t *)pb + PB_TAIL) <= 0)
        return 1;

    for (i = 0; i < *(int16_t *)((uint8_t *)pb + PB_TAIL); i++) {
        uint8_t *wp    = WP_SLOT((uint8_t *)pb + PB_BUFFER, i);
        int16_t  score = (int16_t)(wp[WP_CHARS]
                                   - ta_CheckPhraseLinkEnd(ta, i));
        int32_t  here  = (int32_t)*(uint16_t *)(wp + WP_MORAS);

        if (here > ends
            || (here == ends && score < cost)
            || (here == ends && score == cost && wp[WP_FZKS] < fzks)) {
            ends = *(int16_t *)(wp + WP_MORAS);
            cost = score;
            fzks = (int16_t)wp[WP_FZKS];
            at   = i;
        }
    }

    if (*(int16_t *)((uint8_t *)TA_AT(ta, TA_INPUTCHAR_AT) + IC_COUNT)
        != (int16_t)(ends + off))
        return 1;

    *((uint8_t *)ta + TA_UNKNOWN_10) = 1;
    *(int16_t *)((uint8_t *)ta + TA_INTON_FAILED) = 0;
    return ta_Kakutei(ta, WP_SLOT((uint8_t *)pb + PB_BUFFER, at));
}

/* ---- the best phrase over all three buffers --------------------------- */

/* Which slot of which buffer the analysis should settle next, and through the
 * caller's pointer which buffer it came out of.
 *
 * Each buffer is read twice over. The first pass looks for the phrase that
 * reaches furthest into the text and, between two that reach equally far, the
 * one covering fewest characters -- skipping any whose first word carries the
 * bit that says it may not stand here. The second pass is the same without
 * that skip and runs only where the first found nothing at all, so a buffer
 * always names something.
 *
 * Then the four phrases the place kept are scored against the one the first
 * pass chose: how far the two together reach, what the characters cost once
 * the link to the phrase in front is taken off, how many function words, and
 * how far from each other the two ends are. Best first in that order, and a
 * phrase the break rules refuse is passed over whatever it scored.
 *
 * With nothing chosen anywhere, the first place of the first buffer answers
 * and the caller is told nought.
 */
int16_t ta_PhraseMatching(void *ta, int16_t *out)
{
    void   *pb = TA_AT(ta, TA_PHRASEBUF_AT);
    int16_t best     = -1;
    int16_t bestEnds = 0;
    int16_t bestCost = 0x63;
    int16_t bestFzks = 0x63;
    int16_t bestGap  = 0x63;
    int16_t bestBuf  = -1;
    int16_t b;

    for (b = 0; b < *(int16_t *)((uint8_t *)ta + TA_COUNT); b++) {
        uint8_t *base = (uint8_t *)ta + TA_BUFFERS
                        + (size_t)b * TA_BUFFER_SIZE;
        int16_t  ends  = 0;
        int16_t  chars = 0x63;
        int16_t  kind  = 0;
        int16_t  from = 0;  /* IBM leaves it until a pass names it; nothing
                               reads it before one does, so ours may clear
                               it */
        int16_t  used = *(int16_t *)((uint8_t *)ta + TA_USED + b * 2);
        int16_t  j;
        int16_t  slot;
        int16_t  gap;
        int16_t  total;
        int32_t  hi, lo;

        if (TA_PERBUF_AT(ta, b)[0] < 0)
            continue;

        for (j = 0; j < used; j++) {
            uint8_t *wp = WP_SLOT(base, j);
            int32_t  here = (int32_t)*(uint16_t *)(wp + WP_MORAS);

            if (here > ends || (here == ends && chars > wp[WP_CHARS])) {
                if (WW_SLOT(wp, 0)[WW_ATTR] & 0x08)
                    continue;
                ends  = *(int16_t *)(wp + WP_MORAS);
                chars = (int16_t)wp[WP_CHARS];
                kind  = (int16_t)wp[WP_TYPE];
                from  = j;
            }
        }
        /* And again without the skip, where the skip left nothing. */
        if (ends == 0) {
            for (j = 0; j < used; j++) {
                uint8_t *wp = WP_SLOT(base, j);
                int32_t  here = (int32_t)*(uint16_t *)(wp + WP_MORAS);

                if (here > ends || (here == ends && chars > wp[WP_CHARS])) {
                    ends  = *(int16_t *)(wp + WP_MORAS);
                    chars = (int16_t)wp[WP_CHARS];
                    kind  = (int16_t)wp[WP_TYPE];
                    from  = j;
                }
            }
        }

        slot = TA_PERBUF_AT(ta, b)[0];
        {
            int32_t here = (int32_t)*(uint16_t *)
                (WP_SLOT((uint8_t *)pb + PB_BUFFER, slot) + WP_MORAS);

            hi    = here > ends ? here : ends;
            lo    = here < ends ? here : ends;
            gap   = (int16_t)(hi - lo);
            total = (int16_t)(here + ends);
        }

        for (j = 0; j < 4; j++) {
            uint8_t *wp;
            int16_t  cost;
            int16_t  fzks;

            if (TA_PERBUF_AT(ta, b)[j] < 0)
                continue;
            slot = TA_PERBUF_AT(ta, b)[j];
            wp   = WP_SLOT((uint8_t *)pb + PB_BUFFER, slot);
            cost = (int16_t)(wp[WP_CHARS] + chars);
            fzks = (int16_t)wp[WP_FZKS];
            if (ends != 0)
                cost = (int16_t)(cost - ta_CheckPhraseLink(ta, b, slot, from));

            if (total > bestEnds
                || (total == bestEnds && cost < bestCost)
                || (total == bestEnds && cost == bestCost && fzks < bestFzks)
                || (total == bestEnds && cost == bestCost && fzks == bestFzks
                    && gap < bestGap)) {
                if (ta_Kinsoku(ta, slot, kind) >= 0) {
                    bestEnds = total;
                    bestCost = cost;
                    bestFzks = fzks;
                    bestGap  = gap;
                    best     = slot;
                    bestBuf  = b;
                }
            }
        }
    }

    if (best < 0) {
        best = TA_PERBUF_AT(ta, 0)[0];
        *out = 0;
    } else {
        *out = bestBuf;
    }
    return best;
}

/* ---- one sentence, phrase by phrase ----------------------------------- */

/* The whole of parsing one sentence. The marks are cleared and the two the
 * parse starts from set, the dictionary search is run, a path is made and the
 * phrase buffer filled from it. Then, while the phrases do not yet reach the
 * end of the text: the next buffer's worth of candidates is built, the best
 * phrase over all three buffers chosen, that phrase settled into rows of the
 * phrase table, and the offset moved on by what it covered.
 *
 * The negative answers say which step refused, and one is the phrase table
 * having run out of room, which the caller deals with rather than this.
 */
int16_t ta_TextParsing(void *ta)
{
    void   *pb = TA_AT(ta, TA_PHRASEBUF_AT);
    int16_t maru;
    int16_t off = 0;
    int16_t n;
    int16_t buf = 0;

    memset((uint8_t *)ta + TA_MARKS, 0, TA_MARKS_HALF);
    *((uint8_t *)ta + TA_MARKS2) = 1;
    *((uint8_t *)ta + TA_MARKS)  = 2;
    if (ds_Do(TA_AT(ta, TA_DICTSEARCH_AT)) < 0)
        return -20;
    /* The lattice and the paths over it, in the form reference/jptap.c
       prints them from IBM's own JPath::Make, so the two dumps diff as they
       stand. `EVV_JPTRACE' turns it on and nothing is written without it.
       This is the seam below the romanizer's own: the tap on the outside
       says whether the two romanizers agree on a sentence, and this says
       whether they agree on the words they had to choose between. A missing
       dictionary entry that turned one Japanese sentence into one phrase
       instead of two was found by diffing these two dumps. */
    if (getenv("EVV_JPTRACE")) {
        void   *d = TA_AT(ta, TA_DICTSEARCH_AT);
        int16_t i, ne = *(int16_t *)((uint8_t *)d + DS_COUNT);

        fprintf(stderr, "entries %d\n", (int)ne);
        for (i = 0; i < ne; i++) {
            const uint8_t *e = (const uint8_t *)d + DS_ENTRY
                               + (size_t)i * DS_ENTRY_SIZE;
            int j;

            fprintf(stderr, "  e%d at=%d chars=%d pos=%d kanalen=%d "
                    "accent=%d attr=%02x attr2=%02x cost=%d kana=",
                    (int)i, (int)*(const int16_t *)(e + DE_AT),
                    (int)e[DE_CHARS], (int)e[DE_POS], (int)e[DE_KANALEN],
                    (int)*(const int16_t *)(e + DE_ACCENT), e[DE_ATTR],
                    e[DE_ATTR2], (int)*(const int32_t *)(e + DE_COST));
            for (j = 0; j < e[DE_KANALEN] && j < 10; j++)
                fprintf(stderr, "%02x", e[DE_KANA + j]);
            fprintf(stderr, "\n");
        }
    }

    jp_Make(TA_AT(ta, TA_JPATH_AT), off);
    if (getenv("EVV_JPTRACE")) {
        void   *jp = TA_AT(ta, TA_JPATH_AT);
        int16_t i, np = (int16_t)JP_U16(jp, JP_PATH_COUNT);

        fprintf(stderr, "paths %d\n", (int)np);
        for (i = 0; i < np; i++) {
            const uint8_t *pt = JP_PATH_AT(jp, i);
            int j;

            fprintf(stderr, "  p%d count=%d cost=%d end=%d cont=%d at=",
                    (int)i, (int)pt[JPT_COUNT], (int)pt[JPT_COST],
                    (int)pt[JPT_END], (int)pt[JPT_CONT]);
            for (j = 0; j < pt[JPT_COUNT]; j++)
                fprintf(stderr, "%d,", (int)pt[JPT_AT + j]);
            fprintf(stderr, "\n");
        }
    }
    n = pb_SetPhraseBuffer(pb, (uint8_t *)pb + PB_BUFFER);
    *(int16_t *)((uint8_t *)pb + PB_TAIL) = n;
    if (n == 0)
        return -11;
    if (ta_SetPhraseMakeTable(ta, off) != 0)
        return -12;

    for (;;) {
        int16_t got;

        maru = ta_CheckMaru(ta, off);
        if (maru <= 0)
            break;
        if (ta_SetNextPhraseBuffer(ta, off) == 0)
            return -13;
        buf = 0;
        n = ta_PhraseMatching(ta, &buf);
        if (n < 0)
            return -14;
        *((uint8_t *)ta + TA_UNKNOWN_10) = 0;
        *(int16_t *)((uint8_t *)ta + TA_INTON_FAILED) = 0;
        got = ta_Kakutei(ta, WP_SLOT((uint8_t *)pb + PB_BUFFER, n));
        if (got < 0)
            return got == -1 ? 1 : -19;
        off = (int16_t)(off + *(uint16_t *)
                        (WP_SLOT((uint8_t *)pb + PB_BUFFER, n) + WP_MORAS));
        pb_Copy(pb, buf);
        *(int16_t *)((uint8_t *)pb + PB_TAIL) =
            *(int16_t *)((uint8_t *)ta + TA_USED + buf * 2);
        if (ta_SetPhraseMakeTable(ta, off) != 0)
            return -15;
    }
    if (maru < 0)
        return maru == -1 ? 1 : -19;
    return 0;
}

/* ---- and every sentence the reader still holds ------------------------ */

/* Sentences are parsed one after another until the reader says the text ended
 * on something, and a parse that answers one -- the phrase table full -- is
 * cut short where the last row that fits ends and read again from there.
 *
 * Five is the romanizer having been told to stop. The unknown-word pass runs
 * once at the end over everything settled.
 */
int16_t ta_ProcessRemaining(void *ta)
{
    void   *ic = TA_AT(ta, TA_INPUTCHAR_AT);
    void   *pt = TA_AT(ta, TA_PHRASETABLE_AT);
    void   *row = NULL;
    int32_t at = 0;
    int16_t rc;

    if (*(int16_t *)((uint8_t *)ic + IC_COUNT) == 0) {
        *(int32_t *)((uint8_t *)ta + TA_DONE) = 1;
        return 1;
    }

    do {
        *((uint8_t *)ta + TA_LONGWORDS) = 0;
        if (*(int32_t *)((uint8_t *)TA_AT(ta, TA_OWNER_AT) + RZ_STOPPED) != 0)
            return 5;
        rc = ta_TextParsing(ta);
        if (rc < 0) {
            *(int32_t *)((uint8_t *)ta + TA_DONE) = 0;
            return rc == -20 ? rc : -8;
        }
        if (rc == 1)
            ta_HandleOverflow(ta, row, at);
        row = *(void **)((uint8_t *)pt + PTB_TAIL_AT);
        at  = *(int32_t *)((uint8_t *)ic + IC_POS);
    } while (*(int32_t *)((uint8_t *)ic + IC_ENDED) == 0);

    *(int32_t *)((uint8_t *)ta + TA_DONE) = 1;
    *((uint8_t *)ta + TA_LONGWORDS) = 0;
    ta_UnknownWord(ta);
    return 0;
}

/* ---- the whole of a stretch of text ----------------------------------- */

/* Sentences are read and parsed until the reader says the text ended on
 * something. What the reader answers decides the rest: minus five is a
 * refusal, minus four a sentence with nothing in it, minus one a sentence too
 * long for the buffer, two an annotation, three the caller having asked for
 * this much and no more, and anything else is a sentence to parse.
 *
 * Where the last row settled has to be marked, it is marked on the table's
 * tail: the fourth kind for a sentence with nothing in it and the third for
 * one that was too long, and the reader is wound back a character so that the
 * next call reads it again.
 */
int16_t ta_ProcessSentence(void *ta)
{
    void   *ic = TA_AT(ta, TA_INPUTCHAR_AT);
    void   *pt = TA_AT(ta, TA_PHRASETABLE_AT);
    void   *row = NULL;
    void   *tail;
    int32_t at = 0;
    int16_t rc;

    *((uint8_t *)ta + TA_LONGWORDS) = 0;

    for (;;) {
        if (*(int32_t *)((uint8_t *)TA_AT(ta, TA_OWNER_AT) + RZ_STOPPED) != 0)
            return 5;

        rc = ic_ReadSentence(ic);
        *(int32_t *)((uint8_t *)ic + IC_RESUME) = 0;

        if (rc == -5)
            return -8;
        if (rc == -4) {
            tail = *(void **)((uint8_t *)pt + PTB_HEAD_AT);
            if (tail != NULL) {
                tail = *(void **)((uint8_t *)pt + PTB_TAIL_AT);
                ((uint8_t *)tail)[PT_KIND] = 4;
                *(int32_t *)((uint8_t *)ta + TA_DONE) = 1;
                *((uint8_t *)ta + TA_LONGWORDS) = 0;
                ta_UnknownWord(ta);
            }
            /* IBM takes the address of the punctuation the sentence ended on
               here and does nothing with it. */
            return 0;
        }
        if (rc == -1) {
            tail = *(void **)((uint8_t *)pt + PTB_HEAD_AT);
            if (tail != NULL) {
                tail = *(void **)((uint8_t *)pt + PTB_TAIL_AT);
                ((uint8_t *)tail)[PT_KIND] = 3;
                *(int32_t *)((uint8_t *)ic + IC_POS) =
                    *(int32_t *)((uint8_t *)ic + IC_POS) - 1;
                *(int32_t *)((uint8_t *)ta + TA_DONE) = 1;
                *((uint8_t *)ta + TA_LONGWORDS) = 0;
                ta_UnknownWord(ta);
                return 2;
            }
            if (*(int16_t *)((uint8_t *)ic + IC_RAWPOS) > 0) {
                *(int32_t *)((uint8_t *)ic + IC_POS) =
                    *(int32_t *)((uint8_t *)ic + IC_POS) - 1;
                return 2;
            }
            *(int32_t *)((uint8_t *)ta + TA_DONE) = 1;
            return 1;
        }
        if (rc == 2) {
            *(int32_t *)((uint8_t *)ic + IC_POS) =
                *(int32_t *)((uint8_t *)ic + IC_POS) - 1;
            *(int32_t *)((uint8_t *)ic + IC_RESUME) = 1;
            return 2;
        }
        if (rc == 3)
            return 3;

        rc = ta_TextParsing(ta);
        if (rc < 0) {
            *(int32_t *)((uint8_t *)ta + TA_DONE) = 0;
            return rc == -20 ? rc : -8;
        }
        if (rc == 1)
            ta_HandleOverflow(ta, row, at);
        row = *(void **)((uint8_t *)pt + PTB_TAIL_AT);
        at  = *(int32_t *)((uint8_t *)ic + IC_POS);
        if (*(int32_t *)((uint8_t *)ic + IC_ENDED) != 0)
            break;
    }

    *(int32_t *)((uint8_t *)ta + TA_DONE) = 1;
    *((uint8_t *)ta + TA_LONGWORDS) = 0;
    ta_UnknownWord(ta);
    return 0;
}

/* ---- the text arriving ------------------------------------------------ */

/* A fresh stretch of text, replacing whatever was there.
 *
 * The annotations are taken out first if the caller asked for them to be --
 * `TextNormalizer' rewrites each annotated stretch into the words a reader
 * would say and hands back a buffer of its own, which is given up again at the
 * end. Then two buffers are made: one of 0xff bytes as long as the text, which
 * is what the marks are kept in, and one a byte longer for the text itself,
 * which `FormatAddText' fills. Nought is a buffer that could not be made.
 */
int32_t ta_SetText(void *ta, const char *text, int32_t len)
{
    char    *norm = NULL;
    uint32_t got  = 0;

    if (TA_AT(ta, TA_FORMATTED_AT) != NULL)
        cpp_delete(TA_AT(ta, TA_FORMATTED_AT));
    if (TA_AT(ta, TA_RAW_AT) != NULL)
        cpp_delete(TA_AT(ta, TA_RAW_AT));

    if (rp_isAnnotationsInText(
            *(RomInstParam **)((uint8_t *)TA_AT(ta, TA_OWNER_AT)
                               + RZ_PARAM_AT))
        && text != NULL) {
        if (tn_normalizeText(TA_AT(ta, TA_NORMALIZER_AT), text,
                             (uint32_t)len, &norm, &got) == 0) {
            len  = (int32_t)got;
            text = norm;
        }
    }

    TA_AT(ta, TA_RAW_AT) = cpp_new((uint32_t)len);
    if (TA_AT(ta, TA_RAW_AT) == NULL)
        return 0;
    *(int32_t *)((uint8_t *)ta + TA_RAW_LEN) = len;
    memset(TA_AT(ta, TA_RAW_AT), 0xff, (size_t)len);

    if (text != NULL) {
        TA_AT(ta, TA_FORMATTED_AT) = cpp_new((uint32_t)(len + 1));
        if (TA_AT(ta, TA_FORMATTED_AT) == NULL)
            return 0;
        ic_DeleteSnlkTable(TA_AT(ta, TA_INPUTCHAR_AT));
        ta_FormatAddText(ta, (char *)TA_AT(ta, TA_FORMATTED_AT), text, len);
    } else {
        TA_AT(ta, TA_FORMATTED_AT) = (void *)text;
    }

    ic_SetTextAt(TA_AT(ta, TA_INPUTCHAR_AT),
                 (const char *)TA_AT(ta, TA_FORMATTED_AT), 0);
    *(int32_t *)((uint8_t *)ta + TA_DONE) = 0;
    if (norm != NULL) {
        cpp_delete(norm);
        norm = NULL;
    }
    return 1;
}

/* And a stretch added to whatever is left over.
 *
 * What is left over is the marks buffer from before the point the reader had
 * reached, which is copied to the front of a new one so that the marks of text
 * not yet read keep their places. The chain of annotation marks is walked
 * twice for the same reason: everything before that point is thrown away and
 * everything after it has its place moved back by as much.
 */
int32_t ta_AppendText(void *ta, const char *text, int32_t len)
{
    void    *ic = TA_AT(ta, TA_INPUTCHAR_AT);
    char    *norm = NULL;
    uint32_t got  = 0;
    uint8_t *old  = NULL;
    void    *node;
    void    *last = NULL;
    int32_t  at   = 0;
    int32_t  read;
    int32_t  cut  = 0;

    if (TA_AT(ta, TA_FORMATTED_AT) != NULL) {
        cpp_delete(TA_AT(ta, TA_FORMATTED_AT));
        TA_AT(ta, TA_FORMATTED_AT) = NULL;
    }

    if (rp_isAnnotationsInText(
            *(RomInstParam **)((uint8_t *)TA_AT(ta, TA_OWNER_AT)
                               + RZ_PARAM_AT))
        && text != NULL) {
        if (tn_normalizeText(TA_AT(ta, TA_NORMALIZER_AT), text,
                             (uint32_t)len, &norm, &got) == 0) {
            len  = (int32_t)got;
            text = norm;
        }
    }

    read = (int32_t)*(int16_t *)((uint8_t *)ic + IC_LENGTH);
    if (TA_AT(ta, TA_RAW_AT) != NULL) {
        old = (uint8_t *)TA_AT(ta, TA_RAW_AT);
        while (at + read < len
               && at + read < *(int32_t *)((uint8_t *)ta + TA_RAW_LEN)
               && old[at + read] != 0xff)
            at++;
    }

    node = *(void **)((uint8_t *)ic + IC_SNLK_AT);
    while (node != NULL) {
        if ((int32_t)*(int16_t *)((uint8_t *)node + SN_AT) >= read) {
            cut = 1;
            break;
        }
        last = node;
        node = *(void **)((uint8_t *)node + SN_NEXT_AT);
    }
    if (cut != 0 && last != NULL) {
        *(void **)((uint8_t *)last + SN_NEXT_AT) = NULL;
        ic_DeleteSnlkTable(ic);
        *(void **)((uint8_t *)ic + IC_SNLK_AT) = node;
        while (node != NULL) {
            *(int16_t *)((uint8_t *)node + SN_AT) =
                (int16_t)(*(int16_t *)((uint8_t *)node + SN_AT) - read);
            node = *(void **)((uint8_t *)node + SN_NEXT_AT);
        }
    }

    TA_AT(ta, TA_RAW_AT) = cpp_new((uint32_t)len);
    if (TA_AT(ta, TA_RAW_AT) == NULL)
        return 0;
    *(int32_t *)((uint8_t *)ta + TA_RAW_LEN) = len;
    memset(TA_AT(ta, TA_RAW_AT), 0xff, (size_t)len);
    if (old != NULL) {
        memcpy(TA_AT(ta, TA_RAW_AT), old + read, (size_t)at);
        cpp_delete(old);
        old = NULL;
    }

    TA_AT(ta, TA_FORMATTED_AT) = cpp_new((uint32_t)(len + 1));
    if (TA_AT(ta, TA_FORMATTED_AT) == NULL)
        return 0;
    ta_FormatAddText(ta, (char *)TA_AT(ta, TA_FORMATTED_AT), text, len);
    ic_SetText(ic, (const char *)TA_AT(ta, TA_FORMATTED_AT));
    *(int32_t *)((uint8_t *)ta + TA_DONE) = 0;
    if (norm != NULL) {
        cpp_delete(norm);
        norm = NULL;
    }
    return 1;
}

/* ---- an annotation that names a word and its reading ------------------ */

/* Text of the shape `s1 <reading> `s2 <word> `s3, read one character at a
 * time, with the state moving on at each of the three marks and anything else
 * ending the walk. The reading may be kana, a caret, or a long-vowel mark that
 * is not the first character; the word may be anything up to thirty-two
 * characters. What comes back through the caller's pointers is the word and
 * the reading, each in a buffer of its own, and where the annotation ended.
 *
 * Minus one is anything that did not read as such an annotation, which is the
 * ordinary answer: most text is not one.
 */
int32_t ta_processSnlkAnno(void *ta, const char *text, char **word,
                           char **reading, const char **end)
{
    const char *p     = text;
    const char *next  = text;
    const char *wordStart = NULL;
    const char *wordEnd   = NULL;
    const char *readStart = NULL;
    const char *readEnd   = NULL;
    int32_t     state = 0;
    int32_t     going = 1;
    int32_t     found = 0;
    int32_t     count = 0;
    int32_t     len;

    (void)ta;
    while (*p != '\0' && going != 0) {
        next = ju_IsDBCSLeadByte(*p) ? p + 2 : p + 1;

        if (*p == '`') {
            if (tolower((unsigned char)next[0]) == 's') {
                switch (next[1]) {
                case '1':
                    if (state == 0) {
                        state = 1;
                        p = next + 1;
                        continue;
                    }
                    going = 0;
                    break;
                case '2':
                    if (state == 1) {
                        state = 2;
                        p = next + 1;
                        count = 0;
                        continue;
                    }
                    going = 0;
                    break;
                case '3':
                    if (state == 2) {
                        state = 3;
                        p = next + 1;
                        continue;
                    }
                    going = 0;
                    break;
                default:
                    going = 0;
                    break;
                }
            } else {
                going = 0;
            }
        }

        if (state == 1) {
            if (next[0] == '\0') {
                going = 0;
            } else if (next[0] == ' ') {
                if (readStart != NULL && readEnd == NULL)
                    readEnd = next;
            } else if (next[0] == '^' || ju_IsHiragana(next)
                       || ju_IsKatakana(next)
                       || (ju_IsLongVowel(next) && next != readStart)) {
                if (readStart == NULL) {
                    if (ju_IsLongVowel(next))
                        going = 0;
                    else
                        readStart = next;
                }
            } else if (next[0] == '`') {
                if (readStart == NULL)
                    going = 0;
                else if (readEnd == NULL)
                    readEnd = next;
            } else {
                going = 0;
            }
        } else if (state == 2) {
            if (next[0] == '\0') {
                going = 0;
            } else if (next[0] == ' ') {
                if (wordStart != NULL && wordEnd == NULL)
                    wordEnd = next;
            } else if (next[0] == '`') {
                if (wordStart == NULL)
                    going = 0;
                else if (wordEnd == NULL)
                    wordEnd = next;
            } else {
                if (wordStart == NULL)
                    wordStart = next;
                count++;
                if (count > 0x20)
                    going = 0;
            }
        } else if (state == 3) {
            *end  = next;
            found = 1;
            going = 0;
        }
        p = next;
    }

    if (found == 0)
        return -1;

    len = (int32_t)(wordEnd - wordStart);
    *word = cpp_new((uint32_t)(len + 1));
    strncpy(*word, wordStart, (size_t)len);
    (*word)[len] = '\0';

    len = (int32_t)(readEnd - readStart);
    *reading = cpp_new((uint32_t)(len + 1));
    strncpy(*reading, readStart, (size_t)len);
    (*reading)[len] = '\0';
    return 0;
}

/* ---- the text as the reader wants it ---------------------------------- */

/* Text arrives with two kinds of annotation in it and leaves with neither.
 *
 * The first is a tilde: `~' and then a reading in kana, optionally a slash and
 * a second reading, which says how the run of characters in front of it is to
 * be said. The second is a backquote, which `processSnlkAnno' reads. Both are
 * taken out of the text and put on the reader's own table instead, keyed by
 * how many characters in they were.
 *
 * The marks buffer is written as the walk goes: nought for a character that
 * came through as itself, one for the first character of an annotation's
 * written form and two for the rest of it. A character whose mark is already
 * something other than 0xff has been seen before and is skipped at the start,
 * which is what makes this work on text added to text.
 *
 * Six states, and what moves between them is what the next character is: a
 * delimiter, an ideographic space, a backquote, a tilde with kana behind it,
 * or anything else. A run of more than thirty-two ordinary characters gives up
 * and goes to the state that copies.
 *
 * Minus one is the reader's table refusing an annotation.
 */
int32_t ta_FormatAddText(void *ta, char *out, const char *text, int32_t len)
{
    void       *ic  = TA_AT(ta, TA_INPUTCHAR_AT);
    uint8_t    *raw = (uint8_t *)TA_AT(ta, TA_RAW_AT);
    const char *p;
    const char *next      = NULL;
    const char *segStart;
    const char *segEnd    = NULL;
    const char *copyFrom;
    const char *markStart = NULL;
    const char *slash     = NULL;
    char       *partA = NULL;
    char       *partB = NULL;
    char       *partC = NULL;
    int32_t     state;
    int32_t     flagCopy = 0;
    int32_t     flagSnlk = 0;
    int32_t     flagAnno = 0;
    int32_t     outLen   = 0;
    int32_t     n;
    int16_t     charIndex = 0;
    int16_t     runLen    = 0;

    (void)len;
    out[0] = '\0';
    *(int16_t *)((uint8_t *)ic + IC_LENGTH) = 0;

    /* Whatever was read before is skipped: its marks are not 0xff. */
    p = text;
    while (raw[charIndex] != 0xff) {
        p += ju_IsDBCSLeadByte(*p) ? 2 : 1;
        charIndex++;
    }
    n = (int32_t)(p - text);
    if (n > 0) {
        strncpy(out, text, (size_t)n);
        out[n] = '\0';
        outLen = n;
    }
    segStart = p;
    copyFrom = p;

    if (ju_DbCmp(p, "\x81\x40")) {
        state = 4;
    } else if (ju_IsSNLKDelim(p)) {
        state = 3;
    } else if (*p == '`') {
        state = 5;
    } else if (ju_IsNum(*p) || ju_IsAlpha(*p) || ju_IsDBCSLeadByte(*p)) {
        runLen++;
        state = 0;
    } else {
        state = 4;
    }

    while (*p != '\0') {
        next = p + (ju_IsDBCSLeadByte(*p) ? 2 : 1);

        switch (state) {
        case 0:
            if (next[0] == '\0') {
                segEnd = next;
                flagCopy = 1;
            } else if (ju_IsSNLKDelim(next)) {
                state = 3;
            } else if ((uint32_t)(int32_t)*p < 0x20) {
                state = 4;
            } else if (next[0] == '`') {
                segEnd = next;
                flagCopy = 1;
                state = 5;
            } else if (next[0] == '~'
                       && (ju_IsHiragana(next + 1) || ju_IsKatakana(next + 1)
                           || next[1] == '^')) {
                markStart = next + 1;
                state = 1;
            } else {
                runLen++;
                if (runLen > 0x20)
                    state = 4;
            }
            break;

        case 1:
            if (next[0] == '\0') {
                segEnd = next;
                flagSnlk = 1;
                flagAnno = 1;
            } else if (ju_IsSNLKDelim(next)) {
                segEnd = next;
                flagSnlk = 1;
                flagAnno = 1;
                state = 3;
            } else if (p == markStart - 1) {
                /* the character the tilde itself sits on */
            } else if (next[0] == '/') {
                slash = next;
                state = 2;
            } else if (next[0] == '^' || ju_IsHiragana(next)
                       || ju_IsKatakana(next)
                       || (ju_IsLongVowel(next) && next != markStart)) {
                /* still inside the reading */
            } else {
                state = 4;
            }
            break;

        case 2:
            if (next[0] == '\0') {
                segEnd = next;
                flagSnlk = 1;
                flagAnno = 1;
            } else if (ju_IsSNLKDelim(next)) {
                segEnd = next;
                flagSnlk = 1;
                flagAnno = 1;
                state = 3;
            } else if (ju_IsHiragana(next) || ju_IsKatakana(next)
                       || (ju_IsLongVowel(next) && next != slash + 1)) {
                /* still inside the second reading */
            } else {
                state = 4;
            }
            break;

        case 3:
            if (next[0] == '\0') {
                segEnd = next;
                flagCopy = 1;
            } else if (next[0] == '`') {
                segEnd = next;
                flagCopy = 1;
                state = 5;
            } else if (ju_DbCmp(next, "\x81\x40")) {
                segEnd = next;
                flagCopy = 1;
                state = 4;
            } else if (!ju_IsSNLKDelim(next)) {
                segEnd = next;
                flagCopy = 1;
                state = 0;
                runLen = 1;
            }
            break;

        case 4:
            if (next[0] == '\0') {
                segEnd = next;
                flagCopy = 1;
            } else if (next[0] == '`') {
                segEnd = next;
                flagCopy = 1;
                state = 5;
            } else if (ju_IsSNLKDelim(next)) {
                state = 3;
            }
            break;

        case 5:
            if (next[0] == '\0') {
                segEnd = next;
                flagCopy = 1;
            } else if (ta_processSnlkAnno(ta, p, &partA, &partB, &next) == 0) {
                segEnd   = next;
                flagAnno = 1;
                if (ju_IsSNLKDelim(next))
                    state = 3;
                else if (next[0] != '`')
                    state = 4;
            } else {
                state = 4;
            }
            break;

        default:
            break;
        }

        if (flagCopy != 0) {
            const char *q = copyFrom;

            while (q != segEnd) {
                if (raw[charIndex] == 0xff)
                    raw[charIndex] = 0;
                q += ju_IsDBCSLeadByte(*q) ? 2 : 1;
                charIndex++;
            }
            n = (int32_t)(segEnd - copyFrom);
            strncpy(out + outLen, copyFrom, (size_t)n);
            out[outLen + n] = '\0';
            outLen   += n;
            flagCopy  = 0;
            slash     = NULL;
            markStart = NULL;
            copyFrom  = segEnd;
            segStart  = copyFrom;
        }

        if (flagSnlk != 0) {
            partC = NULL;
            partB = NULL;
            partA = NULL;
            n = (int32_t)(markStart - segStart - 1);
            partA = cpp_new((uint32_t)(n + 1));
            strncpy(partA, segStart, (size_t)n);
            partA[n] = '\0';
            if (slash == NULL) {
                n = (int32_t)(segEnd - markStart);
                partB = cpp_new((uint32_t)(n + 1));
                strncpy(partB, markStart, (size_t)n);
                partB[n] = '\0';
            } else {
                n = (int32_t)(slash - markStart);
                partB = cpp_new((uint32_t)(n + 1));
                strncpy(partB, markStart, (size_t)n);
                partB[n] = '\0';
                /* The reading after the slash is built and then given up
                   again: the reader's table takes two strings, not three. */
                n = (int32_t)(segEnd - slash);
                partC = cpp_new((uint32_t)(n + 1));
                strncpy(partC, slash + 1, (size_t)n);
                partC[n] = '\0';
            }
            flagSnlk = 0;
        }

        if (flagAnno != 0) {
            const char *q;

            if (ic_AddSnlkTable(ic, charIndex, partA, partB, 2) != 0) {
                if (partA != NULL) { cpp_delete(partA); partA = NULL; }
                if (partB != NULL) { cpp_delete(partB); partB = NULL; }
                if (partC != NULL) { cpp_delete(partC); partC = NULL; }
                return -1;
            }
            for (q = partA; *q != '\0'; ) {
                raw[charIndex] = (uint8_t)(q == partA ? 1 : 2);
                q += ju_IsDBCSLeadByte(*q) ? 2 : 1;
                charIndex++;
            }
            strcat(out, partA);
            outLen   += (int32_t)strlen(partA);
            markStart = NULL;
            slash     = NULL;
            flagAnno  = 0;
            copyFrom  = segEnd;
            segStart  = copyFrom;
            if (partA != NULL) { cpp_delete(partA); partA = NULL; }
            if (partB != NULL) { cpp_delete(partB); partB = NULL; }
            if (partC != NULL) { cpp_delete(partC); partC = NULL; }
        }

        p = next;
    }
    return 0;
}

/* ---- making and unmaking one ------------------------------------------ */

/* The analysis is the object every other class in the romanizer is handed a
   reference to, so its constructor names its owner and clears the six it will
   make; nothing is allocated until `initialize'. */
void *ta_ctor(void *ta, void *romanizer)
{
    TA_AT(ta, TA_OWNER_AT)       = romanizer;
    TA_AT(ta, TA_FORMATTED_AT)   = NULL;
    TA_AT(ta, TA_INPUTCHAR_AT)   = NULL;
    TA_AT(ta, TA_ANNOTATION_AT)  = NULL;
    TA_AT(ta, TA_DICTSEARCH_AT)  = NULL;
    TA_AT(ta, TA_JPATH_AT)       = NULL;
    TA_AT(ta, TA_PHRASEBUF_AT)   = NULL;
    TA_AT(ta, TA_PHRASETABLE_AT) = NULL;
    TA_AT(ta, TA_NORMALIZER_AT)  = NULL;
    return ta;
}

/* And the six made, each asked for out of the heap and then given the analysis
 * to belong to. Every one is asked for before any is checked, so a failure
 * part way through still leaves a record that can be unmade; the checks come
 * afterwards, in the same order, and nought is any of them.
 *
 * The phrase table is the odd one: it has no constructor of its own, so this
 * writes its owner itself and then asks it to make the number reader.
 */
int32_t ta_initialize(void *ta)
{
    void *p;

    /* The sizes IBM asks for are its own records; ours are those plus the
       pointers parked past each, which is what the _ROOM names hold. */
    p = cpp_new(IC_ROOM);
    TA_AT(ta, TA_INPUTCHAR_AT) = p != NULL ? ic_ctor(p, ta) : NULL;

    p = cpp_new((uint32_t)sizeof(Annotation));
    TA_AT(ta, TA_ANNOTATION_AT) = p != NULL
                                  ? (void *)an_ctor((Annotation *)p, ta)
                                  : NULL;

    p = cpp_new(DS_ROOM);
    TA_AT(ta, TA_DICTSEARCH_AT) = p != NULL ? dsr_ctor(p, ta) : NULL;

    p = cpp_new(JP_ROOM);
    TA_AT(ta, TA_JPATH_AT) = p != NULL ? jp_ctor(p, ta) : NULL;

    p = cpp_new(PB_ROOM);
    TA_AT(ta, TA_PHRASEBUF_AT) = p != NULL ? pb_ctor(p, ta) : NULL;

    p = cpp_new(PTB_ROOM);
    if (p != NULL)
        *(void **)((uint8_t *)p + PTB_OWNER_AT) = ta;
    TA_AT(ta, TA_PHRASETABLE_AT) = p;

    p = cpp_new(TN_ROOM);
    TA_AT(ta, TA_NORMALIZER_AT) = p != NULL ? tn_ctor(p) : NULL;

    TA_AT(ta, TA_FORMATTED_AT) = NULL;
    TA_AT(ta, TA_RAW_AT)       = NULL;

    if (TA_AT(ta, TA_INPUTCHAR_AT) == NULL)
        return 0;
    if (TA_AT(ta, TA_ANNOTATION_AT) == NULL)
        return 0;
    if (TA_AT(ta, TA_DICTSEARCH_AT) == NULL)
        return 0;
    if (TA_AT(ta, TA_JPATH_AT) == NULL)
        return 0;
    if (TA_AT(ta, TA_PHRASEBUF_AT) == NULL)
        return 0;
    if (TA_AT(ta, TA_PHRASETABLE_AT) == NULL
        || ptb_initialize(TA_AT(ta, TA_PHRASETABLE_AT)) == 0)
        return 0;
    if (TA_AT(ta, TA_NORMALIZER_AT) == NULL)
        return 0;

    memset((uint8_t *)ta + TA_PHRASE, 0,
           (size_t)TA_PHRASE_N * TA_PHRASE_SIZE);
    return 1;
}

/* Unmaking one, in the order IBM unmakes them: the reader first, with the
   chain of annotation marks emptied before the block goes, then the other five
   through their own destructors, then the two text buffers, then the
   normaliser. */
void ta_dtor(void *ta)
{
    if (TA_AT(ta, TA_INPUTCHAR_AT) != NULL) {
        ic_DeleteSnlkTable(TA_AT(ta, TA_INPUTCHAR_AT));
        cpp_delete(TA_AT(ta, TA_INPUTCHAR_AT));
    }
    if (TA_AT(ta, TA_ANNOTATION_AT) != NULL)
        an_destroy((Annotation *)TA_AT(ta, TA_ANNOTATION_AT), 1);
    if (TA_AT(ta, TA_DICTSEARCH_AT) != NULL)
        dsr_destroy(TA_AT(ta, TA_DICTSEARCH_AT), 1);
    if (TA_AT(ta, TA_JPATH_AT) != NULL)
        jp_destroy(TA_AT(ta, TA_JPATH_AT), 1);
    if (TA_AT(ta, TA_PHRASEBUF_AT) != NULL)
        pb_destroy(TA_AT(ta, TA_PHRASEBUF_AT), 1);
    if (TA_AT(ta, TA_PHRASETABLE_AT) != NULL)
        ptb_destroy(TA_AT(ta, TA_PHRASETABLE_AT), 1);
    if (TA_AT(ta, TA_FORMATTED_AT) != NULL)
        cpp_delete(TA_AT(ta, TA_FORMATTED_AT));
    if (TA_AT(ta, TA_RAW_AT) != NULL)
        cpp_delete(TA_AT(ta, TA_RAW_AT));
    if (TA_AT(ta, TA_NORMALIZER_AT) != NULL) {
        tn_dtor(TA_AT(ta, TA_NORMALIZER_AT));
        cpp_delete(TA_AT(ta, TA_NORMALIZER_AT));
    }
}

void *ta_destroy(void *ta, int32_t freeIt)
{
    ta_dtor(ta);
    if (freeIt & 1)
        cpp_delete(ta);
    return ta;
}

/* The first row of the phrase table, which is what Romanizer reads the answer
   out of. */
void *ta_GetPhraseTableRoot(void *ta)
{
    return *(void **)((uint8_t *)TA_AT(ta, TA_PHRASETABLE_AT) + PTB_HEAD_AT);
}
