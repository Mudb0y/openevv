/* The words the dictionary did not know.
 *
 * When a stretch of text has been parsed and the phrase table still holds rows
 * the search could not read, this is what reads them: the run of characters is
 * taken as one unknown word, a reading is built for it out of what the
 * characters are rather than out of any dictionary, the function words that
 * followed it are put back on the end, and the phrase is settled in place of
 * the rows it replaces -- which are given back to the free list.
 *
 * Four methods of `TextAnalysis' are in this object. `UnknownWord' walks the
 * table looking for a run to reparse, `ReParsing' works out where the run
 * begins and ends and asks the reader for the characters, `GenUnknownPhrase'
 * builds the phrase, and `SetOneMoraWord' is the answer for a run of one
 * character, which is short enough that there is nothing to work out.
 */

#include <stdint.h>
#include <string.h>
#include "jprom.h"
#include "txtanal.h"
#include "phrasebuf.h"
#include "phrasetable.h"
#include "intonphrase.h"
#include "inputchar.h"
#include "dictsearch.h"

#define TA_AT(ta, which) (*(void **)((uint8_t *)(ta) + (which)))
#define PT_HEAD(pt)      (*(void **)((uint8_t *)(pt) + PTB_HEAD_AT))
#define PT_TAIL(pt)      (*(void **)((uint8_t *)(pt) + PTB_TAIL_AT))

/* How many candidate runs of function words are kept, and how long each may
   be: sixteen of twenty-five bytes, the last four of which are the tag bytes
   the run has in common. */
#define UK_CAND_N     16
#define UK_CAND_SIZE  0x19
#define UK_CAND_LEN   0x14
#define UK_CAND_MASK  0x15

/* One character's worth of unknown word, which is short enough that the
   reading is whatever the row already holds, with the accent and the two
   kakari cleared and five readings rewritten by hand. */
void ta_SetOneMoraWord(void *ta, void *row)
{
    uint8_t *t;
    int16_t  i;

    if (row != NULL)
        t = PT_NEXT_OF(row);
    else
        t = (uint8_t *)PT_HEAD(TA_AT(ta, TA_PHRASETABLE_AT));

    t[PT_TYPE] = 0;
    for (i = 0; i < 4; i++)
        t[PT_LEFT + i] = 0;
    for (i = 0; i < 6; i++)
        t[PT_RIGHT + i] = 0;

    switch (t[PT_KANA]) {
    case 0x48:
        t[PT_KANA]  = 0x98;
        t[PT_LEFT]  = 2;
        t[PT_RIGHT] = 4;
        break;
    case 0x4b:
        t[PT_KANA]  = 0xfb;
        t[PT_LEFT]  = 2;
        t[PT_RIGHT] = 4;
        break;
    case 0x24:
    case 0x64:
    case 0xa0:
        t[PT_LEFT]  = 2;
        t[PT_RIGHT] = 4;
        break;
    case 0xfd:
        t[PT_KANA]  = 0x32;
        t[PT_LEFT]  = 2;
        t[PT_RIGHT] = 0xe0;
        break;
    default:
        t[PT_LEFT]  = 2;
        t[PT_RIGHT] = 0xe0;
        break;
    }
}

/* Where the run of unknown characters begins and ends, and the characters
 * themselves fetched.
 *
 * The two rows the caller names bound the run: the one in front of it and the
 * one behind. With no row in front the run starts at the head of the table,
 * and with no row behind it runs to where the reader has reached. One is the
 * table being empty, which is nothing to do; minus two is the reader refusing
 * the characters.
 */
int16_t ta_ReParsing(void *ta, void *a, void *b)
{
    void   *pt = TA_AT(ta, TA_PHRASETABLE_AT);
    void   *ic = TA_AT(ta, TA_INPUTCHAR_AT);
    int32_t from;
    int32_t to;
    int16_t moras;
    int16_t fzk;
    int16_t rc;

    if (a == NULL) {
        from  = *(int32_t *)((uint8_t *)PT_HEAD(pt) + PT_COST);
        moras = *(int16_t *)((uint8_t *)PT_HEAD(pt) + PT_MORA_VAL);
    } else if (PT_NEXT_OF(a) == NULL) {
        return 1;
    } else if (((uint8_t *)a)[PT_KIND] != 0) {
        from  = *(int32_t *)((uint8_t *)PT_NEXT_OF(a) + PT_COST);
        moras = *(int16_t *)((uint8_t *)PT_NEXT_OF(a) + PT_MORA_VAL);
    } else {
        from  = *(int32_t *)((uint8_t *)a + PT_COST);
        moras = *(int16_t *)((uint8_t *)a + PT_MORA_VAL);
    }

    if (b != NULL)
        to = *(int32_t *)((uint8_t *)b + PT_COST);
    else
        to = *(int32_t *)((uint8_t *)ic + IC_POS);

    if (ic_GetUnknownKanji(ic, (int16_t)(moras - 1), from, to) < 0)
        return -2;

    fzk = ds_FzkParsingReverse(TA_AT(ta, TA_DICTSEARCH_AT));
    rc  = ta_GenUnknownPhrase(ta, a, b, fzk, moras);
    return rc < 0 ? -1 : rc;
}

/* Every run of rows the search could not read, one after another.
 *
 * The walk goes down the chain twice. The first pass finds the last row before
 * the run that the analysis is willing to attach to -- a row of the sixth or
 * fifth kind is not one, and neither is a row of the second kind or below, nor
 * one of the tenth, nor one whose kind field says it was settled. The second
 * pass runs on from there adding up the moras until it meets a row that was
 * settled or has read twenty-five moras' worth, and that is where the run
 * ends. Everything between is handed to `ReParsing'.
 */
void ta_UnknownWord(void *ta)
{
    void *pt = TA_AT(ta, TA_PHRASETABLE_AT);
    void *row;
    void *before;

    *(int16_t *)((uint8_t *)ta + TA_INTON_FAILED) = -1;

    row    = PT_HEAD(pt);
    before = NULL;
    while (row != NULL) {
        void   *p = PT_HEAD(pt);
        int16_t rc;

        for (; p != NULL; p = PT_NEXT_OF(p)) {
            uint8_t kind = ((uint8_t *)p)[PT_TYPE];

            if (kind == 6 || kind == 5)
                break;
            if (kind > 2 && kind != 0x0a && ((uint8_t *)p)[PT_KIND] == 1)
                continue;
            before = p;
        }
        if (p == NULL)
            break;

        {
            int16_t moras = 0;
            int16_t done  = 0;
            void   *end;

            for (; p != NULL; p = PT_NEXT_OF(p)) {
                if (((uint8_t *)p)[PT_TYPE] <= 1 || done != 0 || moras >= 0x19)
                    break;
                moras = (int16_t)(moras + ((uint8_t *)p)[PT_MORAS]);
                if (((uint8_t *)p)[PT_KIND] != 1)
                    done = 1;
            }
            if (moras >= 0x19) {
                end = PT_HEAD(pt);
                while (PT_NEXT_OF(end) != p)
                    end = PT_NEXT_OF(end);
                p = end;
            } else {
                end = p;
            }
            rc = ta_ReParsing(ta, before, end);
        }

        if (rc < 0) {
            if (rc == -2)
                *(int16_t *)((uint8_t *)ta + TA_INTON_FAILED) = 2;
            return;
        }
        if (rc == 1) {
            if (before != NULL) {
                if (PT_NEXT_OF(before) != NULL)
                    ((uint8_t *)PT_NEXT_OF(before))[PT_TYPE] = 0;
                else
                    ((uint8_t *)before)[PT_TYPE] = 0;
            } else {
                ((uint8_t *)PT_HEAD(pt))[PT_TYPE] = 0;
            }
        }
        row    = p;
        before = row;
    }

    *(int16_t *)((uint8_t *)ta + TA_INTON_FAILED) = 0;
}

/* The phrase itself, built out of what the characters are.
 *
 * First the runs of function words the search left behind are gathered: each
 * one is a chain through DictSearch's own table, and a run is kept only where
 * the three vector bytes its rule gives, masked, say something. Sixteen runs
 * at most, and the four bytes each has in common are kept beside it.
 *
 * Then the best run is chosen -- the one costing most moras, and between two
 * of equal cost the shorter -- so long as what is left of the text after it is
 * more than nothing.
 *
 * Then the phrase: a reading built from the characters one at a time, with the
 * hiragana read through the dictionary and the katakana turned into the codes
 * 0xf8 to 0xff, the function words of the chosen run put on the end out of the
 * same table, and a part of speech found by looking for the row of the tag
 * table whose four bytes are the run's own four.
 *
 * Last the splice: the phrase is settled where the run of rows was, every row
 * it replaces is given back to the free list, and the chain is joined up
 * again.
 *
 * A text of more than twenty-five characters, or of exactly one with no run
 * chosen, is handed to `SetOneMoraWord' instead.
 */
int16_t ta_GenUnknownPhrase(void *ta, void *a, void *b, int16_t nFzk,
                            int16_t at)
{
    static const uint8_t MASK[3] = { 0xff, 0xfe, 0xfc };
    void    *pt = TA_AT(ta, TA_PHRASETABLE_AT);
    void    *ic = TA_AT(ta, TA_INPUTCHAR_AT);
    void    *ds = TA_AT(ta, TA_DICTSEARCH_AT);
    void    *pb = TA_AT(ta, TA_PHRASEBUF_AT);
    uint8_t  cand[UK_CAND_N][UK_CAND_SIZE];
    uint8_t  kana[0x20];
    uint8_t *wp;
    uint8_t *entry = (uint8_t *)ds + DS_ENTRY;
    void    *prev;
    void    *next;
    uint8_t  kind = 0;
    int16_t  nCand = 0;
    int16_t  bestCost = 0;
    int16_t  bestLen = 0;
    int16_t  bestAt = -1;
    int16_t  chars;
    int16_t  nKana;
    int16_t  moras;
    int16_t  count;
    int16_t  i, k;

    for (i = (int16_t)(nFzk - 1); i >= 0; i--) {
        uint8_t and4[4];
        uint8_t any = 0;
        uint8_t rule;

        for (k = 0; k < 4; k++)
            and4[k] = 0;
        rule = dm_GetFuncDict()[DS_FZK_S16(ds, i, PF_AT) + 4];
        for (k = 0; k < 3; k++) {
            and4[k] = (uint8_t)(dm_GetPhrVectorAt((uint16_t)((rule - 1) * 14
                                                             + k))
                                & MASK[k]);
            if (and4[k] > 0)
                any = 1;
        }
        if (any == 1 && nCand < UK_CAND_N) {
            int8_t w = (int8_t)i;

            k = 0;
            while (w >= 0) {
                cand[nCand][k] = (uint8_t)w;
                w = (int8_t)DS_FZK_B(ds, (int16_t)w, PF_LINK);
                k++;
            }
            cand[nCand][UK_CAND_LEN] = (uint8_t)k;
            for (k = 0; k < 4; k++)
                cand[nCand][UK_CAND_MASK + k] = and4[k];
            nCand++;
        }
    }

    count = *(int16_t *)((uint8_t *)ic + IC_COUNT);

    for (i = 0; i < nCand; i++) {
        int16_t cost = 0;
        int16_t len  = cand[i][UK_CAND_LEN];

        for (k = 0; k < len && k < 0x14; k++)
            cost = (int16_t)(cost + DS_FZK_B(ds, cand[i][k], PF_KANALEN));

        if (bestCost < cost || (bestCost == cost && bestLen >= len)) {
            if (count - cost > 0) {
                bestCost = cost;
                bestLen  = len;
                bestAt   = i;
            }
        }
    }

    if (count > 0x19 || (count == 1 && bestAt < 0)) {
        ta_SetOneMoraWord(ta, a);
        return 0;
    }

    wp    = WP_SLOT((uint8_t *)pb + PB_BUFFER, 0);
    chars = (int16_t)(count - bestCost);

    if (a == NULL) {
        *(int16_t *)(wp + WP_MORAS) = count;
        wp[WP_KANALEN] = (uint8_t)chars;
        *(int32_t *)(wp + WP_COST) =
            *(int32_t *)((uint8_t *)PT_HEAD(pt) + PT_COST);
        prev = NULL;
    } else if (((uint8_t *)a)[PT_KIND] != 0) {
        *(int16_t *)(wp + WP_MORAS) = count;
        wp[WP_KANALEN] = (uint8_t)chars;
        *(int32_t *)(wp + WP_COST) =
            *(int32_t *)((uint8_t *)PT_NEXT_OF(a) + PT_COST);
        prev = a;
    } else {
        chars = (int16_t)(count - bestCost - ((uint8_t *)a)[PT_HOLD]);
        if (chars > 0) {
            *(int16_t *)(wp + WP_MORAS) =
                (int16_t)(count - ((uint8_t *)a)[PT_HOLD]);
            wp[WP_KANALEN] = (uint8_t)chars;
            *(int32_t *)(wp + WP_COST) =
                *(int32_t *)((uint8_t *)PT_NEXT_OF(a) + PT_COST);
            prev = a;
        } else {
            *(int16_t *)(wp + WP_MORAS) = count;
            wp[WP_KANALEN] = (uint8_t)(chars + ((uint8_t *)a)[PT_HOLD]);
            *(int32_t *)(wp + WP_COST) = *(int32_t *)((uint8_t *)a + PT_COST);
            prev = PT_HEAD(pt);
            while (PT_NEXT_OF(prev) != a)
                prev = PT_NEXT_OF(prev);
        }
    }

    wp[WP_CHARS]  = 0;
    wp[WP_TYPE]   = 0x0a;
    wp[WP_WORDS]  = 1;
    wp[WP_FZKS]   = (uint8_t)bestLen;
    wp[WP_ACCENT] = 0;

    if (bestLen != 0) {
        uint8_t ok = 0;

        for (i = 0; i < 0xb4; i++) {
            if (ok == 1)
                break;
            ok = 1;
            for (k = 0; k < 4 && ok != 0; k++)
                if (cand[bestAt][UK_CAND_MASK + k]
                    != dm_GetTGAt2((uint8_t)i, (uint8_t)k))
                    ok = 0;
        }
        WW_SLOT(wp, 0)[WW_POS] = ok != 0 ? (uint8_t)(i - 1) : (uint8_t)0;
    } else {
        WW_SLOT(wp, 0)[WW_POS] = 0;
    }
    WW_SLOT(wp, 0)[WW_CHARS] = wp[WP_KANALEN];

    for (i = 0; i < *(int16_t *)((uint8_t *)ic + IC_COUNT); i++) {
        *((uint8_t *)ic + IC_TEXT + i * 2) =
            *((uint8_t *)ic + IC_SCRATCH + i * 2);
        *((uint8_t *)ic + IC_TEXT + i * 2 + 1) =
            *((uint8_t *)ic + IC_SCRATCH + i * 2 + 1);
    }
    *((uint8_t *)ic + IC_TEXT + i * 2)     = 0;
    *((uint8_t *)ic + IC_TEXT + i * 2 + 1) = 0;

    nKana = 0;
    for (i = 0; i < (int16_t)wp[WP_KANALEN] && nKana < 0x19; ) {
        switch (ic_GetCharType(ic, i)) {
        case 4:
            ds_ProcessHiragana(ds, i, entry);
            for (k = 0; k < (int16_t)entry[DE_KANALEN]; k++)
                kana[nKana++] = entry[DE_KANA + k];
            i = (int16_t)(i + entry[DE_CHARS]);
            break;
        case 8:
            kana[nKana] = (uint8_t)(kana[nKana - 1] % 8 + 0xf8);
            nKana++;
            i++;
            break;
        case 2:
            i++;
            break;
        default:
            return -1;
        }
    }

    WW_SLOT(wp, 0)[WW_KANALEN] = (uint8_t)nKana;
    *(int16_t *)(WW_SLOT(wp, 0) + WW_OFFSET) = at;
    moras = nKana;
    if (nKana > 9) {
        ds_SetLongWord(ds, nKana, entry, kana);
        WW_SLOT(wp, 0)[WW_KANA] = entry[DE_KANA];
    } else {
        for (k = 0; k < nKana; k++)
            WW_SLOT(wp, 0)[WW_KANA + k] = kana[k];
    }

    if (dm_GetTGAt2(WW_SLOT(wp, 0)[WW_POS], 2) & 0x20)
        *(int16_t *)(WW_SLOT(wp, 0) + WW_ACCENT) = 0;
    else
        *(int16_t *)(WW_SLOT(wp, 0) + WW_ACCENT) =
            (int16_t)WW_SLOT(wp, 0)[WW_KANALEN];

    for (i = 0; i < bestLen && i < 0x0f && moras < 0x19; i++) {
        int16_t  which = cand[bestAt][i];
        uint8_t *f = WF_SLOT(wp, i);
        int16_t  n;

        moras = (int16_t)(moras
                          + dm_GetFuncDict()[DS_FZK_S16(ds, which, PF_AT)] - 6);
        f[WF_CODE] = DS_FZK_B(ds, which, PF_CODE);
        *(int16_t *)(f + WF_AT) = DS_FZK_S16(ds, which, PF_AT);
        f[WF_KANALEN] = DS_FZK_B(ds, which, PF_KANALEN);
        n = (int16_t)((uint16_t)DS_FZK_S16(ds, which, PF_ACCENT)
                      - DS_FZK_B(ds, which, PF_KANALEN) + 1);
        *(int16_t *)(f + WF_ACCENT) =
            (int16_t)(*(int16_t *)((uint8_t *)ic + IC_COUNT) - 1
                      - (uint16_t)n);
        *(int16_t *)(f + WF_OFFSET) =
            *(int16_t *)((uint8_t *)ic + IC_OFFSET
                         + DS_FZK_S16(ds, which, PF_ACCENT) * 2);
    }

    if (b != NULL) {
        void *p = PT_HEAD(pt);

        while (PT_NEXT_OF(p) != b)
            p = PT_NEXT_OF(p);
        kind = ((uint8_t *)p)[PT_KIND];
    } else {
        void *p = PT_HEAD(pt);

        while (PT_NEXT_OF(p) != NULL)
            p = PT_NEXT_OF(p);
        kind = ((uint8_t *)p)[PT_KIND];
        PT_TAIL(pt) = prev;
    }

    if (prev != NULL) {
        next = PT_NEXT_OF(prev);
        PT_NEXT_SET(prev, b);
    } else {
        next = PT_HEAD(pt);
        PT_HEAD(pt) = b;
    }

    while (next != b && next != NULL) {
        void    *p = PT_NEXT_OF(next);
        uint16_t which = (uint16_t)*(int16_t *)((uint8_t *)next + PT_INDEX);

        ju_TableFree((uint16_t *)((uint8_t *)ta + TA_LAST),
                     (uint16_t *)((uint8_t *)ta + TA_SPARE_18),
                     (uint16_t *)((uint8_t *)ta + TA_TOP),
                     (uint8_t *)ta + TA_LINK, TA_LINK_N, which);
        next = p;
    }

    *((uint8_t *)ta + TA_UNKNOWN_10) = 0;
    *(int16_t *)((uint8_t *)ta + TA_INTON_FAILED) = 1;
    ta_Kakutei(ta, WP_SLOT((uint8_t *)pb + PB_BUFFER, 0));
    if (*(int16_t *)((uint8_t *)ta + TA_INTON_FAILED) < 0)
        return 1;

    ((uint8_t *)PT_TAIL(pt))[PT_KIND] = kind;
    if (PT_TAIL(pt) != PT_HEAD(pt)) {
        void *p = PT_HEAD(pt);
        void *last;

        while (p != NULL && PT_NEXT_OF(p) != PT_TAIL(pt))
            p = PT_NEXT_OF(p);
        last = PT_TAIL(pt);
        PT_TAIL(pt) = p;
        PT_NEXT_SET(PT_TAIL(pt), NULL);
        if (prev != NULL) {
            PT_NEXT_SET(prev, last);
            PT_NEXT_SET(last, b);
        } else {
            PT_HEAD(pt) = last;
            PT_NEXT_SET(last, b);
        }
    }
    return 0;
}
