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
#include <string.h>
#include "jprom.h"
#include "txtanal.h"
#include "inputchar.h"
#include "phrasebuf.h"
#include "phrasetable.h"
#include "intonphrase.h"

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
