/* What a phrase will attach to, worked out for a link rather than for a row.
 *
 * `PhraseTable::SetUkeTypePhrase' answers the same question for a phrase that
 * is becoming a row of the phrase table: which phrases may sit in front of
 * this one, as bits in the kakari. This object answers it for a link in the
 * search, before any of that is settled, and IBM keeps the answer in four
 * named methods rather than in one long one -- the two halves of a compound,
 * the function word on the end, and the extra bits a tag adds.
 *
 * The four are close to `SetUkeTypePhrase' but not the same, and the places
 * they differ are the reason this is a separate object rather than a call:
 * a phrase of the tenth kind is cleared here and set to 0xe4 there, the third
 * tag byte's bit two clears the answer here and is not asked there, and the
 * last test is the fourth tag byte's bit six here against bits six and two
 * there.
 */

#include <stdint.h>
#include "jprom.h"
#include "phrasebuf.h"

#define WP_B(w, off)    (*((uint8_t *)(w) + (off)))

/* The extra bits a tag adds to a kakari that is already worked out. Six tags
   each set one bit of the sixth byte, one sets two bits of the second, and
   the last two set the top two bits of the first -- but only where the caller
   says so through a flag of its own. */
void ta_ExtKkrForLink(void *ta, uint8_t *kkr, int16_t tg, const uint8_t *other)
{
    (void)ta;
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
    case 0x53:
        kkr[5] |= 0x02;
        break;
    case 0x54:
        kkr[5] |= 0x01;
        break;
    case 0x55:
        kkr[1] |= 0x0c;
        break;
    case 0x56:
    case 0x57:
        if (other[0] & 0x01)
            kkr[0] |= 0xc0;
        break;
    default:
        break;
    }
}

/* A compound gets two answers rather than one: the first word's bits and the
   last word's, out of the same four tag bits apiece. Where the two want a bit
   in common the compound cannot be split there at all, so nothing is written
   and the caller is told to put its answer in the first byte; otherwise the
   first word's bits go in the first byte and the caller is told to put its
   own in the second. */
int16_t ta_SetJWordUkeTypeForLink(void *ta, uint8_t *uke,
                                  const uint8_t *tg0, const uint8_t *tgL)
{
    uint8_t a = 0;
    uint8_t b = 0;

    (void)ta;
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
    if (a & b)
        return 0;
    uke[0] |= a;
    return 1;
}

/* The function word on the end of the phrase decides the answer on its own
   where it says anything at all, and the three tag bytes are asked in turn:
   the first outright, the second against the function word's own number, and
   the third against it as well. A tag that says nothing in any of the three
   clears the answer. */
void ta_SetFWordUkeTypeForLink(void *ta, uint8_t at, uint8_t fzk, uint8_t *uke,
                               const uint8_t *tg0, const uint8_t *tgL)
{
    (void)ta;
    (void)tg0;
    if (tgL[0] != 0) {
        uke[at] |= 0x80;
        return;
    }
    if (tgL[1] != 0) {
        if (tgL[1] & 0xd0) {
            uke[at] |= 0x80;
            return;
        }
        /* This arm returns only where it set the bit; where it did not, the
           test below is asked as well. */
        if (tgL[1] & 0x0e) {
            if ((uke[at] & 0x80) || (fzk >= 0x23 && fzk <= 0x26)
                || fzk == 0x1f || fzk == 0x0b) {
                uke[at] |= 0x80;
                return;
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
        return;
    }
    if (tgL[2] != 0) {
        if (tgL[2] & 0xc0) {
            if (fzk >= 0x29 && fzk <= 0x32) {
                uke[at] |= 0x40;
                return;
            }
            if (fzk == 0x4e || fzk == 0x13) {
                uke[at] |= 0x40;
                uke[at] |= 0x20;
                return;
            }
        }
        if (tgL[2] & 0x21) uke[at] |= 0x20;
        if (tgL[2] & 0x08) uke[at] |= 0x10;
        if (tgL[2] & 0x10) uke[at] = 0x08;
        if (tgL[2] & 0x02) uke[at] = 0x00;
        return;
    }
    uke[at] = 0;
}

/* The whole answer for one phrase. The compound is settled first, since it
   says which byte the rest of the answer goes in; then the phrase kind, the
   function word, and last the tags of the phrase's own last word.
 *
 * Minus one is a refusal rather than an error, exactly as in
 * `PhraseTable::SetUkeTypePhrase': a last word carrying one of two tags and
 * an accent that is neither nought nor one takes nothing in front of it.
 */
int16_t ta_SetUkeTypeForLink(void *ta, uint8_t *uke, void *wp)
{
    int16_t        last = (int16_t)(WP_B(wp, WP_WORDS) - 1);
    const uint8_t *tg0  = dm_GetTGAt(*(WW_SLOT(wp, 0) + WW_POS));
    const uint8_t *tgL  = dm_GetTGAt(*(WW_SLOT(wp, last) + WW_POS));
    uint8_t        at;

    if (WP_B(wp, WP_WORDS) > 1)
        at = (uint8_t)ta_SetJWordUkeTypeForLink(ta, uke, tg0, tgL);
    else
        at = 0;

    if (WP_B(wp, WP_TYPE) == 0x0a) {
        uke[at] = 0;
        return 0;
    }
    if (WP_B(wp, WP_FZKS) != 0) {
        ta_SetFWordUkeTypeForLink(ta, at,
                                  (uint8_t)(WP_B(wp, WP_FZK) & 0x7f),
                                  uke, tg0, tgL);
        return 0;
    }
    if (WP_B(wp, WP_TYPE) == 0x06) {
        uke[at] = 0x02;
        return 0;
    }
    if (tgL[1] != 0) {
        if (tgL[1] & 0x30) {
            uint16_t v = *(uint16_t *)(WW_SLOT(wp, last) + WW_ACCENT);

            uke[at] |= 0x80;
            /* IBM asks of the accent that it be other than nought, and then
               of that same accent that it be one -- so nothing passes and
               this arm always refuses. The row's own reader asks the two the
               other way round and lets nought and one through, which is what
               says the shape here is IBM's and not a misreading. */
            if (v != 0)
                return -1;
            if (v == 1)
                return 0;
            return -1;
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
        if (tgL[2] & 0x04) uke[at] = 0x00;
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
    if (tgL[3] & 0x40)
        uke[at] = 0x01;
    else
        uke[at] = 0x00;
    return 0;
}
