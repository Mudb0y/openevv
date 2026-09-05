/* The part of TextAnalysis that settles a phrase.
 *
 * `Kakutei' is what the analysis calls fixing a phrase: the search has
 * produced a way of reading a stretch of text and this is where that reading
 * stops being a candidate and becomes the answer. Four methods of
 * `TextAnalysis' are in this object beside it, two of them static.
 *
 * Three of the five are written. `Kakutei' and `UpdatePhraseBuffer' wait on
 * `SearchJrtSeparate', which is in `txtanal.obj'.
 */

#include <stdint.h>
#include "jprom.h"
#include "txtanal.h"
#include "phrasebuf.h"

#define WP_B(w, off)    (*((uint8_t *)(w) + (off)))

/* One word copied from one place to another.
 *
 * What IBM calls a `_P_JRT_T' is the same eighteen bytes as a word of a
 * phrase, so `rom/jajp/phrasebuf.h' is its map as well: how long the reading
 * is, where the accent falls, how many characters it covers, its attribute
 * byte, the reading itself, the part of speech and the offset into the text.
 *
 * Note the reading is copied by the length in the destination rather than in
 * the source -- which is the same number, since the length was copied first
 * -- and that no more than nine codes are copied whatever the length says.
 */
void ta_CopyJrtPart(const void *src, void *dst)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t       *d = (uint8_t *)dst;
    int16_t        n;
    int16_t        i;

    d[WW_KANALEN] = s[WW_KANALEN];
    *(int16_t *)(d + WW_ACCENT) = *(const int16_t *)(s + WW_ACCENT);
    d[WW_CHARS] = s[WW_CHARS];
    d[WW_ATTR]  = s[WW_ATTR];
    *(int16_t *)(d + WW_OFFSET) = *(const int16_t *)(s + WW_OFFSET);

    n = d[WW_KANALEN] > 9 ? 9 : (int16_t)d[WW_KANALEN];
    for (i = 0; i < n; i++)
        d[WW_KANA + i] = s[WW_KANA + i];
    d[WW_POS] = s[WW_POS];
}

/* One function word copied from one phrase to another, at an index apiece.
   Five fields and no reading: a function word carries its own number, where
   its rule is in the dictionary, how many codes it reads to, where its accent
   falls and where in the text it began. */
void ta_CopyFzkPart(const void *src, void *dst, int16_t si, int16_t di)
{
    const uint8_t *s = WF_SLOT(src, si);
    uint8_t       *d = WF_SLOT(dst, di);

    d[WF_CODE] = s[WF_CODE];
    *(int16_t *)(d + WF_AT)     = *(const int16_t *)(s + WF_AT);
    d[WF_KANALEN] = s[WF_KANALEN];
    *(int16_t *)(d + WF_ACCENT) = *(const int16_t *)(s + WF_ACCENT);
    *(int16_t *)(d + WF_OFFSET) = *(const int16_t *)(s + WF_OFFSET);
}

/* How many moras the words of a phrase run to, and a correction on the way.
 *
 * A phrase of the ninth kind is a run of digits, and a word of it whose tags
 * say both of two things does not read as its own characters at all: what it
 * reads as is a number, and the number of moras that takes is worked out from
 * how many of its codes are digits rather than from how many codes it has.
 * Two moras for the number itself and four for each digit up to four, and the
 * word's own length is written back with that. The caller is told through its
 * pointer how long the word it corrected was.
 *
 * A reading of more than nine codes is not in the word at all but in the
 * analysis's own long-word store, which the word indexes with its first code.
 */
uint8_t ta_CountMoraInPhrase(void *ta, void *wp, int16_t *out)
{
    uint8_t total = 0;
    int16_t i;

    /* IBM works out the last word's number and clears a second local here,
       and reads neither again. */
    for (i = 0; i < (int16_t)WP_B(wp, WP_WORDS); i++) {
        uint8_t *w = WW_SLOT(wp, i);

        if (WP_B(wp, WP_TYPE) == 9
            && (dm_GetTGAt2(w[WW_POS], 2) & 0x20)
            && (dm_GetTGAt2(w[WW_POS], 3) & 0x10)) {
            int16_t n = 0;
            int16_t j;

            *out = (int16_t)w[WW_KANALEN];
            if (*out > 9) {
                const uint8_t *lw = (const uint8_t *)ta + TA_LONGWORD
                                    + (size_t)w[WW_KANA] * TA_LONGWORD_SIZE;

                for (j = 0; j < *out; j++)
                    /* IBM asks as well that the code not be negative, which
                       it reads as unsigned and so can never be. */
                    if (lw[j] <= 9)
                        n++;
            } else {
                for (j = 0; j < *out; j++)
                    if (w[WW_KANA + j] <= 9)
                        n++;
            }
            w[WW_KANALEN] = (uint8_t)(4 * (n > 4 ? 4 : n) + 2);
        }
        total = (uint8_t)(total + w[WW_KANALEN]);
    }
    return total;
}

/* The phrase the search settled on, written out as phrases a row can hold.
 *
 * What arrives is one stretch of words and function words, which may be more
 * than the twenty-five moras a row of the phrase table holds. What leaves is
 * one or more phrases in the analysis's own working area, and the answer is
 * how many. While the stretch is too long, `SearchJrtSeparate' says where to
 * cut it, the words up to the cut are copied out as a phrase of their own and
 * their moras taken off the total; what is left over goes out as the last
 * phrase with the function words on the end of it.
 *
 * Three things are worth naming. A phrase of the ninth kind -- a run of digits
 * -- loses that kind on the way out and takes it back only where a word of it
 * still carries the two tags that say it reads as a number, and that word's
 * reading is put back to the length `CountMoraInPhrase' found rather than the
 * one it wrote. The walk that does that over a cut-off phrase is bounded by
 * the whole stretch's word count rather than by how many words were copied, so
 * it reads words of the destination that this call did not write. And a cut
 * the search refuses ends the loop by clearing the mora count rather than by
 * leaving it, so what is left goes out as one phrase however long it is.
 */
int16_t ta_UpdatePhraseBuffer(void *ta, void *wp, const uint8_t *dict)
{
    /* IBM does not clear this, and only `CountMoraInPhrase' writes it -- and
       that method looks at the tags only where the phrase is of the ninth
       kind, while the two walks below look at them whatever the kind is. So a
       phrase of another kind whose word carries both tags has its reading
       written back from a number nothing set. Ours reads nought, which is the
       seventeenth deliberate divergence. */
    int16_t  origLen = 0;
    uint8_t  moras;
    int16_t  fzk = 0;
    int16_t  i, j;
    int16_t  start = 0;
    /* IBM keeps the last word's number and the count of function words in
       one local, which is why this is written twice and read between. */
    int16_t  bound;
    int16_t  out = 0;
    uint8_t *dst;
    uint8_t  sum;

    moras = ta_CountMoraInPhrase(ta, wp, &origLen);

    for (i = 0; i < (int16_t)WP_B(wp, WP_FZKS); i++)
        fzk = (int16_t)(fzk
                        + dict[*(int16_t *)(WF_SLOT(wp, i) + WF_AT)] - 6);
    moras = (uint8_t)(moras + fzk);

    bound = (int16_t)(WP_B(wp, WP_WORDS) - 1);
    dst  = WP_SLOT((uint8_t *)ta + TA_WORK, out);

    while (moras > 0x19) {
        int16_t cut = ta_SearchJrtSeparate(ta, wp, start, bound, moras);
        uint8_t chars = 0;
        uint8_t kana  = 0;

        if (cut < 0) {
            moras = 0;
            continue;
        }

        j = 0;
        for (i = start; i <= cut; i++, j++) {
            ta_CopyJrtPart(WW_SLOT(wp, i), WW_SLOT(dst, j));
            chars = (uint8_t)(chars + WW_SLOT(dst, j)[WW_CHARS]);
            kana  = (uint8_t)(kana + WW_SLOT(dst, j)[WW_KANALEN]);
        }
        *(int16_t *)(dst + WP_MORAS) = (int16_t)chars;
        dst[WP_ACCENT]  = 0;
        dst[WP_WORDS]   = (uint8_t)j;
        dst[WP_KANALEN] = kana;
        dst[WP_FZKS]    = 0;
        dst[WP_CHARS]   = 0;
        *(int32_t *)(dst + WP_COST) = *(const int32_t *)((uint8_t *)wp
                                                         + WP_COST);
        dst[WP_TYPE] = WP_B(wp, WP_TYPE) == 9 ? 0 : WP_B(wp, WP_TYPE);
        moras = (uint8_t)(moras - kana);

        /* Bounded by the whole stretch rather than by what was copied. */
        for (j = 0; j < (int16_t)WP_B(wp, WP_WORDS); j++) {
            uint8_t *w = WW_SLOT(dst, j);

            if ((dm_GetTGAt2(w[WW_POS], 2) & 0x20)
                && (dm_GetTGAt2(w[WW_POS], 3) & 0x10)) {
                w[WW_KANALEN] = (uint8_t)origLen;
                dst[WP_TYPE]  = 9;
            }
        }

        out++;
        dst   = WP_SLOT((uint8_t *)ta + TA_WORK, out);
        start = (int16_t)(cut + 1);
    }

    /* IBM walks the function words here and does nothing with them. */
    bound = (int16_t)WP_B(wp, WP_FZKS);
    dst[WP_TYPE] = WP_B(wp, WP_TYPE) == 9 ? 0 : WP_B(wp, WP_TYPE);
    for (j = start; j < (int16_t)WP_B(wp, WP_WORDS); j++) {
        uint8_t *w = WW_SLOT(wp, j);

        if ((dm_GetTGAt2(w[WW_POS], 2) & 0x20)
            && (dm_GetTGAt2(w[WW_POS], 3) & 0x10)) {
            w[WW_KANALEN] = (uint8_t)origLen;
            dst[WP_TYPE]  = 9;
        }
    }

    sum = 0;
    dst[WP_ACCENT] = WP_B(wp, WP_ACCENT);
    j = 0;
    for (i = start; i < (int16_t)WP_B(wp, WP_WORDS); i++, j++) {
        ta_CopyJrtPart(WW_SLOT(wp, i), WW_SLOT(dst, j));
        sum = (uint8_t)(sum + WW_SLOT(dst, j)[WW_CHARS]);
    }
    dst[WP_KANALEN] = sum;
    dst[WP_WORDS]   = (uint8_t)j;

    sum = 0;
    j = 0;
    for (i = 0; i < bound; i++, j++) {
        ta_CopyFzkPart(wp, dst, i, j);
        sum = (uint8_t)(sum + WF_SLOT(dst, j)[WF_KANALEN]);
    }
    dst[WP_FZKS] = (uint8_t)bound;
    *(int16_t *)(dst + WP_MORAS) = (int16_t)(dst[WP_KANALEN] + sum);
    dst[WP_CHARS] = 0;
    *(int32_t *)(dst + WP_COST) = *(const int32_t *)((uint8_t *)wp + WP_COST);
    out++;
    return out;
}

/* One settled stretch of text turned into rows of the phrase table.
 *
 * The dictionary the function words are read out of depends on whether the
 * intonation pass gave up on the sentence before it: the ordinary table where
 * it did not, and the extended one where it did.
 */
int16_t ta_Kakutei(void *ta, void *wp)
{
    const uint8_t *dict;
    int16_t        nLong = 0;
    int16_t        where = 0;
    int16_t        n, i;

    if (*(int16_t *)((uint8_t *)ta + TA_INTON_FAILED) == 0)
        dict = dm_GetFuncDictEx();
    else
        dict = dm_GetFuncDict();

    n = ta_UpdatePhraseBuffer(ta, wp, dict);
    for (i = 0; i < n; i++) {
        int16_t rc = ptb_SetPhraseTable(
            *(void **)((uint8_t *)ta + TA_PHRASETABLE_AT), i, n,
            WP_SLOT((uint8_t *)ta + TA_WORK, i), (uint8_t *)dict,
            &nLong, &where);

        if (rc < 0)
            return rc;
    }
    *(int16_t *)((uint8_t *)ta + TA_INTON_FAILED) = 0;
    return 0;
}
