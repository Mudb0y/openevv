/* The part of TextAnalysis that settles a phrase.
 *
 * `Kakutei' is what the analysis calls fixing a phrase: the search has
 * produced a way of reading a stretch of text and this is where that reading
 * stops being a candidate and becomes the answer. Four methods of
 * `TextAnalysis' are in this object beside it, two of them static.
 *
 * Only `CopyJrtPart' is written so far, because `PhraseTable::SetSuushiPhrase'
 * wants it: a phrase whose words are digits is taken apart, read through
 * `NumRead', and put back together, and the taking apart and putting back is
 * this copy.
 */

#include <stdint.h>
#include "jprom.h"
#include "phrasebuf.h"

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
