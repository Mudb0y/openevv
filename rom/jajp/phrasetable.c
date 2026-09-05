/* PhraseTable: where a phrase becomes a row of the phrase table.
 *
 * The path search has already decided how the sentence breaks up and
 * `PhraseBuf' has already turned each break into an accent phrase. What is
 * left is to write that phrase out as the record the intonation and the
 * prosody read: the moras of the whole phrase in one run rather than one run
 * a word, the accent of the compound rather than of each word, the kana, and
 * one part of speech for the phrase rather than one for each word in it.
 * That record is `_PHR_TBL_T', which `rom/jajp/intonphrase.h' maps as PT_.
 *
 * Almost everything here is a bit test over a part of speech and the tags
 * IBM keeps beside it in `DictMan', which is why so many of the methods are
 * short. The three long ones are the entry point, the compounding, and the
 * two that read a run of digits through `NumRead'.
 */

#include <stdint.h>
#include <string.h>
#include "jprom.h"
#include "phrasetable.h"
#include "phrasebuf.h"
#include "intonphrase.h"
#include "txtanal.h"
#include "numread.h"

#define PTB_P(pt, off)      ((uint8_t *)(pt) + (off))
#define PTB_B(pt, off)      (*PTB_P((pt), (off)))
#define PTB_W(pt, off)      (*(int16_t *)PTB_P((pt), (off)))
#define PTB_OWNER_OF(pt)    (*(void **)PTB_P((pt), PTB_OWNER_AT))
#define PTB_HEAD_OF(pt)     (*(void **)PTB_P((pt), PTB_HEAD_AT))
#define PTB_TAIL_OF(pt)     (*(void **)PTB_P((pt), PTB_TAIL_AT))
#define PTB_NUMREAD_OF(pt)  (*(void **)PTB_P((pt), PTB_NUMREAD_AT))

/* ---- what a tag says a word is -------------------------------------- */

/* IBM writes this as a switch of a hundred and twenty-seven arms over the
   tag, and the compiler makes a table of it; this is that table with the
   answer of each arm folded in. Nought is the arms it has none for, and a
   tag with no arm that falls between 0x1c and 0x4b is a noun regardless,
   which is the default the switch ends on. */
static const uint8_t PTB_POS_OF_TG[0x7f] = {
    1, 1, 4, 4, 4, 2, 5, 2, 2, 7, 3, 5, 1, 1, 6, 1,
    4, 2, 2, 2, 2, 2, 0, 2, 5, 2, 2, 4, 0, 3, 0, 3,
    3, 0, 7, 3, 0, 0, 0, 0, 0, 7, 0, 7, 6, 2, 0, 0,
    0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0,
    6, 5, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 8, 4, 4,
    1, 4, 1, 1, 5, 1, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 8, 1, 1, 1, 1, 0, 1, 1, 1,
};

uint8_t ptb_GetPosFromTG(void *pt, uint8_t tg)
{
    (void)pt;
    if (tg <= 0x7e && PTB_POS_OF_TG[tg] != 0)
        return PTB_POS_OF_TG[tg];
    return (tg >= 0x1c && tg <= 0x4b) ? 2 : 0;
}

/* A function word's is one of two and nothing else. */
uint8_t ptb_GetFzkPosFromTG(void *pt, uint8_t tg)
{
    (void)pt;
    if (tg < 0x45 || tg == 0x5a)
        return 9;
    return 10;
}

/* Which kind of affix a word's tag block names, in the order the tests are
   asked: a prefix beats a suffix, and a counter beats both. */
int16_t ptb_GetAffixType(void *pt, uint8_t *tg)
{
    (void)pt;
    if (tg[2] & 0x02)
        return 2;
    if (tg[2] & 0x01)
        return 3;
    if (tg[2] & 0x10)
        return 7;
    if (tg[3] & 0x01)
        return 4;
    if (tg[3] & 0x08)
        return 8;
    if ((tg[0] & 0xff) != 0 || (tg[1] & 0xe0) != 0)
        return 12;
    return 11;
}

/* ---- taking a row off the free list ---------------------------------- */

/* One row spliced out of the chain over the phrase table. `first' is the head
 * of the chain in use, `last' the row it ended on, `free_' the head of what is
 * spare, and `link' the chain itself: two sixteen-bit indices an entry, the
 * back link at nought and the forward link at two.
 *
 * There is a road in here where the row's own back link is never written --
 * the one where the chain already had a last -- and the test at the end reads
 * it. What it reads then is whatever `InitPhraseTable' left, which is the
 * circular chain that function builds; that is IBM's and it is reproduced.
 */
int16_t ptb_TableAllocPhrase(void *pt, uint16_t *first, uint16_t *last,
                             uint16_t *free_, uint8_t *link, uint16_t count)
{
    uint16_t a = *last;
    uint16_t b = *free_;

    (void)pt;
    if (b == count)
        return -1;

    *last  = b;
    *free_ = *(uint16_t *)(link + (size_t)b * 4 + 2);
    if (a != count) {
        *(uint16_t *)(link + (size_t)a * 4 + 2) = b;
    } else {
        *(uint16_t *)(link + (size_t)b * 4 + 2) = count;
        *(uint16_t *)(link + (size_t)b * 4)     = a;
        *(uint16_t *)(link + (size_t)*free_ * 4) = count;
    }
    if (*(int16_t *)(link + (size_t)*last * 4) == (int16_t)count)
        *first = *last;
    return (int16_t)*last;
}

/* And the same, over the analysis's own three words and its chain. The row
   that comes back has its own place in the table written into it, which is
   what everything that walks the table by index reads. */
void *ptb_PhraseAlloc(void *pt)
{
    void   *ta = PTB_OWNER_OF(pt);
    int16_t at;

    at = ptb_TableAllocPhrase(pt, (uint16_t *)((uint8_t *)ta + TA_FIRST),
                              (uint16_t *)((uint8_t *)ta + TA_LAST),
                              (uint16_t *)((uint8_t *)ta + TA_SPARE_18),
                              (uint8_t *)ta + TA_LINK, TA_LINK_N);
    if (at < 0)
        return NULL;

    *(int16_t *)((uint8_t *)ta + TA_PHRASE + (size_t)at * PT_ROW_SIZE
                 + PT_INDEX) = at;
    return (uint8_t *)ta + TA_PHRASE + (size_t)at * PT_ROW_SIZE;
}

/* A row taken and put on the end of this class's own chain, then cleared.
 *
 * The clearing is not a memset: the three runs of moras are cleared to
 * nought, the three of values to minus one, and the thirty-byte run that
 * follows them to minus one as well -- and only the first fifteen of each,
 * though two of the three runs are thirty long. That is IBM's and the rest of
 * the row is whatever the row before it left.
 */
void *ptb_GeneratePhraseTable(void *pt)
{
    void   *row = ptb_PhraseAlloc(pt);
    int16_t i;

    if (row == NULL)
        return row;

    if (PTB_TAIL_OF(pt) == NULL) {
        PTB_HEAD_OF(pt) = row;
        PTB_TAIL_OF(pt) = row;
    } else {
        void *w = PTB_TAIL_OF(pt);

        while (PT_NEXT_OF(w) != NULL)
            w = PT_NEXT_OF(w);
        PT_NEXT_SET(w, row);
        PTB_TAIL_OF(pt) = row;
    }

    PT_LINK(row) = 0;
    PTB_B(row, PT_GROUP) = 0;
    PTB_B(row, PT_MORAS) = 0;
    PTB_B(row, PT_FIRST_WORD) = 0;
    for (i = 0; i < PT_MORA_N; i++) {
        PTB_B(row, PT_MORA + i)        = 0;
        PTB_B(row, PT_MORA_ACC + i)    = 0;
        PTB_B(row, PT_MORA_HI + i)     = 0;
        PTB_B(row, PT_MORA_HI_ACC + i) = 0;
        PTB_W(row, PT_MORA_VAL + i * 2)    = -1;
        PTB_W(row, PT_MORA_HI_VAL + i * 2) = -1;
        PTB_W(row, PT_LONG + i * 2)        = -1;
        PTB_B(row, PT_LONG_B + i)          = 0xff;
    }
    for (i = 0; i < 6; i++)
        PTB_B(row, PT_RIGHT + i) = 0;
    for (i = 0; i < 4; i++)
        PTB_B(row, PT_LEFT + i) = 0;
    return row;
}

/* ---- what a function word does to the phrase in front of it ---------- */

/* A word's tag turned into bits on the phrase's own kakari record, which is
   what says how the phrase before it may attach. The order is IBM's: two of
   the arms clear a bit they have just set in the same byte. */
void ptb_ExtKKRPhrase(void *pt, uint8_t *kkr, int16_t tg, uint8_t *other)
{
    (void)pt;
    switch (tg) {
    case 0x4c:
        kkr[5] |= 0x10;
        break;
    case 0x50:
        kkr[5] |= 0x08;
        break;
    case 0x51:
        kkr[5] |= 0x04;
        break;
    case 0x52:
    case 0x53:
        kkr[5] |= 0x02;
        kkr[0] |= 0xe0;
        break;
    case 0x54:
        kkr[5] |= 0x01;
        break;
    case 0x55:
        kkr[1] &= 0x0c;
        break;
    case 0x56:
    case 0x57:
        if (other[0] & 0x01)
            kkr[0] &= 0xc0;
        break;
    default:
        break;
    }
}

/* And the same over the sub-kakari, which is four bytes rather than six and
   is what a phrase inside a compound carries. The third byte is not built out
   of bits here at all: it is cleared and then taken whole out of DictMan's
   own phrase table, one byte a tag. */
void ptb_SetSubUkeType(void *pt, uint8_t *uke, int16_t tg, uint8_t *flag)
{
    (void)pt;
    if ((tg >= 0x1a && tg <= 0x28) || (tg >= 0x01 && tg <= 0x0b)
        || tg == 0x33 || tg == 0x3d || tg == 0x3e) {
        uke[1] |= 0x80;
        uke[1] &= 0xfd;
    }
    if ((tg >= 0x2d && tg <= 0x32) || tg == 0x38 || tg == 0x39
        || (tg >= 0x29 && tg <= 0x2c) || tg == 0x34) {
        uke[0] |= 0x40;
        uke[0] &= 0xfd;
    }
    if (tg == 0x13)
        uke[1] = 0x20;
    if (tg == 0x5e)
        uke[1] |= 0x20;
    if ((tg >= 0x1a && tg <= 0x44) || tg == 0x49 || tg == 0x4a
        || tg == 0x52 || tg == 0x5f || tg == 0x61 || tg == 0x65) {
        uke[2] = 0;
        uke[2] |= dm_GetPhraseDataPtr()[0x1c8 + tg];
    }
    switch (tg) {
    case 0x4c:
        uke[3] |= 0x10;
        break;
    case 0x4e:
        uke[3] |= 0x40;
        break;
    case 0x50:
        uke[3] |= 0x08;
        break;
    case 0x51:
        uke[3] |= 0x04;
        break;
    case 0x53:
        uke[3] |= 0x02;
        break;
    case 0x54:
        uke[3] |= 0x01;
        break;
    default:
        break;
    }
    if (tg >= 0x4b && tg <= 0x52)
        *flag = 1;
}

/* The kakari of a phrase with no function word on the end of it, which is
   decided by the last content word alone. Note the two arms that assign
   rather than set a bit, and the last of all, which fills in a phrase no arm
   said anything about. */
void ptb_SetNoneFzkKKR(void *pt, uint8_t *kkr, void *wp)
{
    int16_t        last = (int16_t)(PTB_B(wp, WP_WORDS) - 1);
    const uint8_t *tg = dm_GetTGAt(*(WW_SLOT(wp, last) + WW_POS));

    (void)pt;
    if ((tg[2] & 0x01) || (tg[2] & 0x02) || (tg[2] & 0x20)) {
        if (*(WW_SLOT(wp, last) + WW_ATTR) & 0x80)
            kkr[0] |= 0x01;
        else
            kkr[0] |= 0xa0;
        if (PTB_B(wp, WP_WORDS) == 1
            && (dm_GetTGAt2(*(WW_SLOT(wp, 0) + WW_POS), 1) & 0xf0) == 0
            && dm_GetTGAt2(*(WW_SLOT(wp, 0) + WW_POS), 2) == 0x20)
            kkr[0] |= 0x03;
    }
    if (tg[2] & 0x08) {
        kkr[0] |= 0xd0;
        kkr[0] &= 0xdc;
    }
    if (tg[2] & 0x10)
        kkr[0] |= 0x20;
    if (tg[3] & 0x20)
        kkr[0] |= 0x02;
    if (PTB_B(wp, WP_TYPE) == 0x0a)
        kkr[0] |= 0xa0;
    if (tg[3] & 0x40)
        kkr[0] = 0x01;
    if (tg[3] & 0x04)
        kkr[0] = 0x03;
    if (kkr[0] == 0 && !(tg[2] & 0x04))
        kkr[0] = 0x84;
}

/* ---- what a phrase will attach to ------------------------------------ */

/* The kakari of a whole phrase, which is what says how the phrase in front of
 * it may attach to it. Three things decide it, in order: the function word on
 * the end if there is one, the tags of the last content word, and the phrase
 * kind. A compound gets two answers rather than one -- the first word's bits
 * in the first byte and the rest in the second -- unless the two words want
 * the same bit, in which case the two answers are folded into one.
 *
 * Minus one is not an error but a refusal: a phrase whose last word carries
 * one of two tags and whose accent is neither nought nor one cannot take a
 * phrase in front of it at all.
 */
int16_t ptb_SetUkeTypePhrase(void *pt, uint8_t *uke, void *wp)
{
    const uint8_t *tg0;
    const uint8_t *tgL;
    int16_t        last;
    uint8_t        fzk;
    uint8_t        at;

    (void)pt;
    fzk  = PTB_B(wp, WP_FZKS) == 0 ? 0
                                   : (uint8_t)(PTB_B(wp, WP_FZK) & 0x7f);
    last = (int16_t)(PTB_B(wp, WP_WORDS) - 1);
    tg0  = dm_GetTGAt(*(WW_SLOT(wp, 0) + WW_POS));
    tgL  = dm_GetTGAt(*(WW_SLOT(wp, last) + WW_POS));

    if (PTB_B(wp, WP_WORDS) > 1) {
        uint8_t a = 0;
        uint8_t b = 0;

        if (tg0[2] & 0x20) a |= 0x20;
        if (tg0[2] & 0xc0) a |= 0x40;
        if (tg0[2] & 0x10) a |= 0x08;
        if (tg0[2] & 0x08) a |= 0x10;
        if (tg0[0] != 0 || (tg0[1] & 0xf0)) a |= 0x80;
        if (tgL[2] & 0x20) b |= 0x20;
        if (tgL[2] & 0xc0) b |= 0x40;
        if (tgL[2] & 0x10) b |= 0x08;
        if (tgL[2] & 0x08) b |= 0x10;
        if (tgL[0] != 0 || (tgL[1] & 0xf0)) b |= 0x80;
        if (a & b) {
            at = 0;
        } else {
            uke[0] |= a;
            at = 1;
        }
    } else {
        at = 0;
    }

    if (fzk != 0) {
        if (PTB_B(wp, WP_TYPE) == 0x0a) {
            uke[at] = 0xe4;
            return 0;
        }
        if (tgL[0] != 0) {
            uke[at] |= 0x80;
            return 0;
        }
        if (tgL[1] != 0) {
            if (tgL[1] & 0xd0) {
                uke[at] |= 0x80;
                return 0;
            }
            /* Note this arm returns only where it set the bit; where it
               did not, the two tests below are asked as well. */
            if (tgL[1] & 0x0e) {
                if ((uke[at] & 0x80) || (fzk >= 0x23 && fzk <= 0x26)
                    || fzk == 0x1f || fzk == 0x0b) {
                    uke[at] |= 0x80;
                    return 0;
                }
            }
            if (tgL[1] & 0x20) {
                if (dm_GetPhraseDataPtr()[0x23b + fzk] == 1)
                    uke[at] |= 0x80;
                else
                    uke[at] |= 0x20;
            } else {
                uke[at] |= 0x20;
            }
            return 0;
        }
        if (tgL[2] != 0) {
            if (tgL[2] & 0xc0) {
                if (fzk >= 0x29 && fzk <= 0x32) {
                    uke[at] |= 0x40;
                    return 0;
                }
                if (fzk == 0x4e || fzk == 0x13) {
                    uke[at] |= 0x40;
                    uke[at] |= 0x20;
                    return 0;
                }
            }
            if (tgL[2] & 0x21) uke[at] |= 0x20;
            if (tgL[2] & 0x08) uke[at] |= 0x10;
            if (tgL[2] & 0x10) uke[at] = 0x08;
            if (tgL[2] & 0x02) uke[at] = 0x00;
        } else {
            uke[at] = 0;
        }
        return 0;
    }

    if (PTB_B(wp, WP_TYPE) == 0x06) {
        uke[at] = 0x02;
        return 0;
    }
    if (PTB_B(wp, WP_TYPE) == 0x0a) {
        uke[at] = 0xe4;
        return 0;
    }
    if (tgL[1] != 0) {
        if (tgL[1] & 0x30) {
            uint16_t v = *(uint16_t *)(WW_SLOT(wp, last) + WW_ACCENT);

            uke[at] |= 0x80;
            if (v != 0 && v != 1)
                return -1;
            return 0;
        }
        if (tgL[1] & 0x08) {
            uke[at] |= 0x80;
            uke[at] |= 0x20;
        }
    }
    if (tgL[2] != 0) {
        if (tgL[2] & 0x21) uke[at] |= 0x20;
        if (tgL[2] & 0x08) uke[at] |= 0x10;
        if (tgL[2] & 0x10) uke[at] |= 0x08;
        /* IBM writes nought over a nought here, which is a store that
           changes nothing and is kept so that the two sides read alike. */
        if (uke[at] == 0)
            uke[at] = 0;
        return 0;
    }
    if (tgL[3] & 0x20) {
        uke[at] = 0x02;
        return 0;
    }
    if (tgL[3] & 0x44)
        uke[at] = 0x01;
    else
        uke[at] = 0x00;
    return 0;
}

/* ---- what a run of function words does to the accent ----------------- */

/* A phrase and the function words after it go in; the groups they break into
 * and the accent of each come out.
 *
 * Two arrays carry the walk: how many moras each group has run to, and where
 * its accent falls. The rule for each word is three bytes, read a nibble at a
 * time -- the low nibble of the second says what kind of word it is, and what
 * that kind is decides which nibble of which byte says where the accent goes.
 * Nought to seven is an accent that many moras in; nine flattens the phrase;
 * thirteen to fifteen wraps round the sixteen and, when the mora it lands on
 * is one of the ones that cannot carry an accent, steps back one.
 *
 * Three of IBM's own oddities are kept. The wrap-round asks about the mora it
 * landed on in one arm and not in the other, though the two arms are
 * otherwise the same shape. The tail that folds a run back into one group
 * runs its two loops on one counter and indexes them with another, so the
 * first group's length is added to fourteen times and the same entry is
 * cleared fourteen times. And a run whose last word is a particle of one kind
 * has its accent pulled back a mora only where the group is longer than one.
 */
void ptb_FzkAccent(void *pt, uint8_t *in, uint8_t *out)
{
    uint8_t A[AI_N];      /* where the accent of each group falls */
    uint8_t B[AI_N];      /* how many moras it has run to */
    int16_t C[AI_N];      /* what the caller wants carried through */
    int16_t i, n = 0, m = 0;
    int16_t v, w;
    int32_t flagA, flagB;
    uint8_t t;

    (void)pt;
    if (in[AI_ACCENT] > in[AI_MORAS] && in[AI_KIND] != 2) {
        flagA = 1;
        in[AI_ACCENT] = 0;
    } else {
        flagA = 0;
    }

    for (i = 0; i < AI_N; i++) {
        A[i] = 0;
        B[i] = 0;
        /* IBM clears the first two and not this one, and then copies all
           fifteen of it into the answer -- so what a group the walk never
           reached carries out is whatever was on its stack. There is nothing
           to reproduce in that, and the entries that are reached are written
           before they are read either way. */
        C[i] = 0;
    }
    B[0] = in[AI_MORAS];
    A[0] = in[AI_ACCENT];
    flagB = ((B[0] == 1 && A[0] == 1) || in[AI_KIND] == 1) ? 1 : 0;

    for (i = 0; i < in[AI_WORDS]; i++) {
        const uint8_t *rule = AI_RULE_OF(in, i);

        v = (int16_t)(rule[1] % 16);
        if (v == 0)
            v = 10;
        if (v == 10 && A[n] > 0 && A[n] != B[n])
            v = 0;

        if (A[n] == 0)
            w = (int16_t)(rule[0] / 16);
        else if (A[n] >= B[n] && B[n] >= 1)
            w = (int16_t)(rule[0] % 16);
        else
            w = (int16_t)(rule[1] / 16);

        if (v >= 0 && v <= 7) {
            if (w >= 0 && w <= 7) {
                A[n] = (uint8_t)(B[n] + w);
            } else if (w == 9) {
                A[n] = 0;
            } else if (w >= 13 && w <= 15) {
                if (B[n] + w > 16)
                    A[n] = (uint8_t)(B[n] + w - 16);
                if (ds_IsOnin(in[AI_KANA + A[n] + m]))
                    A[n] = (uint8_t)(A[n] - 1);
            }

            if (v > in[AI_LEN + i]) {
                if (rule[2] / 16 < in[AI_LEN + i])
                    t = (uint8_t)(in[AI_LEN + i] - rule[2] / 16);
                else
                    t = (uint8_t)(v < in[AI_LEN + i] ? v : in[AI_LEN + i]);
            } else {
                t = (uint8_t)v;
            }

            B[n] = (uint8_t)(B[n] + t);
            C[n] = *(int16_t *)(in + AI_MARK + (size_t)i * 2);
            m    = (int16_t)(m + B[n]);
            if (B[n] != 0)
                n++;
            B[n] = (uint8_t)(in[AI_LEN + i] - t);
            A[n] = (uint8_t)(rule[2] % 16);
        } else if (v == 10) {
            if (w == 8) {
                if (rule[2] % 16 == 0)
                    A[n] = 0;
                else
                    A[n] = (uint8_t)(B[n] + rule[2] % 16);
            } else {
                A[n] = (uint8_t)(B[n] + w);
            }
            B[n] = (uint8_t)(B[n] + in[AI_LEN + i]);
        } else {
            if (w >= 0 && w <= 7) {
                A[n] = (uint8_t)(B[n] + w);
            } else if (w == 9) {
                A[n] = 0;
            } else if (w >= 13 && w <= 15) {
                if (B[n] + w > 16) {
                    if (flagB == 0)
                        A[n] = (uint8_t)(B[n] + w - 16);
                    if (ds_IsOnin(in[AI_KANA + A[n] + m]))
                        A[n] = (uint8_t)(A[n] - 1);
                }
            }
            B[n] = (uint8_t)(B[n] + in[AI_LEN + i]);
        }

        flagB = (in[AI_ENDS + i] == 1 && B[n] == A[n]) ? 1 : 0;
    }

    if (flagA != 0) {
        A[1] = 0;
        for (i = 0; B[i + 1] > 0; i++)
            A[0] = (uint8_t)(A[0] + B[i]);
        if (i > 1) {
            if (A[i] == 0)
                A[0] = 0;
            else
                A[0] = (uint8_t)(A[0] + A[i]);
        }
        for (n = 1; n < 15; n++)
            B[0] = (uint8_t)(B[0] + B[i]);
        for (n = 1; n < 15; n++) {
            B[i] = 0;
            A[i] = 0;
        }
    }

    if (in[AI_WORDS] != 0 && in[AI_AT79] == 3
        && in[AI_ENDS - 1 + in[AI_WORDS]] == 2) {
        for (i = 0; i < AI_N && B[i] != 0; i++)
            ;
        i--;
        if (A[i] == 0 || A[i] == B[i]) {
            if (B[i] != 1)
                A[i] = (uint8_t)(B[i] - 1);
        }
    }

    out[AO_MORAS]  = B[0];
    out[AO_ACCENT] = A[0];
    for (i = 0; i < 15; i++) {
        out[AO_LEN + i] = B[i + 1];
        out[AO_ACC + i] = B[i + 1] < A[i + 1] ? B[i + 1] : A[i + 1];
        *(int16_t *)(out + AO_MARK + (size_t)i * 2) = C[i];
    }
}

/* ---- the words of a phrase joined into one --------------------------- */

/* Where the accent of a compound falls, which is not where the accents of
 * the words in it fell.
 *
 * The walk is over the words of one phrase from the row's own first word,
 * and what it keeps is a run of groups: how many moras each has run to, where
 * its accent falls, and the offset into the caller's text that the group
 * began at. What decides how a word joins the group in front of it is the
 * kind of affix it is -- five kinds have arms of their own -- and, for two of
 * those five, a state left behind by the word before.
 *
 * The two arms that ask what the last mora of the previous word was do it by
 * hand rather than through `DictSearch::IsOnin', and the two hand-written
 * tests are not the same as each other or as that one. The first asks only
 * whether the code's high five bits are thirty or thirty-one; the second
 * asks that, or thirty-one with a low three of five, or a low three of six
 * whatever the high five are. IsOnin itself wants thirty, or thirty-one with
 * a low three of five or six. All three are transcribed as they are.
 */
int16_t ptb_CompoundWord(void *pt, void *wp, void *row)
{
    uint8_t  A[15];
    uint8_t  B[15];
    int16_t  C[15];
    void    *ta = PTB_OWNER_OF(pt);
    uint8_t  at = 0;
    uint8_t  state = 0;
    int16_t  i, start;

    for (i = 0; i < 15; i++) {
        B[i] = 0;
        A[i] = 0;
        C[i] = -1;
    }
    start = (int16_t)PTB_B(row, PT_FIRST_WORD);
    C[0]  = *(int16_t *)(WW_SLOT(wp, start) + WW_OFFSET);

    for (i = start; i < (int16_t)PTB_B(wp, WP_WORDS); i++) {
        const uint8_t *w   = WW_SLOT(wp, i);
        uint16_t       acc = (uint16_t)*(int16_t *)(w + WW_ACCENT);
        uint8_t        len = w[WW_KANALEN];
        int16_t        affix;

        affix = ptb_GetAffixType(pt, (uint8_t *)dm_GetTGAt(w[WW_POS]));

        if (affix == 2) {
            if (state != 0) {
                if (state == 5) {
                    B[at] = (uint8_t)(B[at] + len);
                    if (C[at] < 0)
                        C[at] = *(int16_t *)(w + WW_OFFSET);
                } else {
                    at++;
                }
            }
            if (acc == 7) {
                A[at] = (uint8_t)acc;
                state = 5;
            } else if (acc >= 8 && acc <= 13) {
                A[at] = (uint8_t)(acc - 8);
                state = 7;
            } else {
                A[at] = (uint8_t)acc;
                state = 6;
            }
            B[at] = len;
            C[at] = *(int16_t *)(w + WW_OFFSET);
        } else if (affix == 3) {
            if (acc == 0) {
                /* The word before this one, asked for without anyone asking
                   whether there is one: at the first word this reads the six
                   bytes in front of the run, which are the phrase's own head
                   in the buffer IBM packs these in. */
                uint8_t t = *(WW_SLOT(wp, i - 1) + WW_KANALEN);
                uint8_t u;

                if (t > 10)
                    u = *((uint8_t *)ta + TA_LONGWORD
                          + (size_t)*(WW_SLOT(wp, i - 1) + WW_KANA)
                            * TA_LONGWORD_SIZE + (size_t)(int16_t)(t - 1));
                else
                    u = *(WW_SLOT(wp, i - 1) + WW_ATTR + t);
                if (u / 8 == 0x1e || u / 8 == 0x1f)
                    A[at] = (uint8_t)(B[at] - 1);
                else
                    A[at] = B[at];
                B[at] = (uint8_t)(B[at] + len);
                if (C[at] < 0)
                    C[at] = *(int16_t *)(w + WW_OFFSET);
                state = 4;
            } else if (acc == 6) {
                A[at] = 0;
                B[at] = (uint8_t)(B[at] + len);
                state = 4;
            } else if (acc == 7) {
                if (A[at] == B[at])
                    A[at] = 0;
                B[at] = (uint8_t)(B[at] + len);
                state = 4;
            } else if (acc >= 8 && acc <= 13) {
                at++;
                A[at] = (uint8_t)(acc - 8);
                B[at] = len;
                C[at] = *(int16_t *)(w + WW_OFFSET);
                state = 3;
            } else if (acc == 15) {
                if (A[at] == B[at] || A[at] == 0) {
                    uint8_t t = *(WW_SLOT(wp, i - 1) + WW_KANALEN);
                    uint8_t u;

                    if (t > 10)
                        u = *((uint8_t *)ta + TA_LONGWORD
                              + (size_t)*(WW_SLOT(wp, i - 1) + WW_KANA)
                                * TA_LONGWORD_SIZE
                              + (size_t)(int16_t)(t - 1));
                    else
                        u = *(WW_SLOT(wp, i - 1) + WW_ATTR + t);
                    if (u / 8 == 0x1e || (u / 8 == 0x1f && u % 8 == 5)
                        || u % 8 == 6)
                        A[at] = (uint8_t)(B[at] - 1);
                    else
                        A[at] = B[at];
                }
                B[at] = (uint8_t)(B[at] + len);
                state = 4;
            } else {
                A[at] = (uint8_t)(B[at] + acc);
                B[at] = (uint8_t)(B[at] + len);
                state = 10;
            }
            if (w[WW_ATTR] & 0x04)
                state = 10;
        } else if (affix == 4) {
            if (state == 6) {
                if (acc == 0)
                    A[at] = (uint8_t)(B[at] + 1);
                else
                    A[at] = (uint8_t)(B[at] + acc);
                B[at] = (uint8_t)(B[at] + len);
                if (C[at] < 0)
                    C[at] = *(int16_t *)(w + WW_OFFSET);
            } else if (state == 0) {
                A[at] = (uint8_t)acc;
                B[at] = len;
                C[at] = *(int16_t *)(w + WW_OFFSET);
            } else if (state <= 8 || state == 10) {
                at++;
                A[at] = (uint8_t)acc;
                B[at] = len;
                C[at] = *(int16_t *)(w + WW_OFFSET);
            }
            state = 8;
        } else if (affix == 7) {
            if (acc > 0)
                A[at] = (uint8_t)(B[at] + acc);
            B[at] = (uint8_t)(B[at] + len);
            C[at] = *(int16_t *)(w + WW_OFFSET);
            state = 2;
        } else if (affix == 8 || affix == 11 || affix == 12) {
            if (affix == 8 && (state == 9 || state == 1)) {
                A[at] = 0;
                B[at] = (uint8_t)(B[at] + len);
                state = 9;
                continue;
            }
            if ((w[WW_ATTR] & 0x01)
                && (state == 1 || state == 9 || state == 8))
                state = 10;

            if (state == 0) {
                B[at] = len;
                A[at] = (uint8_t)acc;
                C[at] = *(int16_t *)(w + WW_OFFSET);
            } else if (state == 2 || state == 7 || state == 10) {
                at++;
                B[at] = len;
                A[at] = (uint8_t)acc;
                C[at] = *(int16_t *)(w + WW_OFFSET);
            } else if (state == 5) {
                if (acc == 0)
                    A[at] = 0;
                else
                    A[at] = (uint8_t)(B[at] + acc);
                B[at] = (uint8_t)(B[at] + len);
                if (C[at] < 0)
                    C[at] = *(int16_t *)(w + WW_OFFSET);
            } else if (state <= 10) {
                if (affix == 11 && (w[WW_ATTR] & 0x02)) {
                    A[at] = 0;
                    B[at] = (uint8_t)(B[at] + len);
                } else if (affix == 11 && acc == 0) {
                    if (i > 0 && *(WW_SLOT(wp, i - 1) + WW_KANALEN) == 1)
                        A[at] = 0;
                    else
                        A[at] = (uint8_t)(B[at] + 1);
                    B[at] = (uint8_t)(B[at] + len);
                } else if (affix == 11) {
                    if (acc == len)
                        A[at] = (uint8_t)(B[at] + 1);
                    else
                        A[at] = (uint8_t)(B[at] + acc);
                    B[at] = (uint8_t)(B[at] + len);
                } else if (acc == 0) {
                    A[at] = 0;
                    B[at] = (uint8_t)(B[at] + len);
                } else {
                    A[at] = (uint8_t)(B[at] + acc);
                    B[at] = (uint8_t)(B[at] + len);
                }
                if (C[at] < 0)
                    C[at] = *(int16_t *)(w + WW_OFFSET);
            }
            state = 1;
        }
    }

    at++;
    for (i = 0; i < (int16_t)at && i < 15; i++) {
        PTB_B(row, PT_MORA + i)     = B[i];
        PTB_B(row, PT_MORA_ACC + i) = A[i];
        PTB_W(row, PT_MORA_VAL + i * 2) = C[i];
    }
    return (int16_t)at;
}
