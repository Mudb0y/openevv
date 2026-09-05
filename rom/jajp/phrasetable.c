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

/* ---- a run of digits written into rows ------------------------------- */

/* What `NumRead' made of a run of digits, written out as rows.
 *
 * The first part of the phrase -- the words in front of the digits, which the
 * caller has copied aside -- goes into the row as it stands, one mora entry a
 * word. Then each of the readings but the last gets a row of its own, taken
 * off the free list as it is needed, with the codes of the reading as the
 * kana and one mora entry per pair of numbers the reading carries.
 *
 * Note two things IBM does that a reading of the listing has to keep. The
 * length taken off the phrase's own mora count after the first part is the
 * one belonging to the entry after the last it copied, which is whatever the
 * caller left in it. And the offset written beside every mora of a reading is
 * the offset of the first word after the front part, the same one each time.
 */
void *ptb_SetSuushiPhraseTable(void *pt, void *wp, void *row, uint8_t *jrt,
                               int16_t before, int16_t n)
{
    void   *ta = PTB_OWNER_OF(pt);
    int16_t a = 0;     /* which long entry is being written */
    int16_t b = 0;     /* how many moras have gone by */
    int16_t pos = 0;   /* where the next kana code goes */
    int16_t m = 0;     /* which mora entry is being written */
    int16_t z = 0;     /* which of the left marks */
    int16_t k, d, q;

    PTB_B(row, PT_MORAS) = 0;
    PTB_B(row, PT_HOLD)  = 0;

    if (before > 0) {
        for (k = 0; k < before; k++) {
            uint8_t *j = jrt + (size_t)k * WP_WORD_SIZE;
            int16_t  c;

            PTB_B(row, PT_MORAS) = (uint8_t)(PTB_B(row, PT_MORAS)
                                             + j[WW_KANALEN]);
            PTB_B(row, PT_HOLD)  = (uint8_t)(PTB_B(row, PT_HOLD)
                                             + j[WW_CHARS]);
            PTB_B(row, PT_LONG_B + a) = 8;
            PTB_W(row, PT_LONG + a * 2) = b;
            a++;

            c = (int16_t)*(WW_SLOT(wp, k) + WW_KANALEN);
            if (c > 9) {
                for (d = 0; d < c; d++) {
                    PTB_B(row, PT_KANA + pos) =
                        *((uint8_t *)ta + TA_LONGWORD
                          + (size_t)*(WW_SLOT(wp, k) + WW_KANA)
                            * TA_LONGWORD_SIZE + (size_t)d);
                    pos++;
                }
            } else {
                for (d = 0; d < (int16_t)j[WW_KANALEN]; d++) {
                    PTB_B(row, PT_KANA + pos) = j[WW_KANA + d];
                    pos++;
                }
            }

            PTB_B(row, PT_MORA + m) = j[WW_KANALEN];
            PTB_W(row, PT_MORA_VAL + m * 2) = *(int16_t *)(j + WW_OFFSET);
            if ((uint16_t)*(int16_t *)(j + WW_ACCENT) >= 8)
                PTB_B(row, PT_MORA_ACC + m) =
                    (uint8_t)((uint16_t)*(int16_t *)(j + WW_ACCENT) - 8);
            else
                PTB_B(row, PT_MORA_ACC + m) = j[WW_ACCENT];
            m++;
        }
        PTB_B(row, PT_LEFT) = 8;
        PTB_W(wp, WP_MORAS) = (int16_t)((uint16_t)PTB_W(wp, WP_MORAS)
                                        - jrt[(size_t)before * WP_WORD_SIZE
                                              + WW_CHARS]);
        z = 1;
    } else {
        z = 0;
    }

    for (q = 0; q < n - 1; q++) {
        const uint8_t *rd = (const uint8_t *)PTB_NUMREAD_OF(pt) + NR_READ
                            + (size_t)q * NR_READ_SIZE;
        int16_t        pairs;

        if (q == 0 && before > 0) {
            PTB_B(row, PT_MORAS) = (uint8_t)(PTB_B(row, PT_MORAS)
                                             + rd[RD_COUNT]);
            PTB_B(row, PT_HOLD)  = (uint8_t)(PTB_B(row, PT_HOLD)
                                             + rd[RD_LEN]);
        } else {
            PTB_B(row, PT_MORAS) = rd[RD_COUNT];
            PTB_B(row, PT_HOLD)  = rd[RD_LEN];
        }
        PTB_B(row, PT_KIND) = 1;
        PTB_B(row, 0x0d)    = 0;
        PTB_B(row, 0x0e)    = 0;
        /* Where this reading's moras start counting from is where its kana
           start, which for a row of its own is nought. */
        b = pos;

        for (d = 0; d < (int16_t)rd[RD_COUNT]; d++) {
            PTB_B(row, PT_KANA + pos) = rd[RD_CODES + d];
            pos++;
        }

        pairs = NR_S16(PTB_NUMREAD_OF(pt), NR_ANSWER + q * 2);
        for (d = 0; d < pairs; d++) {
            PTB_B(row, PT_MORA + m) = rd[RD_A + d * RD_PAIR_SIZE];
            PTB_W(row, PT_MORA_VAL + m * 2) =
                *(int16_t *)(WW_SLOT(wp, before) + WW_OFFSET);
            PTB_B(row, PT_MORA_ACC + m) = rd[RD_B + d * RD_PAIR_SIZE];
            m++;
            PTB_B(row, PT_LONG_B + a) = 1;
            PTB_W(row, PT_LONG + a * 2) = b;
            a++;
            b = (int16_t)(b + *(const int16_t *)(rd + RD_A
                                                 + d * RD_PAIR_SIZE));
        }

        PTB_B(row, PT_LEFT + z) = 0x20;
        PTB_B(row, PT_RIGHT)    = 0x21;
        *(int32_t *)((uint8_t *)row + PT_COST) =
            *(int32_t *)((uint8_t *)wp + WP_COST);
        PTB_W(wp, WP_MORAS) = (int16_t)((uint16_t)PTB_W(wp, WP_MORAS)
                                        - rd[RD_LEN]);
        PT_LINK(row) = 0;

        row = ptb_GeneratePhraseTable(pt);
        if (row == NULL)
            return row;
        m   = 0;
        pos = 0;
        m   = 0;
        z   = 0;
        PTB_B(row, PT_FIRST_WORD) = (uint8_t)before;
        a   = 0;
    }
    return row;
}

/* A phrase whose words are digits, read through `NumRead'.
 *
 * The phrase is taken apart into the words in front of the digits, the digits
 * themselves and the words after them; the digits go to the number reader and
 * what comes back is one or more readings; and the phrase is put back
 * together out of the front, the last reading and the back. Where the reader
 * produced more than one reading, all but the last become rows of their own
 * through `SetSuushiPhraseTable' and only the last stays in the phrase.
 *
 * A reading longer than the nine codes a word can hold goes into the
 * analysis's own store of long readings and the word keeps its number; thirty
 * is all that store holds and a thirty-first is what this refuses on.
 *
 * The two arrays it takes the phrase apart into sit next to each other in
 * IBM's frame, so the first running over reaches the second. Ours are one
 * buffer for the same reason: a phrase of more words than the first will hold
 * has to land where IBM's lands.
 */
void *ptb_SetSuushiPhrase(void *pt, void *wp, void *row, int16_t *out)
{
    void    *ta = PTB_OWNER_OF(pt);
    uint8_t  frame[0x180];
    uint8_t *jrtA = frame;
    uint8_t *jrtB = frame + 0xc8;
    uint8_t  before = 0;
    uint8_t  after = 0;
    int16_t  digits = 0;
    int16_t  i, k, r, d, at, from;
    int16_t  n, nLast, p, q;

    memset(frame, 0, sizeof frame);
    for (i = 0; i < (int16_t)PTB_B(wp, WP_WORDS); i++) {
        if (dm_GetTGAt2(*(WW_SLOT(wp, i) + WW_POS), 3) & 0x10)
            digits++;
        else if (digits == 0)
            before++;
        else
            after++;
    }

    for (k = 0; k < (int16_t)before; k++)
        ta_CopyJrtPart(WW_SLOT(wp, k), jrtA + (size_t)k * WP_WORD_SIZE);

    k = 0;
    for (i = (int16_t)(digits + before); i < (int16_t)PTB_B(wp, WP_WORDS);
         i++, k++)
        ta_CopyJrtPart(WW_SLOT(wp, i), jrtB + (size_t)k * WP_WORD_SIZE);

    p = (int16_t)before;
    q = 0;
    if ((int16_t)PTB_B(wp, WP_WORDS) > (int16_t)before)
        n = nr_Do(PTB_NUMREAD_OF(pt), wp, &p, &q);
    else
        n = 0;

    if (n > 1) {
        row = ptb_SetSuushiPhraseTable(pt, wp, row, jrtA, (int16_t)before, n);
        if (row == NULL)
            return NULL;
    } else if (n == 0) {
        at = 0;
        for (k = 0; k < (int16_t)before; k++, at++)
            ta_CopyJrtPart(jrtA + (size_t)k * WP_WORD_SIZE, WW_SLOT(wp, at));
        for (k = 0; k < (int16_t)after; k++, at++)
            ta_CopyJrtPart(jrtB + (size_t)k * WP_WORD_SIZE, WW_SLOT(wp, at));
        PTB_B(wp, WP_WORDS) = (uint8_t)(before + after);
        PTB_B(wp, WP_CHARS) = 0;
        PTB_B(wp, WP_TYPE)  = 0;
        *out = ptb_CompoundWord(pt, wp, row);
        return row;
    }

    PTB_B(wp, WP_CHARS) = 0;
    PTB_B(wp, WP_TYPE)  = 0;
    {
        int16_t sum = 0;

        for (i = 0; i < (int16_t)PTB_B(wp, WP_FZKS); i++)
            sum = (int16_t)(sum + *(WF_SLOT(wp, i) + WF_KANALEN));
        PTB_B(wp, WP_KANALEN) =
            (uint8_t)((uint16_t)PTB_W(wp, WP_MORAS) - sum);
    }
    nLast = NR_S16(PTB_NUMREAD_OF(pt), NR_ANSWER + (n - 1) * 2);
    PTB_B(wp, WP_WORDS) = (uint8_t)(nLast + after + before);

    from = 0;
    at   = 0;
    for (k = 0; k < (int16_t)before; k++, at++)
        ta_CopyJrtPart(jrtA + (size_t)k * WP_WORD_SIZE, WW_SLOT(wp, at));

    for (r = 0; r < NR_S16(PTB_NUMREAD_OF(pt), NR_ANSWER + (n - 1) * 2);
         r++, at++) {
        const uint8_t *rd = (const uint8_t *)PTB_NUMREAD_OF(pt) + NR_READ
                            + (size_t)(n - 1) * NR_READ_SIZE;
        uint8_t       *w  = WW_SLOT(wp, at);

        w[WW_KANALEN] = rd[RD_A + r * RD_PAIR_SIZE];
        *(int16_t *)(w + WW_ACCENT) =
            *(const int16_t *)(rd + RD_B + r * RD_PAIR_SIZE);
        w[WW_ATTR] = 0;
        w[WW_POS]  = 0x7e;
        *(int16_t *)(w + WW_OFFSET) =
            *(int16_t *)(WW_SLOT(wp, before) + WW_OFFSET);
        if (r == (int16_t)PTB_B(wp, WP_WORDS) - 1)
            w[WW_CHARS] = rd[RD_LEN];
        else
            w[WW_CHARS] = 0;

        if (w[WW_KANALEN] > 9) {
            if ((int8_t)PTB_B(ta, TA_LONGWORDS) >= TA_LONGWORD_N)
                return NULL;
            w[WW_KANA] = PTB_B(ta, TA_LONGWORDS);
            for (d = 0; d < (int16_t)w[WW_KANALEN]; d++, from++)
                *((uint8_t *)ta + TA_LONGWORD
                  + (size_t)PTB_B(ta, TA_LONGWORDS) * TA_LONGWORD_SIZE
                  + (size_t)d) = rd[RD_CODES + from];
            PTB_B(ta, TA_LONGWORDS) = (uint8_t)(PTB_B(ta, TA_LONGWORDS) + 1);
        } else {
            for (d = 0; d < (int16_t)w[WW_KANALEN]; d++, from++)
                w[WW_KANA + d] = rd[RD_CODES + from];
        }
    }

    for (k = 0; k < (int16_t)after; k++, at++)
        ta_CopyJrtPart(jrtB + (size_t)k * WP_WORD_SIZE, WW_SLOT(wp, at));

    *out = ptb_CompoundWord(pt, wp, row);
    return row;
}

/* ---- the whole of one phrase ----------------------------------------- */

/* The entry point: one accent phrase written out as a row of the table.
 *
 * A row is taken and the words of the phrase are joined into one, either
 * through `CompoundWord' or, where the phrase is a number, through
 * `SetSuushiPhrase', which may leave several rows behind it. Then the moras
 * of the words go into the row's kana with one long entry a word; then the
 * function words after them, whose readings come out of the caller's own
 * table rather than out of the phrase; then the kakari, which says how the
 * phrase in front may attach; then the kind of break the phrase ends on; and
 * last the accent, worked out over the whole run by `FzkAccent' and written
 * back as one long entry a group.
 *
 * Minus six is a refusal: a phrase of more than twenty-five moras is more
 * than a row holds.
 */
int16_t ptb_SetPhraseTable(void *pt, int16_t a, int16_t b, void *wp,
                           uint8_t *c, int16_t *d, int16_t *e)
{
    void    *ta = PTB_OWNER_OF(pt);
    void    *row;
    uint8_t  in[AI_ROOM];
    uint8_t  out[0x40];
    int16_t  nGroups = 0;
    int16_t  start, moras, pos = 0, nLong = 0, extra;
    int16_t  kind = 0, prevKind = 0;
    int16_t  k, at, nFzk;
    uint8_t  flag = 0;
    uint8_t  prevCode;
    uint8_t  nLast = 0;
    const uint8_t *rule = NULL;

    memset(in, 0, sizeof in);
    memset(out, 0, sizeof out);

    row = ptb_GeneratePhraseTable(pt);
    if (row == NULL)
        return -1;

    if (PTB_B(wp, WP_TYPE) != 9) {
        nGroups = ptb_CompoundWord(pt, wp, row);
    } else {
        row = ptb_SetSuushiPhrase(pt, wp, row, &nGroups);
        if (row == NULL)
            return -1;
    }

    start = (int16_t)PTB_B(row, PT_FIRST_WORD);

    moras = 0;
    for (k = start; k < (int16_t)PTB_B(wp, WP_WORDS); k++)
        moras = (int16_t)(moras + *(WW_SLOT(wp, k) + WW_KANALEN));
    for (k = 0; k < (int16_t)PTB_B(wp, WP_FZKS); k++) {
        moras = (int16_t)(moras
                          + c[*(int16_t *)(WF_SLOT(wp, k) + WF_AT)]);
        moras = (int16_t)(moras - 6);
    }
    if (moras > 0x19)
        return -6;
    PTB_B(row, PT_MORAS) = (uint8_t)moras;

    for (k = start; k < (int16_t)PTB_B(wp, WP_WORDS); k++) {
        const uint8_t *w = WW_SLOT(wp, k);
        int16_t        len = (int16_t)w[WW_KANALEN];
        int16_t        j;

        if (len > 0) {
            PTB_B(row, PT_LONG_B + nLong) =
                ptb_GetPosFromTG(pt, w[WW_POS]);
            PTB_W(row, PT_LONG + nLong * 2) = pos;
            nLong++;
        }
        if (len > 9) {
            for (j = 0; j < len; j++)
                PTB_B(row, PT_KANA + pos + j) =
                    *((uint8_t *)ta + TA_LONGWORD
                      + (size_t)w[WW_KANA] * TA_LONGWORD_SIZE + (size_t)j);
        } else {
            for (j = 0; j < len; j++)
                PTB_B(row, PT_KANA + pos + j) = w[WW_KANA + j];
        }
        pos = (int16_t)(pos + w[WW_KANALEN]);
    }

    {
        const uint8_t *w = WW_SLOT(wp, (int16_t)PTB_B(wp, WP_WORDS) - 1);
        int16_t        j;

        for (j = 0; j < (int16_t)w[WW_KANALEN]; j++)
            in[AI_KANA + 1 + nLast++] = w[WW_KANA + j];
    }

    prevCode = 0xff;
    nFzk = 0;
    for (k = 0; k < (int16_t)PTB_B(wp, WP_FZKS); k++) {
        const uint8_t *f = WF_SLOT(wp, k);
        int16_t        j;

        if (pos >= 0x19)
            break;
        nFzk = (int16_t)(k + 1);
        *(int16_t *)(in + AI_MARK + k * 2) = *(int16_t *)(f + WF_OFFSET);
        prevKind = kind;
        kind = (int16_t)(f[WF_CODE] & 0x7f);
        rule = c + *(int16_t *)(f + WF_AT);
        in[AI_LEN + k] = (uint8_t)(rule[0] - 6);
        at = (int16_t)(rule[2] + PTB_W(ta, TA_INTON_FAILED) - 1);
        AI_RULE_OF(in, k) = (uint8_t *)dm_GetAccentAt((uint16_t)at);

        extra = 0;
        for (j = 0; j < (int16_t)in[AI_LEN + k]; j++, pos++) {
            uint8_t code = rule[6 + j];

            PTB_B(row, PT_KANA + pos) = code;
            in[AI_KANA + 1 + nLast++] = code;
            /* IBM keeps the last code of a one-mora function word here and
               never reads it again; the store is kept so that the two sides
               do the same work, and the value goes nowhere. */
            prevCode = in[AI_LEN + k] == 1 ? rule[6 + j] : (uint8_t)0xff;
            (void)prevCode;

            if (pos == (int16_t)(uint16_t)*(int16_t *)(f + WF_ACCENT)) {
                const uint8_t *ic =
                    *(const uint8_t **)((uint8_t *)ta + TA_INPUTCHAR_AT);
                uint16_t       got;

                got = ju_MakeUshort((char *)(ic + 4
                          + (size_t)*(int16_t *)(f + WF_ACCENT) * 2));
                /* The long-vowel mark: IBM's literal is 0x815b, which
                   the mangled name spells as an escape a nibble a
                   letter and which reads as an exclamation mark and a
                   bracket if the escape is skimmed. */
                if (got == ju_MakeUshort("\x81\x5b")) {
                    uint8_t made = (uint8_t)
                        (PTB_B(row, PT_KANA + pos) % 8 + 0xf0);

                    pos++;
                    PTB_B(row, PT_KANA + pos) = made;
                    PTB_B(row, PT_MORAS) = (uint8_t)(PTB_B(row, PT_MORAS) + 1);
                    in[AI_KANA + 1 + nLast++] = made;
                    extra++;
                }
            }
        }
        if (extra > 0)
            in[AI_LEN + k] = (uint8_t)(in[AI_LEN + k] + extra);

        if (kind == 0x0b && prevKind != 0x23) {
            in[AI_ENDS + k] = 2;
        } else if (kind == 0x0c) {
            if (in[AI_LEN + k] == 1
                && (PTB_B(row, PT_KANA - 1 + pos) == 0xf9
                    || PTB_B(row, PT_KANA - 1 + pos) == 0x71))
                in[AI_ENDS + k] = 1;
            else if (in[AI_LEN + k] == 1)
                in[AI_ENDS + k] = 0;
        } else if (kind == 0x11) {
            if (PTB_B(row, PT_KANA - 1 + pos) == 0x60
                || PTB_B(row, PT_KANA - 1 + pos) == 0x94)
                in[AI_ENDS + k] = 1;
            else
                in[AI_ENDS + k] = 0;
        } else if (kind == 0x28) {
            if (in[AI_LEN + k] == 1) {
                if (PTB_B(row, PT_KANA - 1 + pos) == 1)
                    in[AI_ENDS + k] = 1;
                else
                    in[AI_ENDS + k] = 0;
            }
        } else {
            in[AI_ENDS + k] = 0;
        }
    }

    *(int32_t *)((uint8_t *)row + PT_COST) =
        *(int32_t *)((uint8_t *)wp + WP_COST);
    PTB_B(row, PT_TYPE) = PTB_B(wp, WP_TYPE);
    PTB_B(row, 0x0d) = PTB_B(wp, WP_ACCENT);
    PTB_B(row, PT_HOLD) = PTB_B(wp, WP_MORAS);

    /* The group the compounder made last, named one-based: a count of one
       is the first entry of the two runs, which is why these are indexed
       from one before each run rather than from its start. */
    if (ptb_SetUkeTypePhrase(pt, (uint8_t *)row + PT_LEFT, wp) < 0)
        PTB_B(row, PT_MORA_ACC - 1 + nGroups) =
            (uint8_t)(PTB_B(row, PT_MORA_ACC - 1 + nGroups) - 1);
    if (PTB_B(row, PT_MORA - 1 + nGroups)
        < PTB_B(row, PT_MORA_ACC - 1 + nGroups))
        PTB_B(row, PT_MORA_ACC - 1 + nGroups) = 0;

    in[AI_MORAS]  = PTB_B(row, PT_MORA - 1 + nGroups);
    in[AI_ACCENT] = PTB_B(row, PT_MORA_ACC - 1 + nGroups);
    {
        const uint8_t *last = WW_SLOT(wp, (int16_t)PTB_B(wp, WP_WORDS) - 1);

        if (last[WW_ATTR] & 0x10)
            in[AI_KIND] = 1;
        else if (last[WW_POS] == 0x17)
            in[AI_KIND] = 2;
        else
            in[AI_KIND] = 0;
    }
    /* IBM says how many function words the accent walk is to score by
       counting them off the phrase rather than off the loop above, which
       stops early once the phrase has run to the twenty-five moras a row
       holds. Where it stops early the walk then reads a rule pointer the
       loop never stored -- IBM's own uninitialised frame, and this one it
       dereferences. Ours scores the ones that were read. */
    in[AI_WORDS] = (uint8_t)nFzk;

    for (k = 0; k < (int16_t)PTB_B(wp, WP_FZKS); k++)
        ptb_SetSubUkeType(pt, (uint8_t *)row + PT_LEFT,
                          (int16_t)(*(WF_SLOT(wp, k) + WF_CODE) & 0x7f),
                          &flag);
    PTB_B(row, PT_LEFT + 1) = (uint8_t)(PTB_B(row, PT_LEFT + 1)
                                        & ~(PTB_B(row, PT_LEFT)
                                            & PTB_B(row, PT_LEFT + 1)));

    if (PTB_B(wp, WP_FZKS) > 0) {
        if (PTB_B(wp, WP_WORDS) == 1
            && *(WW_SLOT(wp, 0) + WW_KANALEN) == 0) {
            PTB_B(row, PT_RIGHT) = 2;
        } else {
            uint8_t at2 = (uint8_t)((rule[3] - 1) & 0x3f);

            for (k = 0; k < 6; k++)
                PTB_B(row, PT_RIGHT + k) = dm_GetKakariAt((uint16_t)
                                                          (at2 * 6 + k));
            ptb_ExtKKRPhrase(pt, (uint8_t *)row + PT_RIGHT, kind, &flag);
            if (PTB_B(wp, WP_FZKS) == 1 && kind == 0x1c)
                PTB_B(row, PT_RIGHT + 2) =
                    (uint8_t)(PTB_B(row, PT_RIGHT + 2) | 0x80);
        }
    } else {
        ptb_SetNoneFzkKKR(pt, (uint8_t *)row + PT_RIGHT, wp);
        if (PTB_B(wp, WP_WORDS) == 1 && PTB_B(wp, WP_FZKS) == 0
            && (dm_GetTGAt2(*(WW_SLOT(wp, 0) + WW_POS), 1) & 0x30))
            PTB_B(row, PT_RIGHT + 2) =
                (uint8_t)(PTB_B(row, PT_RIGHT + 2) | 0x80);
    }
    if (PTB_B(wp, WP_TYPE) == 0x0a)
        PTB_B(row, PT_RIGHT) = (uint8_t)(PTB_B(row, PT_RIGHT) | 0x80);

    PTB_B(row, PT_GROUP) = (uint8_t)kind;

    if ((int8_t)PTB_B(ta, TA_UNKNOWN_10) > 0 && a == (int16_t)(b - 1)) {
        const uint8_t *ic = *(const uint8_t **)((uint8_t *)ta
                                                + TA_INPUTCHAR_AT);

        if (*(const int32_t *)(ic + IC_ENDED) != 0) {
            const char *at3 = (const char *)(ic + IC_ENDMARK);

            if (at3[0] == 0x3f)
                PTB_B(row, PT_KIND) = 5;
            else if (at3[0] == 0x21)
                PTB_B(row, PT_KIND) = 6;
            else if (at3[0] == 0x60)
                PTB_B(row, PT_KIND) = 2;
            else if (at3[0] == 0x2c)
                PTB_B(row, PT_KIND) = 3;
            else
                PTB_B(row, PT_KIND) = 4;
        } else {
            PTB_B(row, PT_KIND) = 3;
        }
    } else if (PTB_B(row, PT_MORAS) >= 0x19) {
        PTB_B(row, PT_KIND) = 3;
    } else {
        PTB_B(row, PT_KIND) = 1;
    }

    in[AI_AT79] = PTB_B(row, PT_KIND);
    ptb_FzkAccent(pt, in, out);

    {
        int16_t where = 0;
        int16_t back;
        int32_t first = 0;

        if (nLong > 0) {
            where = (int16_t)(PTB_W(row, PT_LONG + (nLong - 1) * 2)
                     + *(WW_SLOT(wp, (int16_t)PTB_B(wp, WP_WORDS) - 1)
                         + WW_KANALEN));
            back = (int16_t)(where - in[AI_MORAS]);
        } else {
            back = *e;
            where = back;
        }

        if (out[AO_MORAS] > in[AI_MORAS]) {
            first = 1;
            for (k = 0; k < (int16_t)in[AI_WORDS]; k++) {
                if (where >= (int16_t)(out[AO_MORAS] + back))
                    break;
                PTB_W(row, PT_LONG + nLong * 2) = where;
                PTB_B(row, PT_LONG_B + nLong) =
                    ptb_GetFzkPosFromTG(pt, *(WF_SLOT(wp, k) + WF_CODE));
                nLong++;
                if (where + in[AI_LEN + k] <= (int16_t)(out[AO_MORAS] + back)) {
                    where = (int16_t)(where + in[AI_LEN + k]);
                } else if (first != 0) {
                    where = (int16_t)(where
                                      + (out[AO_MORAS] + back - where));
                    first = 0;
                } else {
                    where = (int16_t)(where + in[AI_LEN + k]);
                }
            }
        }

        for (k = 0; k < 15; k++) {
            PTB_B(row, PT_MORA_HI + k)     = out[AO_LEN + k];
            PTB_B(row, PT_MORA_HI_ACC + k) = out[AO_ACC + k];
            PTB_W(row, PT_MORA_HI_VAL + k * 2) =
                *(int16_t *)(out + AO_MARK + k * 2);
            if (out[AO_LEN + k] > 0) {
                PTB_W(row, PT_LONG + nLong * 2) = where;
                PTB_B(row, PT_LONG_B + nLong)   = 9;
                nLong++;
                where = (int16_t)(where + out[AO_LEN + k]);
            }
        }
    }

    PTB_B(row, PT_MORA - 1 + nGroups) = out[AO_MORAS];
    if (out[AO_MORAS] < out[AO_ACCENT])
        PTB_B(row, PT_MORA_ACC - 1 + nGroups) = 0;
    else
        PTB_B(row, PT_MORA_ACC - 1 + nGroups) = out[AO_ACCENT];

    PT_LINK(row) = 0;
    PTB_W(row, PT_LONG_N) = nLong;
    *d = (int16_t)(*d + nLong);
    *e = (int16_t)(*e + pos);
    (void)a;
    (void)b;
    return 0;
}

/* ---- making and unmaking one ----------------------------------------- */

/* The one thing a phrase table owns: the number reader it hands a run of
   digits to. Nought is the allocation failing, which the caller passes on. */
int32_t ptb_initialize(void *pt)
{
    void *nr = cpp_new(NR_ROOM);

    if (nr != NULL)
        *(void **)((uint8_t *)nr + NR_OWNER_AT) = PTB_OWNER_OF(pt);
    PTB_NUMREAD_OF(pt) = nr;
    return PTB_NUMREAD_OF(pt) != NULL;
}

void ptb_dtor(void *pt)
{
    if (PTB_NUMREAD_OF(pt) != NULL)
        cpp_delete(PTB_NUMREAD_OF(pt));
}

void *ptb_destroy(void *pt, int32_t freeIt)
{
    ptb_dtor(pt);
    if (freeIt & 1)
        cpp_delete(pt);
    return pt;
}
